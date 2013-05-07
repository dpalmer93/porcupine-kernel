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

struct transaction *txn_queue[TXN_MAX]; // transaction tracking
int txn_qhead;  // first item in q
int txn_qcount;  // next available spot in q

uint64_t txnid_next;
struct lock *txn_lock;
struct cv *txn_cv;

// EVIL KLUDGE
void
txn_bootstrap(void)
{
    txn_lock = lock_create("Transaction Lock");
    if (txn_lock == NULL)
        panic("txn_bootstrap: Out of memory\n");
    
    txn_cv = cv_create("Transaction CV");
    if (txn_cv == NULL) {
        lock_destroy(txn_lock);
        panic("txn_bootstrap: Out of memory\n");
    }
    
    txn_qhead = 0;
    txn_qcount = 0;
    txnid_next = 0;
}

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
    
    lock_acquire(txn_lock);
    // Wait until there is room in our transaction queue
    while (txn_qcount == TXN_MAX)
        cv_wait(txn_cv, txn_lock);
    
    // Acquire a transaction ID
    txn->txn_id = txnid_next;
    txnid_next++;
    
    // Place transaction in txn_queue
    txn_queue[(txn_qhead + txn_qcount) % TXN_MAX] = txn;
    txn_qcount++;
    
    // Write start journal entry to journal
    int err = jnl_write_start(txn, &txn->txn_startblk);
    if (err) {
        lock_release(txn_lock);
        return err;
    }
    
    lock_release(txn_lock);
    
    *ret = txn;
    return 0;
}

static
void
txn_destroy(struct transaction *txn)
{
    bufarray_destroy(txn->txn_bufs);
    kfree(txn);
}


int
txn_commit(struct transaction *txn)
{
    // Decrement the refcount on all the buffers this txn modified
    for (unsigned i = 0; i < bufarray_num(txn->txn_bufs); i++) {
        buffer_txn_yield(bufarray_get(txn->txn_bufs, i));
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
        txn_docheckpoint(txn->txn_jnl);
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
    lock_acquire(jnl->jnl_lock);
    
    // Searches through the queue
    // Frees any transactions that are done
    // Moves the checkpoint up accordingly
    int i = txn_qhead;
    daddr_t checkpoint = jnl->jnl_checkpoint;
    
    for (i = txn_qhead; i != (txn_qhead + txn_qcount) % TXN_MAX; i = (i + 1) % TXN_MAX) {
        struct transaction *txn = txn_queue[i];
        if (txn_issynced(txn)) {
            // Update checkpoint if transaction is synced
            checkpoint = txn->txn_startblk;           
            txn_destroy(txn);
            txn_qcount--;
            cv_signal(txn_cv, txn_lock);
        }
        else {
            break;
        }
    }
    txn_qhead = i;
    
    // Update checkpoint on superblock and write it
    struct sfs_fs *sfs = jnl->jnl_fs->fs_data;
    sfs->sfs_super.sp_ckpoint = checkpoint;
    sfs->sfs_superdirty = true;
    int err = sfs_writesuper(sfs);
            
    // Update checkpoint in journal
    if (err == 0)
        jnl->jnl_checkpoint = checkpoint;
    
    lock_release(jnl->jnl_lock);
    lock_release(txn_lock);
}

