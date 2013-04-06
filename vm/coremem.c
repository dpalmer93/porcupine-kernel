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
#include <swap.h>
#include <addrspace.h>
#include <coremem.h>

// Number of access bits the lru_clock will clear before it just evicts the not busy next page
#define MAX_CLOCKSTEPS 16

// Macro to go from coremap entry to physical address
#define CORE_TO_PADDR(i) (core_btmaddr + i * PAGE_SIZE)
#define PADDR_TO_CORE(paddr) ((paddr - core_btmaddr) / PAGE_SIZE)


struct cm_entry {
    unsigned         cme_kernel:1;   // In use by kernel?
    unsigned         cme_busy:1;     // For synchronization
    unsigned         cme_swapblk:30; // Swap backing block
    struct pt_entry *cme_resident;   // Resident virtual page mapping
};

static struct cm_entry *coremap;
static struct spinlock  core_lock = SPINLOCK_INITIALIZER;
static size_t           core_lruclock;
static size_t           core_len;
paddr_t                 core_btmaddr;

void
core_bootstrap(void)
{
    // get total physical memory
    paddr_t lo;
    paddr_t hi;
    ram_getsize(&lo, &hi);
    
    // page align lo to the next free page
    KASSERT(lo != 0);
    lo = PAGE_FRAME(lo + PAGE_SIZE - 1);
    core_btmaddr = lo;
    
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
    for (int i = 0; i < cm_npages; i++)
    {
        coremap[i] = {
            .cme_kernel = 1,
            .cme_busy = 0,
            .cme_swapblk = 0,
            .cme_resident = NULL
        };
    }
    
    // start LRU clock
    core_lruclock = cm_npages;
}

paddr_t
core_acquire_frame_random(void)
{
    // get an index uniformly distributed over the core map
    int i = i0 = random() % core_len;
    
    // look for a free frame
    spinlock_acquire(core_lock);
    while (coremap[i].cme_kernel || coremap[i].cme_busy || coremap[i].cme_resident) {
        i = (i + 1) % core_len;
        if (i == i0) {
            // FAILED...
            spinlock_release(core_lock);
            return 0;
        }
            
    }
    coremap[i].cme_busy = 1;
    spinlock_release(core_lock);
    return CORE_TO_PADDR(i);
}

// looks for empty frame with eviction
// one LRU clock hand that tries MAX_CLOCKSTEPS times to get an unaccessed frame
paddr_t
core_acquire_frame(void)
{
    int clock_steps = 0;
    size_t lruclock;
    while(true) {
    
        // get current clock hand and increment clock
        lruclock = core_incr_lruclock();
        
        // try to lock the coremap entry
        if (core_try_lock(lruclock)) {
            // found unallocated coremap entry
            if (!(coremap[lruclock].cme_kernel) && !(coremap[lruclock].cme_resident)) {
                break;
            }
            // try to lock page table entry, skip if cannot acquire
            if (pte_try_lock(coremap[lruclock].cme_resident)) {
                struct pt_entry *pte = coremap[lruclock].cme_resident;
                
                //skip entries that are dirty or not in memory
                if (pte_is_dirty(pte) || !pte_is_inmem(pte)) {
                    core_release_lock(lruclock);
                    pte_release_lock(pte);
                    continue;
                }
                
                // frame has been accessed recently
                if (clock_steps < MAX_CLOCKSTEPS && pte->pte_accessed) {
                    clock_steps++;
                    // need to invalidate other PTE's still
                    tlb_invalidate_p(CORE_TO_PADDR(lruclock));
                    // clear accessed bit and release PTE
                    pte_clear_access(pte);
                    core_release_lock(lruclock);
                    pte_release_lock(pte);
                    continue;
                }
                // found a frame that has not been recently accessed
                else {
                    break;
                }
            }      
        }
    }
    
    return CORE_TO_PADDR(lruclock);
}

void
core_release_frame(paddr_t frame)
{
    // frame must be locked before it can be unlocked
    KASSERT(coremap[PADDR_TO_CORE(frame)].cme_busy);
    
    spinlock_acquire(core_lock);
    coremap[PADDR_TO_CORE(frame)].cme_busy = 0;
    spinlock_release(core_lock);
}

void
core_map_frame(paddr_t frame, struct pt_entry *pte, swapidx_t swapblk)
{
    // should hold the frame's lock first
    KASSERT(coremap[PADDR_TO_CORE(frame)].cme_busy);
    
    coremap[PADDR_TO_CORE(frame)] = {
        .cme_kernel = 0,
        .cme_busy = 1,
        .cme_swapblk = swapblk,
        .cme_resident = pte
    };
}

void
core_reserve_frame(paddr_t frame)
{
    // should hold the frame's lock first
    KASSERT(coremap[PADDR_TO_CORE(frame)].cme_busy);
    
    coremap[PADDR_TO_CORE(frame)] = {
        .cme_kernel = 1,
        .cme_busy = 1,
        .cme_swapblk = 0,
        .cme_resident = NULL
    };
}

void
core_free_frame(paddr_t frame)
{
    spinlock_acquire(core_lock);
    
    struct cm_entry *cme = coremap[PADDR_TO_CORE(frame)];
    
    // free the associated swap space
    if (!cme.cme_kernel)
        swap_free(cme->cme_swapblk);
    
    // clear the CME
    cme->cme_kernel = 0;
    cme->cme_busy = 0;
    cme->cme_swapblk = 0;
    cme->cme_resident = NULL;
    
    spinlock_release(core_lock);
}

// increments clock, returns old value
size_t
core_incr_lruclock()
{
    spinlock_acquire(core_lock);
    size_t lruclock = core_lruclock;
    core_lruclock++;
    spinlock_release(core_lock);
    return lruclock;
}

// helper function for cleaner daemon only
// not exported in coremem.h
bool
core_try_lock(size_t pgnum)
{
    struct cm_entry *cme = coremap[pgnum];
    
    spinlock_acquire(core_lock);
    if (cme->cme_busy) {
        spinlock_release(core_lock);
        return false;
    }
    
    cme->cme_busy = 1;
    spinlock_release(core_lock);
    return true;
}

void
core_release_lock(size_t pgnum)
{
    KASSERT(coremap[pgnum].cme_busy == 1);
    coremap[pgnum].cme_busy == 0;
}

// Does not wait on PTE
// Does this by test and setting once
void
core_clean(void *data1, unsigned long data2)
{
    (void)data1;
    (void)data2;
    
    size_t pgnum;
    while (true)
    {
        // tries to lock both the cme and pte
        // if it fails, go to the next cme
        if (core_try_lock(pgnum) && pte_try_lock(coremap[pgnum].cme_resident) 
            && coremap[pgnum].cme_resident->pte_dirty) {
            
        
        
            coremap[pgnum].cme_resident->pte_busy = 0;
            core_release_frame(CORE_TO_PADDR(pgnum));
        }
        
        index = (index + 1) % cm_npages;
    }
}


