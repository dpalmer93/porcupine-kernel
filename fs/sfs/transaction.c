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

// Allocates a transaction and writes it to disk
int
txn_start(struct journal *jnl, struct transaction **ret)
{
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
    
    lock_acquire(jnl->jnl_lock);
    // Wait until there is room in our transaction queue
    while (jnl->jnl_txnqcount == TXN_MAX)
        cv_wait(jnl->jnl_txncv, jnl->jnl_lock);
    
    // Acquire a transaction ID
    txn->txn_id = jnl->jnl_txnid_next;
    jnl->jnl_txnid_next++;
    
    // Place transaction in txn_queue
    jnl->jnl_txnqueue[(jnl->jnl_txnqhead + jnl->jnl_txnqcount) % TXN_MAX] = txn;
    jnl->jnl_txnqcount++;
    
    // Write start journal entry to journal
    int err = jnl_write_start(txn, &txn->txn_startblk);
    if (err) {
        lock_release(jnl->jnl_lock);
        return err;
    }
    
    lock_release(jnl->jnl_lock);
    
    *ret = txn;
    return 0;
}

void
txn_destroy(struct transaction *txn)
{
    // "empty" the array---we do not need to actually
    // free any buffers here
    bufarray_setsize(txn->txn_bufs, 0);
    bufarray_destroy(txn->txn_bufs);
    kfree(txn);
}


int
txn_commit(struct transaction *txn)
{
    // Decrement the refcount on all the buffers this txn modified
    for (unsigned i = 0; i < bufarray_num(txn->txn_bufs); i++) {
        buffer_txn_yield(bufarray_get(txn->txn_bufs, i));
        bufarray_remove(txn->txn_bufs,i);
    }

    // Write commit message
    int err =  jnl_write_commit(txn, &txn->txn_endblk);
    if (err)
        return err;
        
    // Flush all the journal buffers
    return jnl_sync(txn->txn_jnl);
}

int
txn_abort(struct transaction *txn)
{
    // Decrement the refcount on all the buffers this txn modified
    for (unsigned i = 0; i < bufarray_num(txn->txn_bufs); i++) {
        buffer_txn_yield(bufarray_get(txn->txn_bufs, i));
    }
    
    return jnl_write_abort(txn, &txn->txn_endblk);
}

// Buffer must be marked busy
int
txn_attach(struct transaction *txn, struct buf *b)
{
    // During recover
    if (txn == NULL)
        return 0;
        
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
    unsigned index;
    err = bufarray_add(txn->txn_bufs, b, &index);
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
        jnl_docheckpoint(txn->txn_jnl);
    }
}

bool
txn_issynced(struct transaction *txn)
{
    return (txn->txn_bufcount == 0);
}

