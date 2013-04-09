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
#include <machine/vm.h>
#include <machine/tlb.h>
#include <lib.h>
#include <thread.h>
#include <swap.h>
#include <addrspace.h>
#include <coremem.h>

// Number of clock ticks before we just evict the next
// clean page
#define MAX_CLOCKTICKS 16

// Macro to go from coremap entry to physical address
#define CORE_TO_PADDR(i) (core_frame0 + i * PAGE_SIZE)
#define PADDR_TO_CORE(paddr) ((paddr - core_frame0) / PAGE_SIZE)


struct cm_entry {
    unsigned         cme_kernel:1;   // In use by kernel?
    unsigned         cme_busy:1;     // For synchronization
    unsigned         cme_to_free:1;  // Defer freeing a busy block
    unsigned         cme_swapblk:29; // Swap backing block
    vaddr_t          cme_vaddr;      // Resident virtual address
    struct pt_entry *cme_resident;   // Resident virtual page mapping
};

static struct cm_entry *coremap;
static struct spinlock  core_lock = SPINLOCK_INITIALIZER;
static size_t           core_lruclock;
static size_t           core_len;
paddr_t                 core_frame0;


/**************** BASIC PRIMITIVES ****************/

// increment clock; return old value
static
size_t
core_clocktick()
{
    spinlock_acquire(&core_lock);
    
    size_t lruclock = core_lruclock;
    
    // increment and wrap around
    core_lruclock++;
    if (core_lruclock == core_len)
        core_lruclock = 0;
    
    spinlock_release(&core_lock);
    return lruclock;
}

// actually frees a CME/frame.
// this only gets called from core_free_frame()
// and core_unlock(), which synchronize the freeing
static
void
cme_do_free(struct cm_entry *cme)
{
    
    // free the associated swap space
    if (!cme->cme_kernel)
        swap_free(cme->cme_swapblk);
    
    // clear the CME
    cme->cme_kernel = 0;
    cme->cme_to_free = 0;
    cme->cme_swapblk = 0;
    cme->cme_vaddr = 0;
    cme->cme_resident = NULL;
}

// helper function for cleaner daemon only
// not exported in coremem.h
static
bool
core_try_lock(size_t index)
{
    struct cm_entry *cme = &coremap[index];
    
    spinlock_acquire(&core_lock);
    if (cme->cme_busy) {
        spinlock_release(&core_lock);
        return false;
    }
    
    cme->cme_busy = 1;
    spinlock_release(&core_lock);
    return true;
}

static
void
core_unlock(size_t index)
{
    // frame must be locked before it can be unlocked
    KASSERT(coremap[index].cme_busy);
    
    spinlock_acquire(&core_lock);
    
    // if the deferred free bit is set, free the frame now
    if (coremap[index].cme_to_free)
        cme_do_free(&coremap[index]);
    
    coremap[index].cme_busy = 0;
    spinlock_release(&core_lock);
}

/**************************************************/

