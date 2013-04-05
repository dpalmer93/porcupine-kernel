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

#if 0
#define KHEAP_MAXPAGES 1024

// The kernel page table is very simple.  It is just an array
// of physical addresses.  So kvm_pt[vpn] = frame_paddr
static paddr_t kvm_pt[KHEAP_MAXPAGES];
static spinlock kvm_lock = SPINLOCK_INITIALIZER;
// Number of pages used so far: for synchronizing
// kernel allocations.
static size_t kvm_heaptop;

paddr_t
kvm_translate(vaddr_t vaddr)
{
    spinlock_acquire(kvm_lock);
    paddr_t frame = kvm_pt[PAGE_NUM(vaddr)];
    spinlock_release(kvm_lock);
    
    if (frame == 0)
        return 0;
    
    return frame + PAGE_OFFSET(vaddr);
}

bool
kvm_validate(vaddr_t vaddr)
{
    bool valid;
    
    spinlock_acquire(kvm_lock);
    valid = ((vaddr - MIPS_KSEG2) / PAGE_SIZE) < kvm_heaptop;
    spinlock_release(kvm_lock);
    
    return valid;
}
#endif

vaddr_t
alloc_kpages(int npages)
{
    // this interface is strictly to be used for single pages
    KASSERT(npages == 1);
    
    // get a physical page.
    paddr_t frame = core_acquire_frame();
    if (frame == 0)
        return 0;
    
    // reserve it for the kernel and unlock it
    core_reserve_frame(frame);
    core_release_frame(frame);
    
    return PADDR_TO_KVADDR(frame);
}

void
free_kpages(vaddr_t vaddr)
{
    core_free_frame(KVADDR_TO_PADDR(vaddr));
}
