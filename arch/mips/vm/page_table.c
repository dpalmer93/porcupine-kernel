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
#include <vm.h>
#include <errno.h>
#include <addrspace.h>

#define LEVEL_SIZE 1024
#define L1_INDEX(va) (PAGE_NUM(va) >> 10) // index into the level 1 table
#define L2_INDEX(va) (PAGE_NUM(va) & (LEVEL_SIZE - 1)) // index into the level 2 table

struct page_table
{
    struct pt_entry **pt_index[LEVEL_SIZE];
    struct spinlock  pt_lock;
};

struct page_table *
pt_create()
{
    struct page_table *pt = kmalloc(sizeof(struct page_table));
    if (pt == NULL)
        return NULL;
    
    
    bzero(pt->pt_index, LEVEL_SIZE * sizeof(struct pt_entry **)); // zero the index
    spinlock_init(pt_lock);
}

void
pt_destroy(struct page_table *pt)
{
    // Will assert if someone is holding the spinlock
    spinlock_cleanup(pt->pt_lock);
    
    for (int i = 0; i < LEVEL_SIZE; i++) {
        if (pt->pt_index[i] != NULL) {
            for (int j = 0; j < LEVEL_SIZE; j++) {
                if (pt->pt_index[i][j] != NULL)
                    kfree(pt->pt_index[i][j]);
            }
            kfree(pt->pt_index[i]);
        }
    }
    kfree(pt);
}

/**************** SYNCHRONIZATION FUNCTIONS ****************/

// Guaranteed to return the page table entry if one exists
// Spins if the PTE is busy
struct pt_entry *
pt_acquire_entry(struct page_table *pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    
    spinlock_acquire(pt->pt_lock);
    if (pt->pt_index[l1_idx] == NULL) {
        spinlock_release(pt->pt_lock);
        return NULL;
    }
    
    struct pt_entry *pte = pt->pt_index[l1_idx][l2_idx];
    if (entry == NULL) {
        spinlock_release(pt->pt_lock);
        return NULL;
    }
    
    while (pte->pte_busy) {
        spinlock_release(pt->pt_lock);
        spinlock_acquire(pt->pt_lock);
    }
    
    pte->pte_busy = 1;
    spinlock_release(pt->pt_lock);
    return pte;
}

struct pt_entry *
pt_create_entry(struct page_table *pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    
    spinlock_acquire(pt->pt_lock);
    
    bool new_l2 = false;
    if (pt->pt_index[l1_idx] == NULL) {
        // create a new level 2 table
        pt->pt_index[l1_idx] = kmalloc(LEVEL_SIZE * sizeof(struct pt_entry *));
        if (pt->pt_index[l1_idx] == NULL) {
            spinlock_release(pt->pt_lock);
            return NULL;
        }
        // zero all its entries
        bzero(pt->pt_index[l1_idx], LEVEL_SIZE * sizeof(struct pt_entry *));
        // remember that we created a new level 2 table
        new_l2 = true;
    }
    
    if (pt->pt_index[l1_idx][l2_idx] == NULL) {
        // allocate a new PTE
        pt->pt_index[l1_idx][l2_idx] = kmalloc(sizeof(struct pt_entry));
        if (pt->pt_index[l1_idx][l2_idx] == NULL) {
            // if we allocated a new L2 table earlier, free it
            if (new_l2) kfree(pt->pt_index[l1_idx]);
            spinlock_release(pt->pt_lock);
            return NULL;
        }
    }
    else { // entry already exists
        spinlock_release(pt->pt_lock);
        return NULL;
    }
    
    // initialize and lock entry
    pt->pt_index[l1_idx][l2_idx] = {
        .pte_busy = 1,
        .pte_inmem = 1,
        .pte_accessed = 0,
        .pte_dirty = 0,
        .pte_reserved = 0,
        .pte_frame = 0
    };
    
    spinlock_release(pt->pt_lock);
}

void
pt_release_entry(struct page_table *pt, struct pt_entry *pte)
{
    spinlock_acquire(pt->pt_lock);
    KASSERT(pte->pte_busy);
    pte->pte_busy = 0;
    spinlock_release(pt->pt_lock);
}

/***********************************************************/




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
    if (pte->pte_inmem) {
        pte->pte_accessed = 1;
        pte->pte_dirty = 1;
        return true;
    }
    return false;
}
