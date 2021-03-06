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

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */


#include <vm.h>
#include <page_table.h>
#include "opt-dumbvm.h"

struct vnode;

#if !(OPT_DUMBVM)
/*
 * VM Segment - data structure representing a block of memory with
 * common permissions.  These do not have to be page-aligned.
 */

struct segment {
    vaddr_t seg_base;
    size_t  seg_size;
    bool    seg_write; // write permission
};

bool seg_available(const struct segment *seg);
bool seg_contains(const struct segment *seg, vaddr_t vaddr);
void seg_zero(struct segment *seg);
void seg_init(struct segment *seg, vaddr_t base, size_t size, bool write);

// number of segments (other than stack and heap)
#define NSEGS 4

// user stack size
#define STACK_NPAGES 256

#endif // !(OPT_DUMBVM)

/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 */

struct addrspace {
#if OPT_DUMBVM
	vaddr_t as_vbase1;
	paddr_t as_pbase1;
	size_t as_npages1;
	vaddr_t as_vbase2;
	paddr_t as_pbase2;
	size_t as_npages2;
	paddr_t as_stackpbase;
};
#else
    unsigned int        as_id;
	struct page_table  *as_pgtbl;
    // NSEGS + the stack and heap
    struct segment      as_segs[NSEGS + 2];
    // turn off write protection while loading segments
    bool                as_loading;
};

// Macros for the stack and heap
#define AS_HEAP     as_segs[NSEGS]
#define AS_STACK    as_segs[NSEGS + 1]

#endif // OPT_DUMBVM

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make the specified address space the one currently
 *                "seen" by the processor. Argument might be NULL,
 *                meaning "no particular address space".
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(struct addrspace *);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, size_t sz,
                                   int readable,
                                   int writeable,
                                   int executable);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);



/*
 * Porcupine VM only (not dumbvm):
 * 
 *    as_can_read - tests whether the specified virtual address is valid, i.e.,
 *                  whether the process can read from it
 *
 *    as_can_write - tests whether the process can write to the specified
 *                  virtual address
 *
 *    as_sbrk - extends the heap by <amount> and returns the vaddr
 *                  of the previous heap top.
 */
 
#if !(OPT_DUMBVM)
bool as_can_read(struct addrspace *as, vaddr_t vaddr);
bool as_can_write(struct addrspace *as, vaddr_t vaddr);
int as_sbrk(struct addrspace *as, intptr_t amount, vaddr_t *old_heaptop);
#endif


/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);


#endif /* _ADDRSPACE_H_ */
