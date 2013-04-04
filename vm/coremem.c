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
static size_t           core_start;
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
    coremap = (struct cm_entry *)PADDR_TO_KVADDR(ram_stealmem(cm_npages));
    if (coremap == MIPS_KSEG0) // ram_stealmem() should not return 0
        panic("Error during core map initialization!");
    
    // zero coremap
    bzero(coremap, cmsize);
    
    // reserve kernel pages for coremap
    for (int i = 0; i < cm_npages; i++)
    {
        coremap[i] = {
            .cme_kernel = 1,
            .cme_busy = 0,
            .cme_reserved = 0,
            .cme_swapblk = 0,
            .cme_resident = NULL
        };
    }
    
    // start LRU clock
    // do not bother clocking coremap pages
    core_start = cm_npages;
    core_lruclock = core_start;
}

paddr_t
core_acquire_frame(void)
{
    spinlock_acquire(core_lock);
    
    cm_entry *cme = NULL;
    while (cme == NULL)
    {
        // get an index uniformly distributed over the
        // usable part of the core map
        int idx = random() % core_len;
        if (idx < core_start)
            continue;
        
        
    }
    
    spinlock_release(core_lock);
}

paddr_t
core_map_frame(paddr_t frame, struct pt_entry *pte, blkcnt_t swapblk)
{
    spinlock_acquire(core_lock);
    coremap[PAGE_NUMBER(frame)] = {
        .cme_kernel = 0,
        .cme_busy = 0,
        .cme_reserved = 0,
        .cme_swapblk = swapblk,
        .cme_resident = pte
    };
    spinlock_release(core_lock);
}

void
core_free_frame(paddr_t frame)
{
    
}
