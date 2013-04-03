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

#include <machine/vm.h>
#include <spinlock.h>
#include <coremem.h>
#include <vm.h>

#define KHEAP_MAXPAGES 1024

static struct pt_entry kvm_page_table[KHEAP_MAXPAGES];
static spinlock kvm_lock = SPINLOCK_INITIALIZER;
// Number of pages used so far: for synchronizing
// kernel allocations.
static int kvm_heaptop;

struct pt_entry *
kvm_set(vaddr_t vaddr, paddr_t paddr)
{
    kvm_page_table[PAGE_NUM(vaddr)] = {
        .pte_busy = 0,
        .pte_write = 1,
        .pte_inmem = 1,
        .pte_term = 0,
        .pte_accessed = 1,
        .pte_dirty = 1,
        .pte_reserved = 0,
        .pte_frame = PAGE_NUM(paddr)
    };
    return &kvm_page_table[PAGE_NUM(vaddr)];
}

vaddr_t
alloc_kpages(int npages)
{
    vaddr_t block;
    
    // get a block of memory in kernel virtual memory
    spinlock_acquire(kvm_lock);
    if (npages + kvm_heaptop > KHEAP_MAXPAGES) {
        spinlock_release(kvm_lock);
        return 0;
    }
    block = kvm_heaptop * PAGE_SIZE + MIPS_KSEG2;
    kvm_heaptop += npages;
    spinlock_release(kvm_lock);
    
    // get physical pages and map them into kseg2
    for (int i = 0; i < npages; i++)
    {
        vaddr_t page = block + i * PAGE_SIZE;
        paddr_t frame = core_acquire_frame(); // acquire the frame
        struct pt_entry *pte = kvm_set(page, frame);
        core_map_frame(frame, pte, 0); // use the frame
    }
}

void
free_kpages(vaddr_t addr)
{
    // FIXME!
}
