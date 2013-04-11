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

#include <mips/tlb.h>
#include <addrspace.h>
#include <cpu.h>
#include <lib.h>

#define NUM_ASIDS 63
#define RESERVED_ASID 63

// CPU-specific ASID table
struct asid_table {
    struct addrspace at_holders[NUM_ASIDS];
};

struct asid_table *
at_create(void)
{
    return kmalloc(sizeof(struct asid_table));
}

void
at_destroy(struct asid_table *at)
{
    kfree(at);
}

int
at_assign(struct asid_table *at, struct addrspace *as)
{
    if (as == NULL)
        return RESERVED_ASID;
    
    if (at->at_holders[as->as_id] == as) {
        // the address space already has an ID
        return as->as_id;
    }
    else {
        // pick a random ASID
        // no synchronization is needed, as
        // this is per-CPU
        int asid = random() % NUM_ASIDS;
        at->at_holders[asid] = as;
        as->as_id = asid;
        tlb_flush_asid(asid);
    }
}
