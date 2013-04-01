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

void
vm_bootstrap(void)
{
    
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    if (curthread->t_proc == NULL) /* kernel mode fault */ {
        switch (faulttype) {
            case VM_FAULT_READONLY:
                
            case VM_FAULT_READ:
            case VM_FAULT_WRITE:
                
            default: // no other cases allowed
                return -1;
        }
    }
    else /* user mode fault */ {
        struct page_table *pt = curthread->t_proc->ps_addrspace->as_pgtbl;
        struct pt_entry *pte = pt_acquire_entry(pt, faultaddress);
        
        switch (faulttype) {
            case VM_FAULT_READONLY:
                // Either the page is read-only, or the page is read/write
                // but the dirty bit has been cleared.  In either case,
                // the corresponding PTE must exist.
                KASSERT(pte);
                if (pte_try_dirty(pte)) {
                    tlb_dirty(faultaddress, pte);
                    pt_release_entry(pt, pte);
                    return 0;
                }
                else {
                    pt_release_entry(pt, pte);
                    return EFAULT;
                }
                
            case VM_FAULT_READ:
            case VM_FAULT_WRITE:
                // no valid mapping in the TLB
                if (pte && pte_try_access(pte)) {
                    tlb_load_pte(faultaddress, pte);
                    pt_release_entry(pt, pte);
                    return 0;
                }
                else {
                    return vm_page_fault(faultaddress, pte);
                }
                    
                break;
            
            default: // no other cases allowed
                return -1;
        }
    }
}
