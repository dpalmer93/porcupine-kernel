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
#include <wchan.h>
#include <swap.h>
#include <addrspace.h>
#include <vmstat.h>
#include <coremem.h>

#include "opt-oneclock.h"
#include "opt-twoclock.h"

// Number of dirty pages at which we wake the cleaner thread
#define MAX_DIRTY (core_len/2)
// Number of dirty pages at which the cleaner thread sleeps
#define MIN_DIRTY (core_len/8)

// Macro to go from coremap entry to physical address
#define CORE_TO_PADDR(i) (core_frame0 + i * PAGE_SIZE)
#define PADDR_TO_CORE(paddr) ((paddr - core_frame0) / PAGE_SIZE)

// Number of pages that the 2nd clockhand trails behind the 1st
#define CLOCK_OFFSET 128

// options for clock hands touching active pages
#define ACTIVE_IGNORE   0   // ignore activity level
#define ACTIVE_SKIP     1   // skip active pages
#define ACTIVE_REFRESH  2   // refresh active pages

struct cm_entry {
    unsigned         cme_kernel:1;   // In use by kernel?
    unsigned         cme_busy:1;     // For synchronization
    unsigned         cme_to_free:1;  // Defer freeing a busy block
    unsigned         cme_swapblk:24; // Swap backing block
    vaddr_t          cme_vaddr;      // Resident virtual address
    struct pt_entry *cme_resident;   // Resident virtual page mapping
};

static struct cm_entry *coremap;
static struct spinlock  core_lock = SPINLOCK_INITIALIZER;
static struct wchan    *core_cleaner_wchan;
static size_t           core_lruclock;
static size_t           core_len;
paddr_t                 core_frame0; // physical address of first managed frame


/**************** BASIC PRIMITIVES ****************/

// increment clock; return old value
#if OPT_ONECLOCK || OPT_TWOCLOCK
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
#endif

