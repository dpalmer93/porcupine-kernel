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
#include <machine/vm.h>
#include <addrspace.h>
#include <lib.h>
#include <coremem.h>

struct cm_entry {
    struct addrspace   *pf_as;
    vaddr_t             pf_resident;
    blkcnt_t            pf_backing;
};

struct cm_entry     *coremap;
struct spinlock      core_lock = SPINLOCK_INITIALIZER;
unsigned long        core_lruclock;
unsigned long        core_nframes;

void
core_bootstrap(void)
{
    // get total physical memory
    paddr_t lo;
    paddr_t hi;
    ram_getsize(&lo, &hi);
    
    // calculate size of coremap
    core_nframes = (hi - lo) / PAGE_SIZE;
    size_t cmsize = core_nframes * sizeof(struct cm_entry);
    
    // allocate space for coremap
    size_t cm_npages = (cmsize + PAGE_SIZE - 1) / PAGE_SIZE;
    coremap = (struct cm_entry *)ram_stealmem(cm_npages);
    if (coremap == NULL)
        panic("Error during core map initialization!");
    
    // zero coremap
    bzero(coremap, cmsize);
}

paddr_t
core_get_frame(void)
{
    
}

void
core_free_frame(paddr_t pframe)
{
    
}
