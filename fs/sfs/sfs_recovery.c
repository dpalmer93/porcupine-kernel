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

#include <sfs.h>
#include <kern/errno.h>

struct txnid_node {
    uint64_t txnid;
    struct txnid_node *next;
};

static
struct txnid_node *
txnid_node_create(uint64_t txnid) {
    struct txnid_node *n = kmalloc(sizeof(struct txnid_node));
    if (n == NULL)
        return NULL;
    n->txnid = txnid;
    n->next = NULL;
    return n;
}

static
void
txnid_list_destroy(struct txnid_node *head)
{
    while (head != NULL) {
        struct txnid_node *victim = head;
        head = head->next;
        kfree(victim);
    }
}

static
bool
on_txnid_list(struct txnid_node *head, uint64_t txnid) {
    while (head != NULL) {
        if (head->txnid == txnid)
            return true;
        head = head->next;
    }
    return false;
}

int
sfs_recover(struct sfs_fs *sfs)
{
    int err;
    struct buf *iterator;
    struct jnl_entry *je_blk;
    
    struct txnid_node *txnid_head = NULL;
    struct txnid_node *txnid_tail = NULL;
    
    reserve_buffers(2, SFS_BLOCKSIZE);
    
    uint32_t fsblocks = sfs->sfs_super.sp_nblocks;
    
    // get last checkpoint
    uint32_t checkpoint = sfs->sfs_super.sp_ckpoint;
    
    err = buffer_read(&sfs->sfs_absfs, checkpoint, SFS_BLOCKSIZE, &iterator);
    if (err) {
        unreserve_buffers(2, SFS_BLOCKSIZE);
        return err;
    }
    je_blk = buffer_map(iterator);
    
    uint64_t first_txnid;
    bool found = false;
    for (int i = 0; i < SFS_JE_PER_BLOCK; i++) {
        if (!found) {
            if (je_blk[i].je_type == JE_START) {
                first_txnid = je_blk[i].je_txnid;
                found = true;
            }
        }
        else {
            // Record committed transactions
            if (je_blk[i].je_type == JE_COMMIT) {
                struct txnid_node *n = txnid_node_create(je_blk[i].je_txnid);
                if (n == NULL){
                    buffer_release(iterator);
                    txnid_list_destroy(txnid_head);
                    unreserve_buffers(2, SFS_BLOCKSIZE);
                    return ENOMEM;
                }
                // First txnid found
                if(txnid_head == NULL) {
                    txnid_head = n;
                    txnid_tail = n;
                }
                // Otherwise put it at end of list
                else {
                    txnid_tail->next = n;
                    txnid_tail = n;
                }
            }
        }
    }

    buffer_release(iterator);
    
    uint32_t curblk = checkpoint + 1;
    while (curblk != checkpoint) {
        err = buffer_read(&sfs->sfs_absfs, curblk, SFS_BLOCKSIZE, &iterator);
        if (err) {
            txnid_list_destroy(txnid_head);
            unreserve_buffers(2, SFS_BLOCKSIZE);
            return err;
        }
        je_blk = buffer_map(iterator);
        for (int i = 0; i < SFS_JE_PER_BLOCK; i++) {
            // If you find an invalid entry, then we have found all the committed transactions
            if (je_blk[i].je_type == JE_INVAL) {
                buffer_release(iterator);
                goto foundall;
            }
            // If we find an start entry with a lower txnid then we have looped around
            if (je_blk[i].je_type == JE_START && je_blk[i].je_txnid < first_txnid) {
                buffer_release(iterator);
                goto foundall;
            }
            
            // Record committed transactions
            if (je_blk[i].je_type == JE_COMMIT) {
                struct txnid_node *n = txnid_node_create(je_blk[i].je_txnid);
                if (n == NULL){
                    buffer_release(iterator);
                    txnid_list_destroy(txnid_head);
                    unreserve_buffers(2, SFS_BLOCKSIZE);
                    return ENOMEM;
                }
                // First txnid found
                if(txnid_head == NULL) {
                    txnid_head = n;
                    txnid_tail = n;
                }
                // Otherwise put it at end of list
                else {
                    txnid_tail->next = n;
                    txnid_tail = n;
                }
            }
        }
        buffer_release(iterator);
        curblk = curblk + 1;
        if (curblk == fsblocks)
            curblk = SFS_JNLSTART(fsblocks);
    }
    
foundall:
    // Now loop through the journal and replay all the journal entries
    // that were part of committed transactions
    curblk = checkpoint;
    while (txnid_head != NULL)
    {
        err = buffer_read(&sfs->sfs_absfs, curblk, SFS_BLOCKSIZE, &iterator);
        if (err) {
            txnid_list_destroy(txnid_head);
            unreserve_buffers(2, SFS_BLOCKSIZE);
            return err;
        }
        je_blk = buffer_map(iterator);
        for (int i = 0; i < SFS_JE_PER_BLOCK; i++) {
            // If the transaction this journal entry is part of has been committed
            // then replay it
            if (on_txnid_list(txnid_head, je_blk[i].je_txnid)) {
                // REPLAY
                sfs_replay(&je_blk[i], sfs);
             
                // Remove txnid from list if we reach the commit message
                // It should always be at the head
                if (je_blk[i].je_type == JE_COMMIT) {
                    KASSERT(txnid_head->txnid == je_blk[i].je_txnid);
                    txnid_head = txnid_head->next;
                }
            }
            
        }
        buffer_release(iterator);
        curblk = curblk + 1;
        if (curblk == fsblocks)
            curblk = SFS_JNLSTART(fsblocks);
    }
    
    txnid_list_destroy(txnid_head);
    unreserve_buffers(2, SFS_BLOCKSIZE);
    
    return 0;
}
