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
#include <vmstat.h>
#include <page_table.h>

#define LEVEL_SIZE 1024
#define INDEX_TO_VADDR(l1, l2) (((l1) << 22) + ((l2) << 12))
#define L1_INDEX(va) (PAGE_NUM(va) >> 10) // index into the level 1 table
#define L2_INDEX(va) (PAGE_NUM(va) & (LEVEL_SIZE - 1)) // index into the level 2 table

static void pte_destroy(struct pt_entry *pte);

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
        
    // Loop through the old page table and copy entries
    for (int i = 0; i < LEVEL_SIZE; i++) {
        if (old_pt->pt_index[i] != NULL) {
            for (int j = 0; j < LEVEL_SIZE; j++) {
                if (old_pt->pt_index[i][j] != NULL) {
                
                    struct pt_entry *old_pte = pt_acquire_entry(old_pt, INDEX_TO_VADDR(i, j));
                    struct pt_entry *new_pte = pt_create_entry(new_pt, INDEX_TO_VADDR(i, j), 0);
                    
                    if (new_pte == NULL) {
                        pt_release_entry(old_pt, old_pte);
                        pt_destroy(new_pt);
                        return NULL;
                    }
                    
                    // Copy the new_pte's page to a new place in swap
                    swapidx_t freeblk;
                    if(swap_get_free(&freeblk)) {
                        pt_release_entry(old_pt, old_pte);
                        pt_destroy(new_pt);
                        return NULL;
                    }    
                    if (old_pte->pte_inmem) {
                        if(swap_out(MAKE_ADDR(old_pte->pte_frame, 0), freeblk)) {
                            swap_free(freeblk);
                            pt_release_entry(old_pt, old_pte);
                            pt_destroy(new_pt);
                            return NULL;
                        }
                    }
                    else {
                        if(swap_copy(old_pte->pte_swapblk, freeblk)) {
                            swap_free(freeblk);
                            pt_release_entry(old_pt, old_pte);
                            pt_destroy(new_pt);
                            return NULL;
                        }
                    }
                    
                    // put the correct info into new_pte
                    new_pte->pte_busy = 0;
                    new_pte->pte_inmem = 0;
                    new_pte->pte_swapblk = freeblk;
                    
                    pt_release_entry(old_pt, old_pte);
                }
            }
        }
    }       
            
    return new_pt;
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
    
    // spin until the PTE becomes available
    // PTEs are only held for short times
    while (!pte_try_lock(pte));
    
    return pte;
}

void
pt_release_entry(struct page_table *pt, struct pt_entry *pte)
{
    // unused (for now)
    (void)pt;
    
    KASSERT(pte->pte_busy);
    pte_unlock(pte);
}

// The created entry is locked.  It must be unlocked with
// pt_release_entry() when the operations on it are complete
struct pt_entry *
pt_create_entry(struct page_table *pt, vaddr_t vaddr, paddr_t paddr)
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
    pte->pte_active = 0;
    pte->pte_dirty = 0;
    pte->pte_cleaning = 0;
    pte->pte_reserved = 0;
    pte->pte_frame = PAGE_NUM(paddr);
    
    // save our new structures
    l2_tbl[l2_idx] = pte;
    pt->pt_index[l1_idx] = l2_tbl;
    
    return pte;
}

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

static
void
pte_destroy(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    
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

// call this after paging in
// Must be called with the PTE locked
void
pte_map(struct pt_entry *pte, paddr_t frame)
{
    pte->pte_inmem = 1;
    pte->pte_active = 0;
    pte->pte_dirty = 0;
    pte->pte_cleaning = 0;
    pte->pte_reserved = 0;
    pte->pte_frame = PAGE_NUM(frame);
}

