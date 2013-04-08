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

#include <page_table.h>

// Handle a page fault in the case in which the virtual
// page is unmapped
int
vm_unmapped_page_fault(vaddr_t vaddr, struct page_table *pt)
{
    int err;
    
    // find a free page frame
    paddr_t frame = core_acquire_frame();
    if (frame == 0)
        return ENOMEM;
    
    // create a page table entry
    struct pt_entry *pte = pt_create_entry(pt, vaddr, paddr);
    if (pte == NULL) {
        core_release_frame(frame);
        return ENOMEM;
    }
    
    // get a swap block
    swapidx_t swapblk;
    err = swap_get_free(swapblk);
    if (err) {
        core_release_frame(frame);
        return err;
    }
    
    // clean up
    core_map_frame(frame, vaddr & PAGE_FRAME, pte, swapblk);
    core_release_frame(frame);
    pt_release_entry(pt, pte);
    return 0;
}

// Handle a page fault in the case in which the page has
// been swapped out.
int
vm_swapin_page_fault(vaddr_t vaddr, struct page_table *pt, struct pt_entry *pte)
{
    // find a free page frame
    paddr_t frame = core_acquire_frame();
    if (frame == 0)
        return ENOMEM;
    
    // swap in the page
    swapidx_t swapblk = pte->pte_swapblk;
    swap_in(swapblk, frame);
    
    // clean up...
    core_map_frame(frame, vaddr & PAGE_FRAME, pte, swapblk);
    core_release_frame(frame);
    pt_release_entry(pt, pte);
    return 0;
}
