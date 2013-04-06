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
 * pt_release_entry - release the lock on a page table entry acquired via
 *              one of the above functions.
 *
 * pte_try_lock - atomically tries to lock page table entry,
 *              returns true if successful
 *
 * pte_unlock: unlocks a page table entry
 */
struct pt_entry    *pt_acquire_entry(struct page_table *pt, vaddr_t vaddr);
struct pt_entry    *pt_create_entry(struct page_table *pt, vaddr_t vaddr, paddr_t paddr);
void                pt_release_entry(struct page_table *pt, pt_entry *pte);
bool                pte_try_lock(pt_entry *pte);
void                pte_unlock(pt_entry *pte);

// Must hold pte (via pt_acquire_entry() or pte_trylock()) to use these:
void pte_try_access(struct pt_entry *pte);
bool pte_try_dirty(struct pt_entry *pte);
void pte_clear_access(struct pt_entry *pte);
bool pte_is_inmem(struct pt_entry *pte);
bool pte_is_dirty(struct pt_entry *pte);
void pte_evict(struct pt_entry *pte, swapidx_t swapblk);

// Deep copy of the page table and all the page table entries
struct page_table *pt_copy_deep(struct page_table *pt);

#endif /* _PAGE_TABLE_H_ */
