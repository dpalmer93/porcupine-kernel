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
 
#include <mips/vm.h>
#include <mips/tlb.h>
#include <synch.h>
#include <lib.h> 

#define TOTAL_SHOOTDOWNS TLBSHOOTDOWN_MAX
 
static struct tlbshootdown *ts_pool[TOTAL_SHOOTDOWNS];
static int                  tp_index;
static struct lock         *tp_lock;
static struct cv           *tp_cv;

// Get a shootdown struct from the pool and fill it
struct tlbshootdown *
ts_create(int type, vaddr_t vaddr, struct pt_entry *pte)
{
    lock_acquire(tp_lock);
    
    // Wait if there are no shootdowns available
    while (tp_index == TOTAL_SHOOTDOWNS)
        cv_wait(tp_cv, tp_lock);
    
    // Get the next available shootdown
    struct tlbshootdown *ts = ts_pool[tp_index];
    tp_index++;
    
    lock_release(tp_lock);
    
    ts->ts_type = type;
    ts->ts_vaddr = vaddr;
    ts->ts_pte = pte;
    
    return ts;
}

// Return a shootdown struct to the pool
void
ts_return(struct tlbshootdown *ts)
{
    lock_acquire(tp_lock);
    KASSERT(tp_index > 0);
    
    tp_index--;
    ts_pool[tp_index] = ts;
    
    lock_release(tp_lock);
}
 
// Allocates all the shootdown structs in the pool
void
ts_bootstrap()
{
    tp_lock = lock_create("TLB Shootdown Pool Lock");
    if (tp_lock == NULL)
        panic("ts_bootstrap: Out of memory\n");
    tp_cv = cv_create("TLB Shootdown Pool CV");
    if (tp_cv == NULL)
        panic("ts_bootstrap: Out of memory\n");
    
    for (int i = 0; i < TOTAL_SHOOTDOWNS; i++) {
        ts_pool[i] = kmalloc(sizeof(struct tlbshootdown));
        if (ts_pool[i] == NULL)
            panic("ts_bootstrap: Out of memory\n");
        ts_pool[i]->ts_sem = sem_create("TLB Shootdown Semaphore", 0);
        if (ts_pool[i]->ts_sem == NULL)
            panic("ts_bootstrap: Out of memory\n");
    }
}
 
void
ts_wait(const struct tlbshootdown *ts)
{
    P(ts->ts_sem);
}
 
void
ts_finish(const struct tlbshootdown *ts)
{
    V(ts->ts_sem);
}
