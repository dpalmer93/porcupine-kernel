/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <mips/vm.h>
#include <mips/tlb.h>
#include <kern/errno.h>
#include <cpu.h>
#include <lib.h>
#include <synch.h>
#include <addrspace.h>
#include <coremem.h>
#include <swap.h>
#include <vmstat.h>
#include <page_table.h>

#define LEVEL_SIZE 1024
#define INDEX_TO_VADDR(l1, l2) (((l1) << 22) + ((l2) << 12))
#define L1_INDEX(va) (PAGE_NUM(va) >> 10) // index into the level 1 table
#define L2_INDEX(va) (PAGE_NUM(va) & (LEVEL_SIZE - 1)) // index into the level 2 table

static void pte_destroy(struct pt_entry *pte);
static bool pte_incr_ref(struct pt_entry *pte);
static struct pt_entry *pte_copy(vaddr_t vaddr, struct pt_entry *old_pte);
static struct pt_entry *pte_copy_deep(vaddr_t vaddr, struct pt_entry *old_pte);

struct page_table
{
    struct pt_entry **pt_index[LEVEL_SIZE];
};

struct page_table *
pt_create(void)
{
    struct page_table *pt = kmalloc(sizeof(struct page_table));
    if (pt == NULL)
        return NULL;
    
    // zero the index
    bzero(pt->pt_index, LEVEL_SIZE * sizeof(struct pt_entry **));
    
    return pt;
}

void
pt_destroy(struct page_table *pt)
{
    for (int i = 0; i < LEVEL_SIZE; i++) {
        if (pt->pt_index[i] != NULL) {
            for (int j = 0; j < LEVEL_SIZE; j++) {
                if (pt->pt_index[i][j] != NULL) {
                    pte_destroy(pt->pt_index[i][j]);
                }
            }
            kfree(pt->pt_index[i]);
        }
    }
    kfree(pt);
}

struct page_table *
pt_copy_deep(struct page_table *old_pt)
{
    struct page_table *new_pt = pt_create();
    if (new_pt == NULL)
        return NULL;
    
    for (int i = 0; i < LEVEL_SIZE; i++) {
        if (old_pt->pt_index[i] == NULL)
            continue;
        
        // Create second level page table
        new_pt->pt_index[i] = kmalloc(LEVEL_SIZE * sizeof(struct pt_entry *));
        if (new_pt->pt_index[i] == NULL) {
            pt_destroy(new_pt);
            return NULL;
        }
        bzero(new_pt->pt_index[i], LEVEL_SIZE * sizeof(struct pt_entry *));
        
        for (int j = 0; j < LEVEL_SIZE; j++) {
            if (old_pt->pt_index[i][j] == NULL)
                continue;
            
            // Deeply copy every page table entry
            struct pt_entry *old_pte = pt_acquire_entry(old_pt, INDEX_TO_VADDR(i, j));
            struct pt_entry *new_pte = pte_copy_deep(INDEX_TO_VADDR(i, j), old_pte);
            if (new_pte == NULL) {
                pt_destroy(new_pt);
                return NULL;
            }
            
            new_pt->pt_index[i][j] = new_pte;
            
            // unlock the new PTE (pte_copy() unlocked the old one if necessary)
            pte_unlock(new_pte);
        }
    }
    return new_pt;
}

struct page_table *
pt_copy_shallow(struct page_table *old_pt)
{
    struct page_table *new_pt = pt_create();
    if (new_pt == NULL)
        return NULL;
        
    for (int i = 0; i < LEVEL_SIZE; i++) {
        if (old_pt->pt_index[i] == NULL)
            continue;
            
        // Create second level page table
        new_pt->pt_index[i] = kmalloc(LEVEL_SIZE * sizeof(struct pt_entry *));
        if (new_pt->pt_index[i] == NULL) {
            pt_destroy(new_pt);
            return NULL;
        }
        bzero(new_pt->pt_index[i], LEVEL_SIZE * sizeof(struct pt_entry *));
        
        for (int j = 0; j < LEVEL_SIZE; j++) {
            if (old_pt->pt_index[i][j] == NULL)
                continue;    
            
            // Shallowly copy every page table entry
            struct pt_entry *old_pte = pt_acquire_entry(old_pt, INDEX_TO_VADDR(i, j));
            struct pt_entry *new_pte = pte_copy(INDEX_TO_VADDR(i, j), old_pte);
            if (new_pte == NULL) {
                pt_destroy(new_pt);
                return NULL;
            }
            
            new_pt->pt_index[i][j] = new_pte;
            
            // unlock the new PTE (pte_copy() unlocked the old one if necessary)
            pte_unlock(new_pte);
        }
    }
    // clear all dirty bits in the TLB
    tlb_cleanall();
    return new_pt;
}

