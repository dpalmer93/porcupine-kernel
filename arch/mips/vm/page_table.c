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
#include <synch.h>
#include <addrspace.h>

#define LEVEL_SIZE 1024
#define INDEX_TO_VADDR(L1, L2) (L1 << 22 + l2 << 12)
#define L1_INDEX(va) (PAGE_NUM(va) >> 10) // index into the level 1 table
#define L2_INDEX(va) (PAGE_NUM(va) & (LEVEL_SIZE - 1)) // index into the level 2 table

struct page_table
{
    struct pt_entry **pt_index[LEVEL_SIZE];
    struct lock      *pt_lock;
    struct cv        *pt_cv;
};

struct page_table *
pt_create(void)
{
    struct page_table *pt = kmalloc(sizeof(struct page_table));
    if (pt == NULL)
        return NULL;
    
    pt->pt_lock = lock_create("page table lock");
    if (pt->pt_lock == NULL) {
        kfree(pt);
        return NULL;
    }
    pt->pt_cv = cv_create("page table CV");
    if (pt->pt_cv == NULL) {
        lock_destroy(pt->pt_lock);
        kfree(pt);
        return NULL;
    }
    bzero(pt->pt_index, LEVEL_SIZE * sizeof(struct pt_entry **)); // zero the index
    
    return pt;
}

void
pt_destroy(struct page_table *pt)
{
    cv_destroy(pt->pt_cv);
    lock_destroy(pt->pt_lock);
    
    for (int i = 0; i < LEVEL_SIZE; i++) {
        if (pt->pt_index[i] != NULL) {
            for (int j = 0; j < LEVEL_SIZE; j++) {
                if (pt->pt_index[i][j] != NULL) {
                    // free the page frame or swap space
                    pt_entry *pte = pt->pt_index[i][j];
                    if (pte->pt_inmem)
                        core_free_frame(MAKE_ADDR(pte->pte_frame, 0));
                    else
                        swap_free(pte->pte_swapblk);
                    
                    // free the PTE
                    kfree(pte);
                }
            }
            kfree(pt->pt_index[i]);
        }
    }
    kfree(pt);
}

struct page_table *
pt_copy_deep(struct page_table *old)
{
    struct page_table *new_pt = pt_create();
    if (new_pt == NULL)
        return NULL;
        
    // Loop through the old page table and copy entries
    for (int i = 0; i < LEVEL_SIZE; i++) {
        if (old->pt_index[i] != NULL) {
            for (int j = 0; j < LEVEL_SIZE; j++) {
                if (old->pt_index[i][j] != NULL) {
                
                    struct pt_entry *old_pte = pt_acquire_entry(old, INDEX_TO_VADDR(i, j));
                    if (pt_create_entry(new_pt, INDEX_TO_VADDR(i, j))) {
                        pt_release_entry(old_entry);
                        pt_destroy(new_pt);
                        return NULL;
                    }
                    struct pt_entry *new_pte = new_pt->pt_index[i][j];
                    
                    // Copy the new_pte's page to a new place in swap
                    swapidx_t freeblk;
                    if(swap_get_free(swapidx_t &freeblk)) {
                        pt_release_entry(old_entry);
                        pt_destroy(new_pt);
                        return NULL;
                    }    
                    if (old_pte->pte_inmem) {
                        if(swap_out(MAKE_ADDR(old_pte->pte_frame , off)), freeblk)) {
                            swap_free(freeblk);
                            pt_release_entry(old_entry);
                            pt_destroy(new_pt);
                            return NULL;
                        }
                    }
                    else {
                        if(swap_copy(old_pte->pte_swapblk, freeblk)) {
                            swap_free(freeblk);
                            pt_release_entry(old_entry);
                            pt_destroy(new_pt);
                            return NULL;
                        }
                    }
                    
                    // put the correct info into new_pte
                    new_pte = {
                        .pte_busy = 0;
                        .pte_inmem = 0;
                        .pte_swapblk = freeblk;
                    };                
                    
                    pt_release_entry(old_pte);
                }
            }
        }
    }       
            
            
         
    
    
    
}

/**************** SYNCHRONIZATION FUNCTIONS ****************/

// Guaranteed to return the page table entry if one exists
// Spins if the PTE is busy
struct pt_entry *
pt_acquire_entry(struct page_table *pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    
    lock_acquire(pt->pt_lock);
    if (pt->pt_index[l1_idx] == NULL) {
        lock_release(pt->pt_lock);
        return NULL;
    }
    
    struct pt_entry *pte = pt->pt_index[l1_idx][l2_idx];
    if (entry == NULL) {
        lock_release(pt->pt_lock);
        return NULL;
    }
    
    // wait for the page to become available
    while (pte->pte_busy)
        cv_wait(pt->pt_cv, pt->pt_lock);
    
    pte->pte_busy = 1;
    lock_release(pt->pt_lock);
    return pte;
}

struct pt_entry *
pt_create_entry(struct page_table *pt, vaddr_t vaddr)
{
    unsigned long l1_idx = L1_INDEX(vaddr);
    unsigned long l2_idx = L2_INDEX(vaddr);
    
    lock_acquire(pt->pt_lock);
    
    bool new_l2 = false;
    if (pt->pt_index[l1_idx] == NULL) {
        // create a new level 2 table
        pt->pt_index[l1_idx] = kmalloc(LEVEL_SIZE * sizeof(struct pt_entry *));
        if (pt->pt_index[l1_idx] == NULL) {
            lock_release(pt->pt_lock);
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
            lock_release(pt->pt_lock);
            return NULL;
        }
    }
    else { // entry already exists
        lock_release(pt->pt_lock);
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
    
    lock_release(pt->pt_lock);
}

void
pt_release_entry(struct page_table *pt, struct pt_entry *pte)
{
    lock_acquire(pt->pt_lock);
    KASSERT(pte->pte_busy);
    pte->pte_busy = 0;
    cv_signal(pt->pt_cv);
    lock_release(pt->pt_lock);
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