void
core_bootstrap(void)
{
    // get total physical memory
    paddr_t lo;
    paddr_t hi;
    ram_getsize(&lo, &hi);
    
    // page align lo to the next free page
    KASSERT(lo == (lo & PAGE_FRAME));
    core_frame0 = lo;
    
    // calculate size of coremap
    core_len = (hi - lo) / PAGE_SIZE;
    size_t cmsize = core_len * sizeof(struct cm_entry);
    size_t cm_npages = (cmsize + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // allocate space for coremap
    paddr_t cm_paddr = ram_stealmem(cm_npages);
    if (cm_paddr == 0)
        panic("core_bootstrap: Out of Memory.\n");
    
    coremap = (struct cm_entry *)PADDR_TO_KVADDR(cm_paddr);
    
    // zero coremap
    bzero(coremap, cmsize);
    
    // reserve kernel pages for coremap
    for (size_t i = 0; i < cm_npages; i++)
    {
        coremap[i].cme_kernel = 1;
    }
    
    // start LRU clock
    core_lruclock = 0;
}

paddr_t
core_acquire_random(void)
{
    // get an index uniformly distributed over the core map
    int i, i0;
    i = i0 = random() % core_len;
    
    // look for a free frame
    spinlock_acquire(&core_lock);
    while (coremap[i].cme_kernel || coremap[i].cme_busy || coremap[i].cme_resident) {
        i = (i + 1) % core_len;
        if (i == i0) {
            // FAILED...
            spinlock_release(&core_lock);
            return 0;
        }
            
    }
    coremap[i].cme_busy = 1;
    spinlock_release(&core_lock);
    return CORE_TO_PADDR(i);
}

// looks for empty frame with eviction
// one LRU clock hand that tries MAX_CLOCKTICKS times to
// get a "recently unused" frame
paddr_t
core_acquire_frame(void)
{
    int clock_steps = 0;
    size_t index;
    while(true) {
        // get current clock hand and increment clock
        index = core_clocktick();
        
        // try to lock the coremap entry
        if (core_try_lock(index)) {
            // ignore kernel-reserved pages
            if (coremap[index].cme_kernel) {
                core_unlock(index);
                continue;
            }
            
            struct pt_entry *pte = coremap[index].cme_resident;
            vaddr_t vaddr = coremap[index].cme_vaddr;
            
            // found a free frame.  Take it and return
            if (pte == NULL)
                break;
            
            // otherwise, try to lock the page table entry, skip if cannot acquire
            if (pte_try_lock(pte)) {
                // the PTE should be in memory, since it is using up a
                // physical page
                KASSERT(pte_resident(pte));
                
                // skip dirty pages
                if (pte_is_dirty(pte)) {
                    pte_unlock(pte);
                    core_unlock(index);
                    continue;
                }
                
                // If we have reached MAX_CLOCKTICKS,
                // just take the page regardless of how recently it has
                // been used. Otherwise, refresh the access bit and
                // invalidate TLBs to simulate hardware-managed accessed bit
                // (pte_refresh() does both)
                if (clock_steps < MAX_CLOCKTICKS && pte_refresh(vaddr, pte)) {
                    clock_steps++;
                    
                    // move on...
                    pte_unlock(pte);
                    core_unlock(index);
                    continue;
                }
                else { // found a frame that has not been recently accessed
                    // evict the PTE to its swap block
                    pte_evict(pte, coremap[index].cme_swapblk);
                    pte_unlock(pte);
                    break;
                }
            }
            core_unlock(index);
        }
    }
    
    return CORE_TO_PADDR(index);
}

void
core_release_frame(paddr_t frame)
{
    core_unlock(PADDR_TO_CORE(frame));
}

void
core_map_frame(paddr_t frame, vaddr_t vaddr, struct pt_entry *pte, swapidx_t swapblk)
{
    // get the CME
    struct cm_entry *cme = &coremap[PADDR_TO_CORE(frame)];
    
    // should hold the frame's lock first
    KASSERT(cme->cme_busy);
    
    cme->cme_kernel = 0;
    cme->cme_busy = 1;
    cme->cme_swapblk = swapblk;
    cme->cme_vaddr = vaddr;
    cme->cme_resident = pte;
}

void
core_reserve_frame(paddr_t frame)
{
    // get the CME
    struct cm_entry *cme = &coremap[PADDR_TO_CORE(frame)];
    
    // should hold the frame's lock first
    KASSERT(cme->cme_busy);
    
    cme->cme_kernel = 1;
    cme->cme_busy = 1;
    cme->cme_swapblk = 0;
    cme->cme_vaddr = 0;
    cme->cme_resident = NULL;
}

void
core_free_frame(paddr_t frame)
{
    spinlock_acquire(&core_lock);
    
    struct cm_entry *cme = &coremap[PADDR_TO_CORE(frame)];
    
    // if the CME is busy, defer freeing until core_unlock()
    if (cme->cme_busy) {
        cme->cme_to_free = 1;
        spinlock_release(&core_lock);
        return;
    }
    
    // Otherwise, just free the frame
    cme_do_free(cme);
    
    spinlock_release(&core_lock);
}

// Does not wait on PTE
// Does not hold PTE for long periods
static
void
core_clean(void *data1, unsigned long data2)
{
    (void)data1;
    (void)data2;
    
    size_t index = 0;
    while (true)
    {
        // try to lock both the CME and PTE
        // if it fails, go to the next cme
        if (core_try_lock(index)) {
            if(!(coremap[index].cme_kernel) && coremap[index].cme_resident) {
                if (pte_try_lock(coremap[index].cme_resident)) {
                    
                    struct pt_entry *pte = coremap[index].cme_resident;
                    vaddr_t vaddr = coremap[index].cme_vaddr;
                    
                    // if dirty, then start cleaning
                    if(pte_is_dirty(pte)) {
                        // set the cleaning bit, clean the TLBs, and unlock
                        pte_start_cleaning(vaddr, pte); // this cleans TLBs too
                        pte_unlock(pte);
                        
                        swap_out(CORE_TO_PADDR(index), coremap[index].cme_swapblk);
                        
                        // once done writing, lock the PTE and check the cleaning bit
                        // if it is intact, no writes to this page have intervened
                        // in our cleaning: the clean was successful, and we can clear
                        // the dirty bit
                        if (pte_try_lock(pte)) {
                            pte_finish_cleaning(pte);
                            pte_unlock(pte);
                        }
                    }
                    else
                        pte_unlock(pte);
                }
            }
            core_unlock(index);
        }
        index = (index + 1) % core_len;
        thread_yield();
    }
}

void core_cleaner_bootstrap(void)
{
    thread_fork("Core Cleaner", core_clean, NULL, 0, NULL);
}


