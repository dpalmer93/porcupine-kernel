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

/*
 * SFS filesystem
 *
 * Transaction Management Routines
 */

#include <buf.h>
#include <journal.h>

#define TXN_MAX 128


struct transaction *txn_queue[TXN_MAX]; // transaction tracking
int txn_qhead;  // first item in q
int txn_qtail;  // next available spot in q

uint64_t txnid_next;
struct lock *txn_lock;
struct cv *txn_cv;

// Create a transaction and place it in the txn_queue
struct transaction *
txn_create(struct journal *jnl)
{
    struct transaction *txn = kmalloc(sizeof(struct transaction));
    if (txn == NULL)
        return NULL;
    
    txn->txn_bufs = array_create();
    if (txn->txn_bufs == NULL) {
        free(txn);
        return NULL;
    }
    
    txn->txn_bufcount = 0;
    txn->jnl = jnl;
    
    lock_acquire(txn_lock);
    // Wait until there is room in our transaction queue
    while (txn_qtail == txn_qhead)
        cv_wait(txn_cv, txn_lock);
    
    // Acquire a transaction ID
    txn->txn_id = txnid_next;
    txnid_next++;
    
    // Place transaction in txn_queue
    txn_queue[txn_qtail] = txn;
    txn_qtail = (txn_qtail + 1) % TXN_MAX;
    lock_release(txn_lock);
    
    return txn;
}

void
txn_destroy(struct transaction *txn)
{
    array_destroy(txn->bufs);
    kfree(txn);
}

int
txn_start(struct transaction *txn)
{
    return jnl_write_start(txn->jnl, txn->txn_id, &txn->txn_startblk);
}

int
txn_commit(struct transaction *txn)
{
    // Decrement the refcount on all the buffers this txn modified
    for (unsigned i = 0; i < array_num(txn->txn_bufs); i++) {
        buffer_txn_yield(array_get(txn->txn_bufs, i));
    }

    // Write commit message
    err =  jnl_write_commit(txn->jnl, txn->txn_id, &txn->txn_endblk);
    if (err)
        return err;
        
    struct journal *jnl = txn->txn_jnl;
    // Flush all the journal buffers
    jnl_sync(jnl);
    
    return 0;
}

int
txn_abort(struct transaction *txn)
{
    // Decrement the refcount on all the buffers this txn modified
    for (unsigned i = 0; i < array_num(txn->txn_bufs); i++) {
        buffer_txn_yield(array_get(txn->txn_bufs, i));
    }
    
    return jnl_write_abort(txn->jnl, txn->txn_id, &txn->txn_endblk);
}

// Buffer cannot already be in txn_bufs
// Buffer must be marked busy
int
txn_attach(struct transaction *txn, struct buf *b)
{
    int err;
    // Place transaction onto buffer
    err = buffer_txn_touch(b, txn);
    // Buffer and transaction have already been attached
    if (err == EINVAL) {
        return 0;
    }
    else if (err) {
        return err;
    }
    
    // Place buffer onto transaction
    int index;
    err = array_add(txn->txn_bufs, b, &index);
    if (err)
        return err;
    
    // Increment number of buffers this transaction touches
    txn->txn_bufcount++;
    return 0;
}

// Decrements refcount on a transaction
// If the refcount reaches 0, we do a checkpoint
void
txn_close(struct transaction *txn)
{
    txn->txn_bufcount--;
    if (txn->txn_bufcount == 0) {
        txn_docheckpoint(txn->jnl);
    }
}

bool
txn_issynced(struct transaction *txn)
{
    return (txn->txn_bufcount == 0);
}


void
txn_docheckpoint(struct journal *jnl)
{
    lock_acquire(txn_lock);
    
    // Searches through the queue
    // Frees any transactions that are done
    // Moves the checkpoint up accordingly
    int i = txn_qhead;
    while (i != txn_qtail) {
        struct transaction *txn = txn_queue[i];
        if (txn_issynced(txn)) {
            jnl->jnl_checkpoint = txn->txn_startblk;
            txn_destroy(txn);
        }
        else {
            break;
        }
    }
    txn_qhead = i;
    
    lock_release(txn_lock);
}

int
txn_bootstrap(void)
{
    txn_lock = lock_create("Transaction Queue Lock");
    if (txn_lock == NULL)
        return ENOMEM;
    txn_cv = cv_create("Transaction Queue CV");
    if (txn_cv == NULL) {
        lock_destroy(txn_lock);
        return ENOMEM;
    }
    
    txn_qhead = 0;
    txn_qtail = 0;
    txnid_next = 0;
    
    return 0;
}