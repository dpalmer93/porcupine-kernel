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
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <page_table.h>
#include <machine/tlb.h>

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
    
    // Sets each region to uninitialized
    for (int i = 0; i < NSEGS; i++)
        seg_zero(as->as_segs[i])

	return as;
}
 
int
as_copy(struct addrspace *old_as, struct addrspace **ret)
{
	struct addrspace *new_as;

	new_as = as_create();
	if (new_as == NULL) {
		return ENOMEM;
	}

	new_as->as_pgtbl = pt_copy_deep(old_as->as_pgtbl);
    if (new_as->as_pgtbl == NULL) {
        kfree(new_as);
        return ENOMEM;
    }
    
    for (int i = 0; i < NSEGS + 2; i++) 
        new_as->as_segs[i] = old_as->as_segs[i];
    
    new_as->as_loading = false;
    
	*ret = new;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
	pt_destroy(as->as_pgtbl);
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
    // unused
    (void)as;
    
    // invalidate the entire tlb on a context switch
    tlb_flush();
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

    // vaddr must be page aligned
    KASSERT(PAGE_OFFSET(vaddr) == 0);
    
    size_t npages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // Find an empty region and fill it
    // Temporarily allow writes until load is complete
    for (int i = 0; i < NSEGS; i++) {
        if (seg_available(as->as_segs[i])) {
            seg_init(as->as_segs[i], vaddr, npages, writeable);
            
            // Update base of the heap
            vaddr_t seg_top = vaddr + npages * PAGE_SIZE;
            if (as->AS_HEAP->seg_base < seg_top)
                as->AS_HEAP->seg_base = seg_top;
            
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
    vaddr_t heaptop = as->AS_HEAP->seg_base + as->AS_HEAP->seg_npages * PAGE_SIZE;
    if (stackbase < heaptop)
        return ENOMEM;
    
    // seg up stack segment
    seg_init(as->AS_STACK, stackbase, STACK_NPAGES, true);
    
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
}

bool
seg_available(const struct segment *seg)
{
    return seg->seg_base == 0 && seg->seg_npages == 0;
}

bool
seg_contains(const struct segment *seg, vaddr_t vaddr)
{
    if (vaddr < seg->seg_base)
        return false;
    
    return (vaddr - seg->seg_base) / PAGE_SIZE < seg->seg_npages;
}

void
seg_zero(struct segment *seg)
{
    seg->seg_base = 0;
    seg->seg_npages = 0;
    seg->seg_write = false;
}

void
seg_init(struct segment *seg, vaddr_t base, size_t npages, bool write)
{
    seg->seg_base = base;
    seg->seg_npages = npages;
    seg->seg_write = write;
}