// The PTE in the page table with that virtual address must be locked
// Makes a deep copy of that PTE and returns it
// Unlocks the old PTE; the new PTE is returned locked
struct pt_entry *
pt_copyonwrite(struct page_table* pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    struct pt_entry *old_pte = pt->pt_index[l1_idx][l2_idx];
    
    KASSERT(old_pte != NULL);
    KASSERT(old_pte->pte_busy);
    KASSERT(old_pte->pte_refcount > 1);
    
    old_pte->pte_refcount--;    
    return pte_copy_deep(vaddr, old_pte);
}


/**************** SYNCHRONIZATION FUNCTIONS ****************/

bool
pte_try_lock(struct pt_entry *pte)
{
    // registers to perform the test-and-set
    uint32_t x;
    uint32_t y = 0x80000000;
    
    // test first to reduce contention
    if (pte->pte_busy)
        return false;
    
    // try to set the low bit (the busy bit) atomically
    __asm volatile(
                   ".set push;"         // save assembler mode
                   ".set mips32;"		// allow MIPS32 instructions
                   ".set volatile;"     // avoid unwanted optimization
                   "ll %0, 0(%2);"		//   x = *pte
                   "or %1, %0, %1;"     //   y = x; y.pte_busy = 1
                   "sc %1, 0(%2);"		//   *pte = y; y = success?
                   ".set pop"           // restore assembler mode
                   : "=r" (x), "+r" (y) : "r" (pte));
    
    // if the LL/SC failed, y will be zero
    if (y == 0)
        return false;
    
    // otherwise, return true if the PTE was not previously busy
    return !(x & 0x80000000);
}

void
pte_unlock(struct pt_entry *pte)
{
    pte->pte_busy = 0;
}

// Guaranteed to return the page table entry if one exists
struct pt_entry *
pt_acquire_entry(struct page_table *pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    
    /*
     * We will only be acquiring our own entries.  Since
     * there are no multithreaded processes in this system,
     * we do not need to worry about partially created PTEs
     */
    if (pt->pt_index[l1_idx] == NULL)
        return NULL;
    
    struct pt_entry *pte = pt->pt_index[l1_idx][l2_idx];
    if (pte == NULL)
        return NULL;
    
    // wait until the PTE becomes available
    // If the PTE is being paged in, wait on swap
    while (!pte_try_lock(pte)) {
        swap_wait_lock();
        if (pte->pte_swapin)
            swap_wait();
        else
            swap_wait_unlock();
    }
    
    return pte;
}

