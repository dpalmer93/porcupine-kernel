/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
#include <addrspace.h>
#include <asid.h>
#include <cpu.h>
#include <current.h>
#include <vm.h>
#include <page_table.h>
#include "opt-copyonwrite.h"
#include "opt-asid.h"

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
    
    as->as_pgtbl = pt_create();
    if (as->as_pgtbl == NULL) {
        kfree(as);
        return NULL;
    }
    
    // Start with empty segments
    for (int i = 0; i < NSEGS; i++)
        seg_zero(&as->as_segs[i]);
    
    // set up the stack and heap to be writeable
    seg_init(&as->AS_STACK, 0, 0, true);
    seg_init(&as->AS_HEAP, 0, 0, true);

    as->as_loading = false;
    as->as_id = 0;
	return as;
}
 
int
as_copy(struct addrspace *old_as, struct addrspace **ret)
{
	struct addrspace *new_as = kmalloc(sizeof(struct addrspace));
	if (new_as == NULL) {
		return ENOMEM;
	}

#if OPT_COPYONWRITE
	new_as->as_pgtbl = pt_copy_shallow(old_as->as_pgtbl);
#else
    new_as->as_pgtbl = pt_copy_deep(old_as->as_pgtbl);
#endif
    if (new_as->as_pgtbl == NULL) {
        kfree(new_as);
        return ENOMEM;
    }
    
    for (int i = 0; i < NSEGS + 2; i++) 
        new_as->as_segs[i] = old_as->as_segs[i];
    
    new_as->as_loading = false;
    new_as->as_id = 0;
    
	*ret = new_as;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
	pt_destroy(as->as_pgtbl);
#if OPT_ASID
    tlb_flush_asid(as->as_id);
#endif
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
#if OPT_ASID
    tlb_activate_asid(at_assign(curcpu->c_asids, as));
#else
    // unused
    (void)as;
    
    // invalidate the entire tlb on a context switch
    tlb_flush();
#endif
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
    // unused
	(void)executable;
    (void)readable;
    
    // Find an empty region and fill it
    // Temporarily allow writes until load is complete
    for (int i = 0; i < NSEGS; i++) {
        if (seg_available(&as->as_segs[i])) {
            seg_init(&as->as_segs[i], vaddr, sz, writeable);
            
            // Update base of the heap
            vaddr_t seg_top_aligned = (vaddr + sz + PAGE_SIZE - 1) & PAGE_FRAME;
            if (as->AS_HEAP.seg_base < seg_top_aligned)
                as->AS_HEAP.seg_base = seg_top_aligned;
            
            return 0;
        }
    }
        
	return ENOMEM;
}

int
as_prepare_load(struct addrspace *as)
{
	as->as_loading = true;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
    as->as_loading = false;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	vaddr_t stackbase = USERSTACK - PAGE_SIZE * STACK_NPAGES;
    
    // check for overlap with the heap
    vaddr_t heaptop = as->AS_HEAP.seg_base + as->AS_HEAP.seg_size;
    if (stackbase < heaptop)
        return ENOMEM;
    
    // seg up stack segment
    seg_init(&as->AS_STACK, stackbase, STACK_NPAGES * PAGE_SIZE, true);
    
    // Initial user-level stack pointer
	*stackptr = USERSTACK;
    
	return 0;
}

bool
as_can_read(struct addrspace *as, vaddr_t vaddr)
{
    // see if vaddr is in a defined region (including stack and heap)
    for (int i = 0; i < NSEGS + 2; i++) {
        if (seg_contains(&as->as_segs[i], vaddr))
            return true;
    }
    return false;
}

bool
as_can_write(struct addrspace *as, vaddr_t vaddr)
{
    // see if vaddr is in a defined region (including stack and heap)
    for (int i = 0; i < NSEGS + 2; i++) {
        if (seg_contains(&as->as_segs[i], vaddr)) {
            // turn off write protection when loading segments
            return as->as_loading? true : as->as_segs[i].seg_write;
        }
    }
    return false;
}

int
as_sbrk(struct addrspace *as, intptr_t amount, vaddr_t *old_heaptop)
{
    // get the old heap top
    vaddr_t heaptop = as->AS_HEAP.seg_base + as->AS_HEAP.seg_size;
    
    if (amount == 0) {
        *old_heaptop = heaptop;
        return 0;
    }
    
    // compute the new heap top
    vaddr_t new_heaptop = heaptop + amount;
    
    // check whether the heap top is less than the heap base
    // also check for integer overflow
    if (amount < 0 && new_heaptop > heaptop)
        return EINVAL;
    if (new_heaptop < as->AS_HEAP.seg_base)
        return EINVAL;
    
    // check for overlap with the stack
    vaddr_t stackbase = as->AS_STACK.seg_base;
    if (new_heaptop > stackbase)
        return ENOMEM;
    
    // update the heap segment and return
    as->AS_HEAP.seg_size += amount;
    *old_heaptop = heaptop;
    return 0;
}

bool
seg_available(const struct segment *seg)
{
    return seg->seg_base == 0 && seg->seg_size == 0;
}

bool
seg_contains(const struct segment *seg, vaddr_t vaddr)
{
    if (vaddr < seg->seg_base)
        return false;
    
    return vaddr - seg->seg_base < seg->seg_size;
}

void
seg_zero(struct segment *seg)
{
    seg_init(seg, 0, 0, 0);
}

void
seg_init(struct segment *seg, vaddr_t base, size_t size, bool write)
{
    seg->seg_base = base;
    seg->seg_size = size;
    seg->seg_write = write;
}
