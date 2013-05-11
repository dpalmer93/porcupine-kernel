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

#include <kern/errno.h>
#include <buf.h>
#include <journal.h>
#include <synch.h>
#include <transaction.h>

#define TXN_MAX 128

DEFARRAY(transaction, /* noinline */);

// Allocates a transaction and writes it to disk
int
txn_start(struct journal *jnl, struct transaction **ret)
{
    int err;
    
    struct transaction *txn = kmalloc(sizeof(struct transaction));
    if (txn == NULL)
        return ENOMEM;
    
    txn->txn_bufs = bufarray_create();
    if (txn->txn_bufs == NULL) {
        kfree(txn);
        return ENOMEM;
    }
    
    txn->txn_bufcount = 0;
    txn->txn_jnl = jnl;
    txn->txn_committed = false;
    txn->txn_maptouched = false;
    
    lock_acquire(jnl->jnl_lock);
    // Wait until there is room in our transaction queue
    while (transactionarray_num(jnl->jnl_txnqueue) == TXN_MAX) {
        // sync the journal to commit transactions
        jnl_sync(jnl);
        
        lock_release(jnl->jnl_lock);
        // sync the buffer cache and freemap to close transactions
        err = FSOP_SYNC(jnl->jnl_fs);
        if (err) {
            bufarray_destroy(txn->txn_bufs);
            kfree(txn);
            return err;
        }
        lock_acquire(jnl->jnl_lock);
    }
    
    // Acquire a transaction ID
    txn->txn_id = jnl->jnl_txnid_next;
    jnl->jnl_txnid_next++;
    
    // Place transaction in txn_queue
    unsigned index;
    err = transactionarray_add(jnl->jnl_txnqueue, txn, &index);
    if (err) {
        lock_release(jnl->jnl_lock);
        bufarray_destroy(txn->txn_bufs);
        kfree(txn);
        return err;
    }
    

    // Write START journal entry.
    // This also releases the journal lock.
    err = jnl_write_start(txn, &txn->txn_startblk);
    if (err) {
        lock_release(jnl->jnl_lock);
        bufarray_destroy(txn->txn_bufs);
        kfree(txn);
        return err;
    }
    
    lock_release(jnl->jnl_lock);
    *ret = txn;
    return 0;
}

void
txn_destroy(struct transaction *txn)
{
    // txn must have been completely closed, so
    // we can safely clear the buffer array
    bufarray_setsize(txn->txn_bufs, 0);
    bufarray_destroy(txn->txn_bufs);
    kfree(txn);
}


int
txn_commit(struct transaction *txn)
{
    int err;
    
    // Write COMMIT journal entry.
    err = jnl_write_commit(txn, &txn->txn_endblk);
    if (err)
        return err;
    
    txn->txn_committed = true;
    return 0;
}

void
txn_oncommit(struct transaction *txn)
{
    // Decrement the refcount on all the buffers this txn modified
    // Also remove them from the bufarray
    for (unsigned i = 0; i < bufarray_num(txn->txn_bufs); i++) {
        buffer_txn_yield(bufarray_get(txn->txn_bufs, i));
    }
    bufarray_setsize(txn->txn_bufs, 0);
    
    // Also yield the freemap if necessary
    if (txn->txn_maptouched)
        sfs_map_txn_yield(txn);
}

int
txn_abort(struct transaction *txn)
{
    // Decrement the refcount on all the buffers this txn modified
    // Also remove them from the bufarray
    unsigned num = bufarray_num(txn->txn_bufs);
    for (unsigned i = 0; i < num; i++) {
        buffer_txn_yield(bufarray_get(txn->txn_bufs, i));
    }
    bufarray_setsize(txn->txn_bufs, 0);
    
    // Write ABORT journal entry.
    return jnl_write_abort(txn, &txn->txn_endblk);
}

