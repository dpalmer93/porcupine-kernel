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

#ifndef _MIPS_VM_H_
#define _MIPS_VM_H_

#include <types.h>

/*
 * Machine-dependent VM system definitions.
 */

#define PAGE_SIZE  4096         /* size of VM page */
#define PAGE_FRAME 0xfffff000   /* mask for getting page number from addr */
#define PAGE_NUM(addr) ((PAGE_FRAME & (addr)) >> 12) /* extract 20-bit page num */
#define PAGE_OFFSET(addr) ((addr) &~ PAGE_FRAME) /* extract 12-bit page offset */
#define MAKE_ADDR(pnum, off) (((pnum) << 12) | (off)) /* addr from page num and offset */

/*
 * MIPS-I hardwired memory layout:
 *    0xc0000000 - 0xffffffff   kseg2 (kernel, tlb-mapped)
 *    0xa0000000 - 0xbfffffff   kseg1 (kernel, unmapped, uncached)
 *    0x80000000 - 0x9fffffff   kseg0 (kernel, unmapped, cached)
 *    0x00000000 - 0x7fffffff   kuseg (user, tlb-mapped)
 *
 * (mips32 is a little different)
 */

#define MIPS_KUSEG  0x00000000
#define MIPS_KSEG0  0x80000000
#define MIPS_KSEG1  0xa0000000
#define MIPS_KSEG2  0xc0000000

/*
 * The first 512 megs of physical space can be addressed in both kseg0 and
 * kseg1. We use kseg0 for the kernel. This macro returns the kernel virtual
 * address of a given physical address within that range. (We assume we're
 * not using systems with more physical space than that anyway.)
 *
 * N.B. If you, say, call a function that returns a paddr or 0 on error,
 * check the paddr for being 0 *before* you use this macro. While paddr 0
 * is not legal for memory allocation or memory management (it holds
 * exception handler code) when converted to a vaddr it's *not* NULL, *is*
 * a valid address, and will make a *huge* mess if you scribble on it.
 */
#define PADDR_TO_KVADDR(paddr) ((paddr)+MIPS_KSEG0)

/*
 * A corresponding macro for converting in the opposite direction.
 */
#define KVADDR_TO_PADDR(vaddr) ((vaddr)-MIPS_KSEG0)

/*
 * The top of user space. (Actually, the address immediately above the
 * last valid user address.)
 */
#define USERSPACETOP  MIPS_KSEG0

/*
 * The starting value for the stack pointer at user level.  Because
 * the stack is subtract-then-store, this can start as the next
 * address after the stack area.
 *
 * We put the stack at the very top of user virtual memory because it
 * grows downwards.
 */
#define USERSTACK     USERSPACETOP

/*
 * Interface to the low-level module that looks after the amount of
 * physical memory we have.
 *
 * ram_getsize returns the lowest valid physical address, and one past
 * the highest valid physical address. (Both are page-aligned.) This
 * is the memory that is available for use during operation, and
 * excludes the memory the kernel is loaded into and memory that is
 * grabbed in the very early stages of bootup.
 *
 * ram_stealmem can be used before ram_getsize is called to allocate
 * memory that cannot be freed later. This is intended for use early
 * in bootup before VM initialization is complete.
 */

void ram_bootstrap(void);
paddr_t ram_stealmem(unsigned long npages);
void ram_getsize(paddr_t *lo, paddr_t *hi);

#define MAX_PTEREFCOUNT ((1 << 6) - 1)

/*
 * Page Table Entry Declaration
 * MIPS-Specific
 */
struct pt_entry {
    volatile unsigned   pte_busy:1;     // For synchronization
    unsigned            pte_inmem:1;    // In memory?
    unsigned            pte_refcount:6; // Number of page tables with this PTE
    union {
        // ---------------- In-memory fields ---------------- //
        struct {
            unsigned    pte_active:1;   // Recently accessed?
            unsigned    pte_dirty:1;    // Dirty page?
            unsigned    pte_cleaning:1; // Page currently being cleaned?
            unsigned    pte_swapin:1;   // Page being swapped in?
            unsigned    pte_frame:20;   // Page frame number
        } __attribute__((__packed__));
        // ----------------- On-disk fields ----------------- //
        unsigned        pte_swapblk:24; // Swap backing block
    } __attribute__((__packed__)); 
} __attribute__((__packed__));

/*
 * TLB shootdown bits.
 *
 * We'll take up to 16 invalidations before just flushing the whole TLB.
 */

// TLB shootdown types
#define TS_CLEAN 0 // Clean the TLB entry
#define TS_INVAL 1 // Invalidate the TLB entry

struct tlbshootdown {
    int                 ts_type;
	vaddr_t             ts_vaddr;
	struct pt_entry    *ts_pte;
    struct semaphore   *ts_sem;
};

struct tlbshootdown *ts_create(int type, vaddr_t vaddr, struct pt_entry *pte);
void ts_return(struct tlbshootdown *ts);
void ts_wait(const struct tlbshootdown *ts);
void ts_finish(const struct tlbshootdown *ts);
void ts_bootstrap(void);

#define TLBSHOOTDOWN_MAX 16

#endif /* _MIPS_VM_H_ */
