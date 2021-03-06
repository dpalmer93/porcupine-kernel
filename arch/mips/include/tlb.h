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

#ifndef _MIPS_TLB_H_
#define _MIPS_TLB_H_

#include <mips/vm.h>

/*
 * MIPS-specific TLB access functions.
 *
 *   tlb_random: write the TLB entry specified by ENTRYHI and ENTRYLO
 *        into a "random" TLB slot chosen by the processor.
 *
 *        IMPORTANT NOTE: never write more than one TLB entry with the
 *        same virtual page field.
 *
 *   tlb_write: same as tlb_random, but you choose the slot.
 *
 *   tlb_read: read a TLB entry out of the TLB into ENTRYHI and ENTRYLO.
 *        INDEX specifies which one to get.
 *
 *   tlb_probe: look for an entry matching the virtual page in ENTRYHI.
 *        Returns the index, or a negative number if no matching entry
 *        was found. ENTRYLO is not actually used, but must be set; 0
 *        should be passed.
 *
 *        IMPORTANT NOTE: An entry may be matching even if the valid bit
 *        is not set. To completely invalidate the TLB, load it with
 *        translations for addresses in one of the unmapped address
 *        ranges - these will never be matched.
 */

void tlb_random(uint32_t entryhi, uint32_t entrylo);
void tlb_write(uint32_t entryhi, uint32_t entrylo, uint32_t index);
void tlb_read(uint32_t *entryhi, uint32_t *entrylo, uint32_t index);
int tlb_probe(uint32_t entryhi, uint32_t entrylo);

/*
 * TLB utility functions:
 *
 * tlb_load - atomically load a mapping into the TLB
 *
 * tlb_load_pte - atomically load a page table mapping into the TLB.  The
 *              PTE lock should be held before calling this)
 *
 * tlb_invalidate - atomically invalidate zero or more entries in the TLB
 *              matching the specified vaddr/pte pair.  This is useful
 *              for simulating a hardware-managed page access bit.
 *
 * tlb_clean - atomically clear the dirty bit of any TLB entry matching
 *              the specified vaddr/pte mapping.
 *
 * tlb_flush - atomically empty the entire TLB.  This can be used on a
 *              context switch.
 *
 * tlb_activate_asid - activate an address space ID by placing it in
 *              the c0_entryhi register
 *
 * tlb_flush_asid - invalidate an address space ID
 */

void tlb_load(vaddr_t vaddr, paddr_t paddr, bool write, bool global);
void tlb_load_pte(vaddr_t vaddr, const struct pt_entry *pte);
void tlb_invalidate(vaddr_t vaddr, const struct pt_entry *pte);
void tlb_clean(vaddr_t paddr, const struct pt_entry *pte);
void tlb_flush(void);
void tlb_activate_asid(unsigned int asid);
void tlb_flush_asid(unsigned int asid);


/*
 * TLB entry fields.
 *
 * Note that the MIPS has support for a 6-bit address space ID. In the
 * interests of simplicity, we don't use it. The fields related to it
 * (TLBLO_GLOBAL and TLBHI_PID) can be left always zero, as can the
 * bits that aren't assigned a meaning.
 *
 * The TLBLO_DIRTY bit is actually a write privilege bit - it is not
 * ever set by the processor. If you set it, writes are permitted. If
 * you don't set it, you'll get a "TLB Modify" exception when a write
 * is attempted.
 *
 * There is probably no reason in the course of CS161 to use TLBLO_NOCACHE.
 */

/* Fields in the high-order word */
#define TLBHI_VPAGE   0xfffff000
#define TLBHI_PID     0x00000fc0

/* Fields in the low-order word */
#define TLBLO_PPAGE   0xfffff000
#define TLBLO_NOCACHE 0x00000800
#define TLBLO_DIRTY   0x00000400
#define TLBLO_VALID   0x00000200
#define TLBLO_GLOBAL  0x00000100

#define TLBHI_PID_SHIFT 6
#define TLBLO_PPAGE_SHIFT 12
#define TLBLO_DIRTY_SHIFT 10
#define TLBLO_VALID_SHIFT 9
#define TLBLO_GLOBAL_SHIFT 8

/*
 * Values for completely invalid TLB entries. The TLB entry index should
 * be passed to TLBHI_INVALID; this prevents loading the same invalid
 * entry into multiple TLB slots.
 */
#define TLBHI_INVALID(entryno) ((0x80000+(entryno))<<12)
#define TLBLO_INVALID()        (0)

/*
 * Number of TLB entries in the processor.
 */

#define NUM_TLB  64


#endif /* _MIPS_TLB_H_ */
