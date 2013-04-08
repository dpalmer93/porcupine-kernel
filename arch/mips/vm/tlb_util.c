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

#include <vm.h>
#include <spl.h>
#include <lib.h>
#include <mips/tlb.h>

void
tlb_dirty(vaddr_t vaddr)
{
    // turn off interrupts to make this atomic w.r.t. this CPU
    int x = splhigh();
    
    // get the index of the existing entry
    uint32_t entryhi = (vaddr & TLBHI_VPAGE);
    uint32_t entrylo = 0;
    int index = tlb_probe(entryhi, entrylo);
    KASSERT(index >= 0);
    
    // get the entry
    tlb_read(&entryhi, &entrylo, index);
    
    // set the dirty bit
    entrylo |= TLBLO_DIRTY;
    tlb_write(entryhi, entrylo, index);
    
    splx(x);
}

// Load a mapping
void
tlb_load(vaddr_t vaddr, paddr_t paddr, bool write)
{
    // turn off interrupts to make this atomic w.r.t. this CPU
    int x = splhigh();
    
    uint32_t entryhi = (vaddr & TLBHI_VPAGE);
    uint32_t entrylo = (paddr & TLBLO_PPAGE)
                     | (write? TLBLO_DIRTY : 0)
                     | TLBLO_VALID;
    
    // if the VPN is already in the TLB, replace it
    int index = tlb_probe(entryhi, 0);
    if (index >= 0)
        tlb_write(entryhi, entrylo, index);
    else // otherwise, use a random slot
        tlb_random(entryhi, entrylo);
    
    splx(x);
}

// The PTE lock (pte_busy) should be held before calling this
void
tlb_load_pte(vaddr_t vaddr, const struct pt_entry *pte)
{
    tlb_load(vaddr, MAKE_ADDR(pte->pte_frame, 0), pte->pte_dirty);
}

// invalidate by virtual page number and physical page number
void
tlb_invalidate(vaddr_t vaddr, const struct pt_entry *pte)
{
    // turn off interrupts to make this atomic w.r.t. this CPU
    int x = splhigh();
    
    // check whether there is a corresponding entry
    uint32_t entryhi = (vaddr & TLBHI_VPAGE);
    uint32_t entrylo = 0;
    int index = tlb_probe(entryhi, entrylo);
    if (index >= 0) {
        // get the entry and check the PPN
        tlb_read(&entryhi, &entrylo, index);
        if (entrylo >> 12 == pte->pte_frame) {
            // clear the valid bit
            entrylo &= ~TLBLO_VALID;
            tlb_write(entryhi, entrylo, index);
        }
    }
    
    splx(x);
}

// un-dirty by virtual page number and physical page number
void
tlb_clean(vaddr_t vaddr, const struct pt_entry *pte)
{
    // turn off interrupts to make this atomic w.r.t. this CPU
    int x = splhigh();
    
    // check whether there is a corresponding entry
    uint32_t entryhi = (vaddr & TLBHI_VPAGE);
    uint32_t entrylo = 0;
    int index = tlb_probe(entryhi, entrylo);
    if (index >= 0) {
        // get the entry and check the PPN
        tlb_read(&entryhi, &entrylo, index);
        if (entrylo >> 12 == pte->pte_frame) {
            // clear the dirty bit
            entrylo &= ~TLBLO_DIRTY;
            tlb_write(entryhi, entrylo, index);
        }
    }
    
    splx(x);
}

/*
 * This should do basically the same thing as tlb_reset().
 */
void
tlb_flush(void)
{
    // turn off interrupts to make this atomic w.r.t. this CPU
    int x = splhigh();
    
    for (int i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    
    splx(x);
}
