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
#include <machine/tlb.h>
#include <kern/errno.h>
#include <lib.h>
#include <page_table.h>
#include <coremem.h>
#include <swap.h>
#include <vmstat.h>
#include <vm.h>

// Handle a page fault in the case in which the virtual
// page is unmapped.  The PTE is already locked.
int
vm_unmapped_page_fault(vaddr_t faultaddress, struct page_table *pt)
{
    int err;
    
    // find a free page frame
    paddr_t frame = core_acquire_frame();
    if (frame == 0)
        return ENOMEM;
    
    // create a page table entry
    struct pt_entry *pte = pt_create_entry(pt, faultaddress, frame);
    if (pte == NULL) {
        core_release_frame(frame);
        return ENOMEM;
    }
    
    // get a swap block
    swapidx_t swapblk;
    err = swap_get_free(&swapblk);
    if (err) {
        pt_destroy_entry(pt, faultaddress);
        core_release_frame(frame);
        return err;
    }
    
    // zero the frame
    bzero((void *)PADDR_TO_KVADDR(frame), PAGE_SIZE);
    
    // update the core map
    core_map_frame(frame, faultaddress & PAGE_FRAME, pte, swapblk);
    core_release_frame(frame);
    
    // clean up
    pte_unlock(pte);
    return 0;
}

// Handle a page fault in the case in which the page has
// been swapped out.  The PTE is already locked.
int
vm_swapin_page_fault(vaddr_t faultaddress, struct pt_entry *pte)
{
    // find a free page frame
    paddr_t frame = core_acquire_frame();
    if (frame == 0)
        return ENOMEM;
    
    // Alert others that this PTE is being swapped in
    // and get the page's swap block.
    swapidx_t swapblk = pte_start_swapin(pte, frame);
    
    // swap in the page
    int err = swap_in(swapblk, frame);
    if (err) {
        core_release_frame(frame);
        pte_evict(pte, swapblk);
        pte_unlock(pte);
        return err;
    }
    
    // update the core map
    core_map_frame(frame, faultaddress & PAGE_FRAME, pte, swapblk);
    core_release_frame(frame);
    
    // clean up
    pte_finish_swapin(pte);
    pte_unlock(pte);
    return 0;
}

// Handle a copy-on-write fault.  The old PTE is already locked.
int
vm_copyonwrite_fault(vaddr_t faultaddress, struct page_table *pt)
{
    struct pt_entry *new_pte = pt_copyonwrite(pt, faultaddress);
    // Old pte is now unlocked
    
    if (new_pte == NULL) {
        // could not find any more physical
        // memory into which to copy the page
        return ENOMEM;
    }
    
    tlb_load_pte(faultaddress, new_pte);
    
    // update statistics
    vs_incr_cow_faults();
    
    pte_unlock(new_pte);
    return 0;
}
