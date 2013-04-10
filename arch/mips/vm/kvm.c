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
#include <mips/tlb.h>
#include <kern/errno.h>
#include <lib.h>
#include <spinlock.h>
#include <coremem.h>
#include <vmstat.h>
#include <kvm.h>

#define KHEAP_MAXPAGES 1024

struct kvm_pte {
    unsigned kte_frame:20;      // physical page number
    unsigned kte_reserved:10;   // unused for now
    unsigned kte_term:1;        // end of an extent?
    unsigned kte_used:1;        // currently used
};

// The kernel page table is very simple.  It is just an array
// of physical addresses.  Since these are always aligned,
// we use the bottom two bits for additional state.
static struct kvm_pte   kvm_pt[KHEAP_MAXPAGES];
static struct spinlock  kvm_lock = SPINLOCK_INITIALIZER;
static int              kvm_index = 0; // search index

vaddr_t
kvm_alloc_contig(int npages)
{
    spinlock_acquire(&kvm_lock);
    
    // get a contiguous block of pages in KSEG2
    int kvm_index0 = kvm_index;
    int ncontig = 0;
    while (ncontig < npages) {
        if (kvm_pt[kvm_index].kte_used)
            ncontig = 0;
        else
            ncontig++;
        
        kvm_index++;
        if (kvm_index == KHEAP_MAXPAGES) {
            kvm_index = 0;
            ncontig = 0;
        }
        
        // if we search the whole table without finding
        // a contiguous block of npages, return 0
        if (kvm_index == kvm_index0) {
            spinlock_release(&kvm_lock);
            return 0;
        }
    }
    
    // get the start index of the contiguous block
    int start = kvm_index - ncontig;
    
    // mark the pages as in use
    for (int i = start; i < kvm_index; i++)
    {
        // initialize the kernel page table entry
        kvm_pt[i].kte_frame = 0;
        kvm_pt[i].kte_reserved = 0;
        kvm_pt[i].kte_term = 0;
        kvm_pt[i].kte_used = 1;
    }
    
    kvm_pt[kvm_index - 1].kte_term = 1;
    
    spinlock_release(&kvm_lock);
    
    // return the block
    return start * PAGE_SIZE + MIPS_KSEG2;
}

void
kvm_free_contig(vaddr_t vaddr)
{
    KASSERT(vaddr >= MIPS_KSEG2);
    int index = (vaddr - MIPS_KSEG2) / PAGE_SIZE;
    
    while (true)
    {
        if (kvm_pt[index].kte_frame) {
            // free the frame
            core_free_frame(MAKE_ADDR(kvm_pt[index].kte_frame, 0));
        }
        
        // clear the kernel page table entry
        kvm_pt[index].kte_frame = 0;
        kvm_pt[index].kte_reserved = 0;
        kvm_pt[index].kte_used = 0;
        
        if(kvm_pt[index].kte_term) {
            kvm_pt[index].kte_term = 0;
            return;
        }
        
        index++;
    }
}

bool
kvm_managed(vaddr_t vaddr)
{
    return vaddr >= MIPS_KSEG2;
}

static
int
kvm_page_fault(struct kvm_pte *kte)
{
    paddr_t frame = core_acquire_frame();
    if (frame == 0)
        return ENOMEM;
    
    kte->kte_frame = PAGE_NUM(frame);
    core_reserve_frame(frame);
    core_release_frame(frame);
    return 0;
}

int
kvm_fault(vaddr_t faultaddress)
{
    struct kvm_pte *kte = &kvm_pt[PAGE_NUM(faultaddress - MIPS_KSEG2)];
    
    if (!kte->kte_used)
        return EFAULT;
    
    if (!kte->kte_frame) {
        // Kernel page fault
        return kvm_page_fault(kte);
    }
    
    // load the mapping into the TLB
    tlb_load(faultaddress, MAKE_ADDR(kte->kte_frame, 0), true);
    return 0;
}