// actually frees a CME/frame.
// this only gets called from core_free_frame()
// and cme_unlock(), which synchronize the freeing
static
void
cme_do_free(struct cm_entry *cme)
{
    // free the associated swap space
    if (!cme->cme_kernel)
        swap_free(cme->cme_swapblk);
    else // update stats
        vs_decr_ram_wired();
    vs_incr_ram_free();
    
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
cme_try_lock(size_t index)
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
cme_unlock(size_t index)
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

// tries to clean a single frame...
// Caller must have both the CME and PTE
// already locked.  If the function returns true,
// both remain locked, so that further action
// may be taken with the frame.  Otherwise, the
// PTE is unlocked.
static bool
cme_try_clean(size_t index)
{
    struct cm_entry *cme = &coremap[index];
    struct pt_entry *pte = cme->cme_resident;
    vaddr_t vaddr = cme->cme_vaddr;
    
    // set the cleaning bit, clean the TLBs, and unlock
    pte_start_cleaning(vaddr, pte); // this cleans TLBs too
    pte_unlock(pte);
    
    if (swap_out(CORE_TO_PADDR(index), cme->cme_swapblk))
        return false;
    
    // once done writing, lock the PTE and check the cleaning bit
    // if it is intact, no writes to this page have intervened
    // in our cleaning: the clean was successful, and we can clear
    // the dirty bit
    if (pte_try_lock(pte)) {
        if (pte_finish_cleaning(pte))
            return true;
        pte_unlock(pte);
    }
    return false;
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
    
    // set up stats
    vs_init_ram(hi / PAGE_SIZE, cm_npages + lo / PAGE_SIZE);
}

// This gets called from each of the core_acquire functions.
// It checks a frame for suitability.  The CME must be
// locked before core_clockhand is called, and is locked
// upon return
static
bool
core_clockhand(size_t index, int on_active)
{
    // ignore kernel-reserved pages
    if (coremap[index].cme_kernel)
        return false;
    
    struct pt_entry *pte = coremap[index].cme_resident;
    vaddr_t vaddr = coremap[index].cme_vaddr;
    
    // found a free frame.
    if (pte == NULL) {
        KASSERT(vaddr == 0);
        KASSERT(coremap[index].cme_swapblk == 0);
        return true;
    }
    
    // otherwise, try to lock the page table entry, skip if cannot acquire
    if (pte_try_lock(pte)) {
        // the PTE should be in memory, since it is using up a
        // physical page
        KASSERT(pte_resident(pte));
        
        if (pte_is_dirty(pte)) {
            // skip dirty pages if there are relatively few of them
            // else try to clean them
            if (vs_get_ram_dirty() < MAX_DIRTY) {
                pte_unlock(pte);
                return false;
            }
            else if (!cme_try_clean(index)) {
                // no need to unlock the PTE: it is already unlocked
                return false;
            }
        }
        
        // Refresh the active bit and invalidate TLBs to simulate
        // hardware-managed active bit (pte_refresh() does both)
        if (pte_is_active(pte)) {
            switch(on_active) {
                case ACTIVE_IGNORE: // take the frame anyway
                    // invalidate any TLB entries
                    pte_refresh(vaddr, pte);
                    break;
                case ACTIVE_REFRESH: // refresh the PTE/TLB and move on
                    pte_refresh(vaddr, pte);
                case ACTIVE_SKIP: // just move on
                    pte_unlock(pte);
                    return false;
            }
        }
        
        // found a frame that has not been recently accessed
        // re-map the PTE to its swap block
        pte_evict(pte, coremap[index].cme_swapblk);
        pte_unlock(pte);
        
        // mark the CME as free and update stats
        coremap[index].cme_swapblk = 0;
        coremap[index].cme_vaddr = 0;
        coremap[index].cme_resident = NULL;
        
        // update stats
        vs_decr_ram_inactive();
        vs_incr_ram_free();
        
        return true;
    }
    return false;
}

#if OPT_ONECLOCK
// Looks for empty frame with eviction:
// one LRU clock hand that gets a "recently unused" frame.
// On return, the CME is locked
static
paddr_t
core_acquire_oneclock(void)
{
    while(true) {
        // get current clock hand and increment clock
        size_t index = core_clocktick();
        
        // try to lock the coremap entry
        if (cme_try_lock(index)) {
            if (core_clockhand(index, ACTIVE_REFRESH))
                return CORE_TO_PADDR(index);
            cme_unlock(index);
        }
    }
}

#elif OPT_TWOCLOCK
// Looks for empty frame with eviction.
// two LRU clock hands: one refreshes active bits;
// the other looks for inactive frames.
// On return, the CME is locked
static
paddr_t
core_acquire_twoclock(void)
{
    while(true) {
        // get trailing (page-grabbing) clock hand and increment clock
        size_t trailing = core_clocktick();
        
        // calculate the leading clock hand
        size_t leading = (trailing + CLOCK_OFFSET) % core_len;
        // mark the PTE the second clock hand is pointing at as not recently used
        if (cme_try_lock(leading)) {
            if (!coremap[leading].cme_kernel) {
                struct pt_entry *pte = coremap[leading].cme_resident;
                vaddr_t vaddr = coremap[leading].cme_vaddr;
                
                if (pte && pte_try_lock(pte)) {
                    pte_refresh(vaddr, pte);
                    pte_unlock(pte);
                }
            }
            cme_unlock(leading);
        }                    
    
        // run the trailing hand
        if (cme_try_lock(trailing)) {
            if (core_clockhand(trailing, ACTIVE_SKIP))
                return CORE_TO_PADDR(trailing);
            cme_unlock(trailing);
        }
    }
}

#else
// randomly choose pages until it finds a page
// that is not busy, kernel-allocated, or dirty
static
paddr_t
core_acquire_random(void)
{
    // if the random device is initialized, then
    // start at an index uniformly distributed over the core map
    // otherwise start at 0
    int index = is_random_init()? (random() % core_len) : 0;
        
    while(true) {
        // try to lock the coremap entry
        if (cme_try_lock(index)) {
            if (core_clockhand(index, ACTIVE_IGNORE))
                return CORE_TO_PADDR(index);
            cme_unlock(index);
        }
        
        // move on
        index = (index + 1) % core_len;
    }
}
#endif

paddr_t
core_acquire_frame(void)
{
    // wake up the cleaner thread if necessary
    if (vs_get_ram_dirty() >= MAX_DIRTY) {
        wchan_wakeone(core_cleaner_wchan);
    }
    
#if OPT_ONECLOCK
    return core_acquire_oneclock();
#elif OPT_TWOCLOCK
    return core_acquire_twoclock();
#else
    return core_acquire_random();
#endif
}

void
core_release_frame(paddr_t frame)
{
    cme_unlock(PADDR_TO_CORE(frame));
}

void
core_map_frame(paddr_t frame, vaddr_t vaddr, struct pt_entry *pte, swapidx_t swapblk)
{
    // get the CME
    struct cm_entry *cme = &coremap[PADDR_TO_CORE(frame)];
    
    // should hold the frame's lock first
    KASSERT(cme->cme_busy);
    
    cme->cme_kernel = 0;
    cme->cme_swapblk = swapblk;
    cme->cme_vaddr = vaddr;
    cme->cme_resident = pte;
    
    // update stats
    vs_decr_ram_free();
    vs_incr_ram_inactive();
}

void
core_reserve_frame(paddr_t frame)
{
    // get the CME
    struct cm_entry *cme = &coremap[PADDR_TO_CORE(frame)];
    
    // should hold the frame's lock first
    KASSERT(cme->cme_busy);
    
    cme->cme_kernel = 1;
    cme->cme_swapblk = 0;
    cme->cme_vaddr = 0;
    cme->cme_resident = NULL;
    
    // update stats
    vs_decr_ram_free();
    vs_incr_ram_wired();
}

void
core_free_frame(paddr_t frame)
{
    spinlock_acquire(&core_lock);
    
    struct cm_entry *cme = &coremap[PADDR_TO_CORE(frame)];
    
    // if the CME is busy, defer freeing until cme_unlock()
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
        struct cm_entry *cme = &coremap[index];
        // check the CME first to reduce contention and increase throughput
        if(!(cme->cme_busy) && !(cme->cme_kernel) && cme->cme_resident) {
            // try to lock both the CME and PTE
            // if it fails, go to the next cme
            if (cme_try_lock(index)) {
                if (!(cme->cme_kernel)  // check conditions again to ensure nothing
                && cme->cme_resident    // changed while we were getting the lock
                && pte_try_lock(cme->cme_resident)) {
                    // if dirty, then start cleaning
                    if(pte_is_dirty(cme->cme_resident)) {
                        if (cme_try_clean(index))
                            pte_unlock(cme->cme_resident);
                        // If the cleaning failed, the PTE is already
                        // unlocked.
                    }
                    else
                        pte_unlock(cme->cme_resident);
                }
                cme_unlock(index);
            }
        }
        
        // move on
        index = (index + 1) % core_len;

        // go to sleep if cleaning is unneeded
        if (vs_get_ram_dirty() <= MIN_DIRTY) {
            wchan_lock(core_cleaner_wchan);
            wchan_sleep(core_cleaner_wchan);
        }
    }
}

void core_cleaner_bootstrap(void)
{
    core_cleaner_wchan = wchan_create("Core Cleaner Wait Channel");
    thread_fork("Core Cleaner", core_clean, NULL, 0, NULL);
}