// Buffer must be marked busy
int
txn_attach(struct transaction *txn, struct buf *b)
{
    // During recovery
    if (txn == NULL)
        return 0;
        
    int err;
    // Place transaction onto buffer
    err = buffer_txn_touch(b, txn);
    if (err == EAGAIN) {
        // Buffer and transaction have already been attached
        return 0;
    }
    else if (err) {
        return err;
    }
    
    lock_acquire(txn->txn_jnl->jnl_lock);
    // Place buffer onto transaction
    unsigned index;
    err = bufarray_add(txn->txn_bufs, b, &index);
    if (err) {
        lock_release(txn->txn_jnl->jnl_lock);
        return err;
    }
    
    // Increment number of buffers this transaction touches
    txn->txn_bufcount++;
    lock_release(txn->txn_jnl->jnl_lock);
    return 0;
}

int txn_mapattach(struct transaction *txn)
{
    // During recovery
    if (txn == NULL)
        return 0;
    
    int err;
    // Place transaction onto freemap
    err = sfs_map_txn_touch(txn);
    if (err == EAGAIN) {
        // Freemap and transaction have already been attached
        return 0;
    }
    else if (err) {
        return err;
    }
    
    lock_acquire(txn->txn_jnl->jnl_lock);
    // Place freemap onto transaction
    txn->txn_maptouched = true;
    // Increment bufcount
    txn->txn_bufcount++;
    lock_release(txn->txn_jnl->jnl_lock);
    return 0;
}

// Decrements refcount on a transaction
// If the refcount reaches 0, we do a checkpoint
void
txn_close(struct transaction *txn, struct buf *b)
{
    struct journal *jnl = txn->txn_jnl;
    unsigned i;
    
    txn->txn_bufcount--;
    if (!txn->txn_committed) {
        // the buffer must have been invalidated
        // remove it so that we do not try to yield it later
        unsigned num_bufs = bufarray_num(txn->txn_bufs);
        for (i = 0; i < num_bufs; i++) {
            if (bufarray_get(txn->txn_bufs, i) == b) {
                bufarray_remove(txn->txn_bufs, i);
                break;
            }
        }
        // should have removed the buf
        KASSERT(i < num_bufs);
    }
    else if (txn->txn_bufcount == 0) {
        // all buffers flushed
        // done with this transaction
        lock_acquire(jnl->jnl_lock);
        
        // remove txn from the queue, destroy it, and trigger a checkpoint
        unsigned num_txns = transactionarray_num(jnl->jnl_txnqueue);
        for (i = 0; i < num_txns; i++) {
            if (transactionarray_get(jnl->jnl_txnqueue, i) == txn) {
                transactionarray_remove(jnl->jnl_txnqueue, i);
                break;
            }
        }
        // should have removed the txn
        KASSERT(i < num_txns);
        
        txn_destroy(txn);
        jnl_docheckpoint(jnl);
        
        lock_release(jnl->jnl_lock);
    }
}

// Like txn_close(), but for the freemap
void
txn_mapclose(struct transaction *txn)
{
    struct journal *jnl = txn->txn_jnl;
    unsigned i;
    
    KASSERT(txn->txn_committed);
    
    txn->txn_bufcount--;
    if (txn->txn_bufcount == 0) {
        // all buffers flushed
        // done with this transaction
        lock_acquire(jnl->jnl_lock);
        
        // remove txn from the queue, destroy it, and trigger a checkpoint
        unsigned num_txns = transactionarray_num(jnl->jnl_txnqueue);
        for (i = 0; i < num_txns; i++) {
            if (transactionarray_get(jnl->jnl_txnqueue, i) == txn) {
                transactionarray_remove(jnl->jnl_txnqueue, i);
                break;
            }
        }
        // should have removed the txn
        KASSERT(i < num_txns);
        
        txn_destroy(txn);
        jnl_docheckpoint(jnl);
        
        lock_release(jnl->jnl_lock);
    }
}