// The created entry is locked.  It must be unlocked with
// pte_unlock() when the operations on it are complete
struct pt_entry *
pt_create_entry(struct page_table *pt, vaddr_t vaddr, paddr_t frame)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    
    struct pt_entry **l2_tbl = pt->pt_index[l1_idx];
    if (l2_tbl == NULL) {
        // create a new level 2 table
        l2_tbl = kmalloc(LEVEL_SIZE * sizeof(struct pt_entry *));
        if (l2_tbl == NULL)
            return NULL;
        
        // zero all its entries
        bzero(l2_tbl, LEVEL_SIZE * sizeof(struct pt_entry *));
    }
    
    struct pt_entry *pte = l2_tbl[l2_idx];
    if (pte == NULL) {
        // allocate a new PTE
        pte = kmalloc(sizeof(struct pt_entry));
        if (pte == NULL) {
            // if we allocated a new L2 table earlier, free it
            if (l2_tbl != pt->pt_index[l1_idx]) kfree(l2_tbl);
            return NULL;
        }
    }
    else // entry already exists
        return NULL;
    
    // initialize and lock entry
    pte->pte_busy = 1;
    pte->pte_inmem = 1;
    pte->pte_refcount = 1;
    pte->pte_active = 0;
    pte->pte_dirty = 0;
    pte->pte_cleaning = 0;
    pte->pte_swapin = 0;
    pte->pte_frame = PAGE_NUM(frame);
    
    // save our new structures
    l2_tbl[l2_idx] = pte;
    pt->pt_index[l1_idx] = l2_tbl;
    
    return pte;
}

// Assumes that there is only 1 reference to PTE
void
pt_destroy_entry(struct page_table *pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    
    if (pt->pt_index[l1_idx] == NULL)
        return;
    
    struct pt_entry *pte = pt->pt_index[l1_idx][l2_idx];
    if (pte == NULL)
        return;
    
    // just free the PTE.  We assume the caller will deal
    // with associated swap/core, as the caller might still
    // have these locked
    kfree(pte);
    pt->pt_index[l1_idx][l2_idx] = NULL;
}

/************ Page Table Entry Helper Functions ************/

// if the pte has no more references to it, destroy it
static
void
pte_destroy(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    
    pte->pte_refcount--;
    if (pte->pte_refcount == 0) {
        // free the page frame and/or swap space
        if (pte->pte_inmem) {
            core_free_frame(MAKE_ADDR(pte->pte_frame, 0));
            
            // update stats
            if (pte->pte_active)
                vs_decr_ram_active();
            else
                vs_decr_ram_inactive();
            
            if (pte->pte_dirty)
                vs_decr_ram_dirty();
        }
        else
            swap_free(pte->pte_swapblk);
        
        // free the PTE
        kfree(pte);
    }
}

// Must be called with the PTE locked
static
bool
pte_incr_ref(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    if (pte->pte_refcount < MAX_PTEREFCOUNT) {
        pte->pte_refcount++;
        return true;
    }
    return false;
}

// Must be called with old PTE locked
// Increments refcount in old PTE and returns pointer to old PTE
// If refcount is too high, makes a deep copy
static
struct pt_entry *
pte_copy(vaddr_t vaddr, struct pt_entry *old_pte)
{
    KASSERT(old_pte != NULL);
    KASSERT(old_pte->pte_busy);
    
    if (pte_incr_ref(old_pte))
        return old_pte;
    else
        return pte_copy_deep(vaddr, old_pte);
}

// Must be called with old PTE locked
// Copies PTE and copies the corresponding data into
// a new physical page
// Returns a locked PTE and unlocks the old one
// On failure, returns NULL.
static
struct pt_entry *
pte_copy_deep(vaddr_t vaddr, struct pt_entry *old_pte)
{
    KASSERT(old_pte != NULL);
    KASSERT(old_pte->pte_busy);
    
    struct pt_entry *new_pte = kmalloc(sizeof(struct pt_entry));
    if (new_pte == NULL)
        return NULL;
    
    // acquire a page frame
    paddr_t new_frame = core_acquire_frame();
    if (new_frame == 0) {
        pte_unlock(old_pte);
        kfree(new_pte);
        return NULL;
    }
    
    // copy the old page's data
    if (old_pte->pte_inmem) {
        paddr_t old_frame = MAKE_ADDR(old_pte->pte_frame, 0);
        memcpy((void *)PADDR_TO_KVADDR(new_frame),
               (void *)PADDR_TO_KVADDR(old_frame), PAGE_SIZE);
    }
    else {
        if (swap_in(old_pte->pte_swapblk, new_frame)) {
            core_release_frame(new_frame);
            pte_unlock(old_pte);
            kfree(new_pte);
            return NULL;
        }
    }
    
    // get a new swap block
    swapidx_t new_swapblk;
    if (swap_get_free(&new_swapblk)) {
        core_release_frame(new_frame);
        pte_unlock(old_pte);
        kfree(new_pte);
        return NULL;
    }
    
    // update the coremap
    core_map_frame(new_frame, vaddr, new_pte, new_swapblk);
    core_release_frame(new_frame);
    
    new_pte->pte_busy = 1;
    new_pte->pte_inmem = 1;
    new_pte->pte_refcount = 1;
    new_pte->pte_active = 0;
    new_pte->pte_dirty = 1;
    new_pte->pte_cleaning = 0;
    new_pte->pte_swapin = 0;
    new_pte->pte_frame = PAGE_NUM(new_frame);

    // unlock the old entry
    pte_unlock(old_pte);
    
    return new_pte;
}

