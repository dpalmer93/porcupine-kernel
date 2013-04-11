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

#ifndef _PAGE_TABLE_H_
#define _PAGE_TABLE_H_

#include <swap.h>

struct pt_entry;
struct page_table;

struct page_table  *pt_create(void);
void                pt_destroy(struct page_table *pt);

/*
 * SYNCHRONIZATION:
 * ================
 * pt_acquire_entry - lock and acquire a page table entry via its virtual
 *              address.  If this returns NULL, then there is no such page table entry,
 *              i.e., the vaddr is unmapped.
 *
 * pt_create_entry - create and lock a page table entry for the page containing the
 *              specified virtual address.
 *
 * pt_destroy_entry - destroy a page table entry previously created with pt_create_entry().
 *              This should only be called on an error immediately after pt_create_entry().
 *              It does not free associated swap or core space, as the caller might
 *              still hold locks on these
 *
 * pte_try_lock - atomically tries to lock page table entry,
 *              returns true if successful
 *
 * pte_unlock - unlock a page table entry
 */
struct pt_entry    *pt_acquire_entry(struct page_table *pt, vaddr_t vaddr);
struct pt_entry    *pt_create_entry(struct page_table *pt, vaddr_t vaddr, paddr_t frame);
void                pt_destroy_entry(struct page_table *pt, vaddr_t vaddr);
bool                pte_try_lock(struct pt_entry *pte);
void                pte_unlock(struct pt_entry *pte);

/**** Must hold PTE lock (via pt_acquire_entry() or pte_trylock()) to use these: ***/

// Used to deal with write faults that require copying
// The PTE referred to by pt and vaddr must be locked
// Makes a deep copy of the PTE and returns it, both PTE's are locked
struct pt_entry *pt_copyonwrite(struct page_table* pt, vaddr_t vaddr);

bool pte_try_access(struct pt_entry *pte); // try to access the page
bool pte_try_dirty(struct pt_entry *pte); // try to dirty the page
bool pte_resident(struct pt_entry *pte); // check whether in memory
bool pte_is_dirty(struct pt_entry *pte); // check whether dirty
void pte_evict(struct pt_entry *pte, // evict the page to the swap block
               swapidx_t swapblk);
bool pte_refresh(vaddr_t vaddr, struct pt_entry *pte); // reset & return the "active" bit;
                                                       // invalidate TLBs if necessary
swapidx_t pte_start_swapin(struct pt_entry *pte, paddr_t frame); // mark as paging in
void pte_finish_swapin(struct pt_entry *pte); // mark as paged in
// non-blocking cleaning
void pte_start_cleaning(vaddr_t vaddr, struct pt_entry *pte);
void pte_finish_cleaning(struct pt_entry *pte);

// Deep copy of the page table and all the page table entries
struct page_table *pt_copy_deep(struct page_table *old_pt);

// Copy of page table with shallow copies of page table entries
struct page_table *pt_copy_shallow(struct page_table *old_pt);

#endif /* _PAGE_TABLE_H_ */
