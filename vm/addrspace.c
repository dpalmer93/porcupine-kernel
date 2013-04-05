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
        as->as_segs[i].seg_npages = 0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	(void)old;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

	kfree(as);
}

void
as_activate(struct addrspace *as)
{
    // unused
	(void)as;
    
    // currently invalidate the entire tlb
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

    // page align vaddr
    vaddr = PAGE_FRAME(vaddr);
    
    // Find an empty region and fill it
    // Temporarily allow writes until load is complete
    for (int i = 0; i < NSEGS; i++) {
        if (as->as_segs[i].seg_npages == 0) {
            as->as_segs[i].seg_base = vaddr;
            as->as_segs[i].seg_npages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
            as->as_segs[i].seg_temp = (bool) writeable;
            as->as_segs[i].seg_write = 1;
            return 0;
        }
    }
    
	return EUNIMP;
}

int
as_prepare_load(struct addrspace *as)
{
	// Do nothing
	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
    // Properly set write permissions 
    for (int i = 0; i < NSEGS; i++) {
        if (as->as_segs[i].seg_npages) {
            as->as_segs[i].seg_write = as->as_segs[i].seg_temp;
        }
    }
	
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

bool
as_can_write(struct addrspace *as, vaddr_t vaddr)
{
    // see if vaddr is in a defined region
    for (int i = 0; i < NSEGS; i++) {
        int seg_top = as->as_segs[i].seg_base + as->as_segs[i].seg_npages * PAGE_SIZE;
        if (vaddr < seg_top && vaddr >= as->as_segs[i].seg_base) {
            return as->as_segs[i].seg_write;
        }
    }
    // can always write to stack or heap
    return (vaddr > as->as_stack || vaddr < as->as_heap);
}
