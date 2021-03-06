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

#include <machine/vm.h>
#include <machine/tlb.h>
#include <kern/errno.h>
#include <lib.h>
#include <current.h>
#include <thread.h>
#include <process.h>
#include <addrspace.h>
#include <coremem.h>
#include <vmstat.h>
#include <kvm.h>
#include <vm.h>

void
vm_bootstrap(void)
{
    swap_bootstrap();
    // Initialize the tlbshootdown pool
    ts_bootstrap();
    core_cleaner_bootstrap();
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    // update statistics
    vs_incr_faults();
    
    /* Kernel TLB fault */
    if (kvm_managed(faultaddress)) {
        return kvm_fault(faultaddress);
    }
    
    /* User TLB fault */
    struct addrspace *as = curthread->t_proc->ps_addrspace;
    struct page_table *pt = as->as_pgtbl;
    struct pt_entry *pte = pt_acquire_entry(pt, faultaddress);
    
    
    // first check whether the address is valid
    if (!as_can_read(as, faultaddress))
        return EFAULT;
    
    switch (faulttype) {
        case VM_FAULT_READONLY:
            // Either the page is read-only, or the page is read/write
            // but the dirty bit has been cleared.  In either case,
            // the corresponding PTE must exist.
            // The page must also be in memory, as TLB entries
            // are never loaded for pages in swap.
            KASSERT(pte);
            
            if (!as_can_write(as, faultaddress)) {
                // cannot write to this page
                pte_unlock(pte);
                return EFAULT;
            }
            else if (pte_try_dirty(pte)) {
                // page was previously clean
                // reload the TLB with the dirtied PTE
                tlb_load_pte(faultaddress, pte);
                pte_unlock(pte);
                return 0;
            }
            else {
                // copy-on-write
                // NOTE: this will unlock the PTE when it is done
                return vm_copyonwrite_fault(faultaddress, pt);
            }
    
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            if (pte == NULL)
                return vm_unmapped_page_fault(faultaddress, pt);
            else if (!pte_try_access(pte)) { // PTE is in swap
                // NOTE: this will unlock the PTE when it is done
                return vm_swapin_page_fault(faultaddress, pte);
            }
            else {
                // Just load the TLB
                tlb_load_pte(faultaddress, pte);
                pte_unlock(pte);
                return 0;
            }
        
        default: // no other cases to consider
            return EINVAL;
    }
}

void
vm_tlbshootdown_all(void)
{
    // empty the entire TLB
    // (this should never occur,
    // as the per-CPU shootdown queue
    // is larger than the shootdown pool
    // from which shootdowns are allocated)
    tlb_flush();
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    switch (ts->ts_type) {
        case TS_CLEAN:
            tlb_clean(ts->ts_vaddr, ts->ts_pte);
            break;
            
        case TS_INVAL:
            tlb_invalidate(ts->ts_vaddr, ts->ts_pte);
            break;
    }
    // wake the sender
    ts_finish(ts);
}

/*
 * Kernel Memory Management functions
 */

vaddr_t
alloc_kpages(int npages)
{
    if (npages > 1) {
        // get contiguous pages from the kernel VM system
        return kvm_alloc_contig(npages);
    }
    
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
    if (kvm_managed(vaddr)) {
        kvm_free_contig(vaddr);
        return;
    }
    
    core_free_frame(KVADDR_TO_PADDR(vaddr));
}
