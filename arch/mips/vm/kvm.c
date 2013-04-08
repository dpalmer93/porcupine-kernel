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
 *
 * Kernel Virtual Memory
 */

#include <mips/vm.h>
#include <spinlock.h>
#include <coremem.h>

#define KHEAP_MAXPAGES 1024

struct kvm_pte {
    unsigned kte_frame:20;      // physical page number
    unsigned kte_reserved:10;   // unused for now
    unsigned kte_term:1;        // end of an extent?
    unsigned kte_mapped:1;      // currently used
};

// The kernel page table is very simple.  It is just an array
// of physical addresses.  Since these are always aligned,
// we use the bottom two bits for additional state.
static kvm_pte  kvm_pt[KHEAP_MAXPAGES];
static spinlock kvm_lock = SPINLOCK_INITIALIZER;
static size_t   kvm_top = 0; // kernel heap upper bound

vaddr_t
kvm_alloc_contig(int npages)
{
    size_t block;
    
    // get a contiguous block of pages in KSEG2
    spinlock_acquire(&kvm_lock);
    block = kvm_top;
    kvm_top += npages;
    if (kvm_top > KHEAP_MAXPAGES) {
        spinlock_release(&kvm_lock);
        return 0;
    }
    spinlock_release(&kvm_lock);
    
    // map the pages
    for (int i = 0; i < npages; i++)
    {
        // get a physical page
        paddr_t frame = core_acquire_frame();
        
        // initialize the kernel page table entry
        kvm_pt[block + i].kte_frame = PAGE_NUM(frame);
        kvm_pt[block + i].kte_reserved = 0;
        kvm_pt[block + i].kte_term = 0;
        kvm_pt[block + i].kte_mapped = 1;
        
        // reserve the frame for the kernel
        core_reserve_frame(frame);
    }
    
    kvm_pt[block + npages - 1].kte_term = 1;
    
    // return the block
    return block * PAGE_SIZE + MIPS_KSEG2;
}

paddr_t
kvm_getframe(vaddr_t vaddr)
{
    struct kvm_pte *kte = &kvm_pt[PAGE_NUM(vaddr)];
    if (!kte->mapped)
        return 0;
    
    return MAKE_ADDR(kte->kte_frame, 0);
}
