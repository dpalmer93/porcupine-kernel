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
#include <addrspace.h>
#include <lib.h>
#include <coremem.h>


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

void
core_bootstrap(void)
{
    // get total physical memory
    paddr_t lo;
    paddr_t hi;
    ram_getsize(&lo, &hi);
    
    // calculate size of coremap
    core_len = (hi - lo) / PAGE_SIZE;
    size_t cmsize = core_len * sizeof(struct cm_entry);
    size_t cm_npages = (cmsize + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // allocate space for coremap
    paddr_t cm_paddr = ram_stealmem(cm_npages);
    if (cm_paddr == 0)
        panic("Error during core map initialization!");
    
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
core_acquire_frame(void)
{
    // get an index uniformly distributed over the core map
    int i = i0 = random() % core_len;
    
    // look for a free frame
    spinlock_acquire(core_lock);
    while (coremap[i].cme_kernel || coremap[i].cme_busy || coremap[i].cme_resident) {
        i++;
        if (i == core_len)
            i = 0;
        if (i == i0) {
            // FAILED...
            spinlock_release(core_lock);
            return 0;
        }
            
    }
    coremap[i].cme_busy = 1;
    spinlock_release(core_lock);
}

void
core_map_frame(paddr_t frame, struct pt_entry *pte, blkcnt_t swapblk)
{
    // should hold the frame's lock first
    KASSERT(coremap[PAGE_NUMBER(frame)].cme_busy);
    coremap[PAGE_NUMBER(frame)] = {
        .cme_kernel = 0,
        .cme_busy = 1,
        .cme_swapblk = swapblk,
        .cme_resident = pte
    };
    
    // release the lock
    coremap[i].cme_busy = 0;
}

void
core_reserve_frame(paddr_t frame)
{
    // should hold the frame's lock first
    KASSERT(coremap[PAGE_NUMBER(frame)].cme_busy);
    coremap[i] = {
        .cme_kernel = 1,
        .cme_busy = 1,
        .cme_swapblk = 0,
        .cme_resident = NULL
    };
    
    // release the lock
    coremap[i].cme_busy = 0;
}

void
core_free_frame(paddr_t frame)
{
    spinlock_acquire(core_lock);
    coremap[PAGE_NUMBER(frame)] = {
        .cme_kernel = 0,
        .cme_busy = 0,
        .cme_swapblk = 0,
        .cme_resident = NULL
    };
    spinlock_release(core_lock);
}