// Must be called with the PTE locked
bool
pte_try_access(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    if (pte->pte_inmem) {
        // update stats
        if (!pte->pte_active) {
            vs_decr_ram_inactive();
            vs_incr_ram_active();
        }
        
        pte->pte_active = 1;
        return true;
    }
    return false;
}

// Must be called with the PTE locked
bool
pte_try_dirty(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    if (pte->pte_inmem) {
        KASSERT(!pte->pte_dirty || pte->pte_cleaning);
        
        pte->pte_active = 1;
        pte->pte_dirty = 1;
        
        // update statistics
        vs_incr_ram_dirty();
        
        // if the page is currently being cleaned,
        // nullify the cleaning
        pte->pte_cleaning = 0;
        return true;
    }
    return false;
}

// Must be called with the PTE locked
bool
pte_refresh(vaddr_t vaddr, struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    
    // reset the accesssed bit to 0 and return
    // the old one
    bool accessed = pte->pte_active;
    pte->pte_active = 0;
    
    // invalidate TLBs if necessary
    if (accessed) {
        tlb_invalidate(vaddr, pte);
        struct tlbshootdown ts = {
            .ts_type = TS_INVAL,
            .ts_vaddr = vaddr,
            .ts_pte = pte
        };
        ipi_tlbbroadcast(&ts);
    }
    
    return accessed;
}

// Must be called with the PTE locked
bool
pte_resident(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    return pte->pte_inmem;
}

// Must be called with the PTE locked
bool
pte_is_dirty(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    return pte->pte_dirty;
}



// Must be called with the PTE locked
void
pte_start_cleaning(vaddr_t vaddr, struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    KASSERT(pte->pte_inmem);
    
    // set the cleaning bit
    pte->pte_cleaning = 1;
    
    // clean TLBs
    tlb_clean(vaddr, pte);
    struct tlbshootdown ts = {
        .ts_type = TS_CLEAN,
        .ts_vaddr = vaddr,
        .ts_pte = pte
    };
    ipi_tlbbroadcast(&ts);
}

// Must be called with the PTE locked
void
pte_finish_cleaning(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    KASSERT(pte->pte_inmem);
    
    // clear dirty bit only if cleaning was uninterrupted
    if (pte->pte_cleaning) {
        pte->pte_dirty = 0;
        // update statistics
        vs_decr_ram_dirty();
    }
}

// redirect the PTE to its swap block
// Must be called with the PTE locked
void
pte_evict(struct pt_entry *pte, swapidx_t swapblk)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    
    pte->pte_inmem = 0;
    pte->pte_swapblk = swapblk;
}

// Must be called with the PTE locked and the page in swap
swapidx_t
pte_start_swapin(struct pt_entry *pte, paddr_t frame)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    KASSERT(!pte->pte_inmem);
    
    // save the swap block
    swapidx_t swapblk = pte->pte_swapblk;
    
    pte->pte_inmem = 1;
    pte->pte_dirty = 0;
    pte->pte_cleaning = 0;
    pte->pte_swapin = 1;
    pte->pte_frame = PAGE_NUM(frame);
    
    return swapblk;
}

// call this after paging in
// Must be called with the PTE locked
void
pte_finish_swapin(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    
    pte->pte_swapin = 0;
}

