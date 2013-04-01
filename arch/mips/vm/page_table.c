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
#include <errno.h>
#include <addrspace.h>

#define LEVEL_SIZE 1024
#define L1_INDEX(va) (PAGE_NUM(va) >> 10) // index into the level 1 table
#define L2_INDEX(va) (PAGE_NUM(va) & (LEVEL_SIZE - 1)) // index into the level 2 table
#define PAGE_OFFSET(addr) ((addr) &~ PAGE_FRAME)
#define MAKE_ADDR(pnum, off) (((pnum) << 12) | (off))

struct page_table
{
    struct pt_entry *pt_index[LEVEL_SIZE];
    struct spinlock  pt_lock;
};

struct page_table *
pt_create()
{
    struct page_table *pt = kmalloc(sizeof(struct page_table));
    bzero(pt, LEVEL_SIZE * sizeof(struct pt_entry *)); // zero the index
    spinlock_init(pt_lock);
}

void
pt_destroy(struct page_table *pt)
{
    for (int i = 0; i < LEVEL_SIZE; i++)
    {
        if (pt->pt_index[i] != NULL)
            kfree(pt->pt_index[i]);
    }
    spinlock_cleanup(pt->pt_lock);
    kfree(pt);
}

/**************** SYNCHRONIZATION FUNCTIONS ****************/

// NON-BLOCKING: If the page table entry is busy, this returns
// NULL
struct pt_entry *
pt_acquire_entry(struct page_table *pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    unsigned long offset = PAGE_OFFSET(vaddr);
    
    spinlock_acquire(pt->pt_lock);
    if (pt->pt_index[l1_idx] == NULL) {
        spinlock_release(pt->pt_lock);
        return NULL;
    }
    else {
        struct pt_entry *entry = pt->pt_index[l1_idx][l2_idx];
        if (entry == NULL || entry->pte_busy) {
            spinlock_release(pt->pt_lock);
            return NULL;
        }
        else {
            entry->pte_busy = 1;
            spinlock_release(pt->pt_lock);
            return entry;
        }
    }
}

void
pt_release_entry(struct page_table *pt, struct pt_entry *entry)
{
    spinlock_acquire(pt->pt_lock);
    KASSERT(entry->pte_busy);
    entry->pte_busy = 0;
    spinlock_release(pt->pt_lock);
}

/***********************************************************/



// Synchronized
paddr_t
pt_translate(const struct page_table *pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    unsigned long offset = PAGE_OFFSET(vaddr);
    
    spinlock_acquire(pt->pt_lock);
    if (pt->pt_index[l1_idx] == NULL)
    {
        spinlock_release(pt->pt_lock);
        return NULL;
    }
    else
    {
        struct pt_entry *entry = pt->pt_index[l1_idx][l2_idx];
        if (entry == NULL)
        {
            spinlock_release(pt->pt_lock);
            return NULL;
        }
        spinlock_release(pt->pt_lock);
        return MAKE_ADDR(entry->pte_frame, offset);
    }
}

// Synchronized
void
pt_set(struct page_table *pt, vaddr_t vaddr, paddr_t paddr, bool write)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    unsigned long ppnum = PAGE_NUM(paddr);
    
    spinlock_acquire(pt->pt_lock);
    if (pt->pt_index[l1_idx] == NULL)
    {
        // create a new level 2 table
        pt->pt_index[l1_idx] = kmalloc(LEVEL_SIZE * sizeof(struct pt_entry));
        bzero(pt->pt_index[l1_idx], LEVEL_SIZE * sizeof(struct pt_entry));
    }
    
    // initialize entry
    pt->pt_index[l1_idx][l2_idx] = {
        .pte_busy = 0,
        .pte_write = write,
        .pte_inmem = 1,
        .pte_term = 0,
        .pte_accessed = 0,
        .pte_dirty = 0,
        .pte_cow = 0,
        .pte_reserved = 0
        .pte_frame = ppnum,
    };
    
    spinlock_release(pt->pt_lock);
}

/******** Must be called with the pt_entry locked ********/


bool
pte_try_access(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    if (pte->pte_inmem) {
        pte->pte_accessed = 1;
        return true;
    }
    return false;
}

bool
pte_try_dirty(struct pt_entry *pte)
{
    KASSERT(pte != NULL);
    KASSERT(pte->pte_busy);
    if (pte->pte_inmem && pte->pte_write) {
        pte->pte_dirty = 1;
        return true;
    }
    return false;
}
