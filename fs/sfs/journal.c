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
 * Journal Management Routines
 */

#include <types.h>
#include <kern/iovec.h>
#include <kern/errno.h>
#include <transaction.h>
#include <buf.h>
#include <uio.h>
#include <synch.h>
#include <journal.h>


// Gets the next physical block available for journal and sets jnl->current
// Must hold the journal lock
static
int
jnl_next_block(struct journal *jnl)
{
    int err;
    
    KASSERT(lock_do_i_hold(jnl->jnl_lock));
    
    daddr_t next_current = jnl->jnl_current + 1;
    daddr_t next_block = jnl->jnl_base + next_current;
    daddr_t next_base = jnl->jnl_base;
    
    if (next_block >= jnl->jnl_top || next_current >= MAX_JNLBLKS) {
        err = jnl_sync(jnl);
        if (err)
            return err;
        return 0;
    }
    
    // We've hit our checkpoint so next block is unavailable
    // In this case, flush all the file system buffers
    while (next_block == jnl->jnl_checkpoint) {
        lock_release(jnl->jnl_lock);
        err = FSOP_SYNC(jnl->jnl_fs);
        if (err) {
            lock_acquire(jnl->jnl_lock);
            return err;
        }
        lock_acquire(jnl->jnl_lock);
    }
    
    jnl->jnl_base = next_base;
    jnl->jnl_current = next_current;
    
    return 0;
}

// must be called while holding the journal lock
// releases the journal lock on success or failure
static
int
jnl_write_entry_internal(struct journal *jnl, struct jnl_entry *entry, daddr_t *written_blk)
{
    int err;
    int offset;
    
    // get next journal block if current is full
    if (jnl->jnl_blkoffset == JE_PER_BLK) {
        err = jnl_next_block(jnl);
        if (err)
            return err;
        jnl->jnl_blkoffset = 0;
    }
    
    if (written_blk != NULL)
        *written_blk = jnl->jnl_current + jnl->jnl_base;
    
    offset = jnl->jnl_blkoffset;
    jnl->jnl_blkoffset++;
    
    jnl->jnl_blks[jnl->jnl_current * JE_PER_BLK + offset] = *entry;
    return 0;
}

static
int
jnl_write_entry(struct journal *jnl, struct jnl_entry *entry, daddr_t *written_blk)
{
    lock_acquire(jnl->jnl_lock);
    
    // internal releases the lock
    int err = jnl_write_entry_internal(jnl, entry, written_blk);
    if (err) {
        lock_release(jnl->jnl_lock);
        return err;
    }
    lock_release(jnl->jnl_lock);
    return 0;
}

// unlike the other entries, start
// must be logged with the lock
// already held via txn_start
int 
jnl_write_start(struct transaction *txn, daddr_t *written_blk)
{   
    struct jnl_entry entry;
    entry.je_type = JE_START;
    entry.je_txnid = txn->txn_id;
    
    return jnl_write_entry_internal(txn->txn_jnl, &entry, written_blk);
}

int 
jnl_write_commit(struct transaction *txn, daddr_t *written_blk)
{    
    struct jnl_entry entry;
    entry.je_type = JE_COMMIT;
    entry.je_txnid = txn->txn_id;
    
    return jnl_write_entry(txn->txn_jnl, &entry, written_blk);
}

int 
jnl_write_abort(struct transaction *txn, daddr_t *written_blk)
{    
    struct jnl_entry entry;
    entry.je_type = JE_ABORT;
    entry.je_txnid = txn->txn_id;
    
    return jnl_write_entry(txn->txn_jnl, &entry, written_blk);
}

int
jnl_add_datablock_inode(struct transaction *txn, uint32_t ino, daddr_t childblk, int slot) 
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_ADD_DATABLOCK_INODE;
    je.je_txnid = txn->txn_id;
    je.je_ino = ino;
    je.je_childblk = childblk;
    je.je_slot = slot;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int 
jnl_add_datablock_indirect(struct transaction *txn, daddr_t parentblk, daddr_t childblk, int slot)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_ADD_DATABLOCK_INDIRECT;
    je.je_txnid = txn->txn_id;
    je.je_parentblk = parentblk;
    je.je_childblk = childblk;
    je.je_slot = slot;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int
jnl_new_inode(struct transaction *txn, uint32_t ino, uint16_t inotype)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_NEW_INODE;
    je.je_txnid = txn->txn_id;
    je.je_ino = ino;
    je.je_inotype = inotype;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int 
jnl_write_dir(struct transaction *txn, uint32_t ino, int slot, struct sfs_dir *dir)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_WRITE_DIR;
    je.je_txnid = txn->txn_id;
    je.je_ino = ino;
    je.je_slot = slot;
    je.je_dir.sfd_ino = dir->sfd_ino;
    strcpy(je.je_dir.sfd_name, dir->sfd_name);
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int
jnl_remove_inode(struct transaction *txn, uint32_t ino)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_REMOVE_INODE;
    je.je_txnid = txn->txn_id;
    je.je_ino = ino;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int 
jnl_remove_datablock_inode(struct transaction *txn, uint32_t ino, daddr_t childblk, int slot)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_REMOVE_DATABLOCK_INODE;
    je.je_txnid = txn->txn_id;
    je.je_ino = ino;    
    je.je_childblk = childblk;
    je.je_slot = slot;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int 
jnl_remove_datablock_indirect(struct transaction *txn, daddr_t parentblk, daddr_t childblk, int slot)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_REMOVE_DATABLOCK_INDIRECT;
    je.je_txnid = txn->txn_id;
    je.je_parentblk = parentblk;
    je.je_childblk = childblk;
    je.je_slot = slot;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int
jnl_set_size(struct transaction *txn, uint32_t ino, uint32_t size)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_SET_SIZE;
    je.je_txnid = txn->txn_id;
    je.je_ino = ino;
    je.je_size = size;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int
jnl_set_linkcount(struct transaction *txn, uint32_t ino, uint16_t linkcount)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_SET_LINKCOUNT;
    je.je_txnid = txn->txn_id;
    je.je_ino = ino;
    je.je_linkcount = linkcount;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int
jnl_sync(struct journal *jnl)
{
    int result;
    
    KASSERT(lock_do_i_hold(jnl->jnl_lock));
    
    // write out the journal buffer
    for (unsigned i = 0; i <= jnl->jnl_current; i++) {
        result = FSOP_WRITEBLOCK(jnl->jnl_fs,
                                 i + jnl->jnl_base,
                                 &jnl->jnl_blks[i * JE_PER_BLK],
                                 JNL_BLKSIZE);
        if (result)
            return result;
    }
    
    // finish committing transactions
    unsigned num_txns = transactionarray_num(jnl->jnl_txnqueue);
    for (unsigned i = 0; i < num_txns; i++) {
        struct transaction *txn = transactionarray_get(jnl->jnl_txnqueue, i);
        if (txn->txn_bufcount == 0 && txn->txn_committed) {
            // already completely closed (e.g., buffers invalidated)
            transactionarray_remove(jnl->jnl_txnqueue, i);
            i--;
            num_txns--;
            txn_destroy(txn);
        }
        else if (txn->txn_committed)
            txn_oncommit(txn);
    }
    
    // move the in-memory window
    jnl->jnl_base = jnl->jnl_current + jnl->jnl_base + 1;
    if (jnl->jnl_base >= jnl->jnl_top)
        jnl->jnl_base = jnl->jnl_bottom;
    jnl->jnl_current = 0;
    
    return 0;
}

void
jnl_destroy(struct journal *jnl, daddr_t *checkpoint, uint64_t *txnid)
{
    lock_acquire(jnl->jnl_lock);
    jnl_sync(jnl);
    
    KASSERT(transactionarray_num(jnl->jnl_txnqueue) == 0);
    
    // get last checkpoint and next txn ID
    if (checkpoint != NULL)
        *checkpoint = jnl->jnl_current + jnl->jnl_base;
    if (txnid != NULL)
        *txnid = jnl->jnl_txnid_next;
    
    transactionarray_destroy(jnl->jnl_txnqueue);
    
    lock_release(jnl->jnl_lock);
    lock_destroy(jnl->jnl_lock);
    kfree(jnl);
}

// journal lock must be held
void
jnl_docheckpoint(struct journal *jnl)
{
    daddr_t old_checkpoint = jnl->jnl_checkpoint;
    
    // get the minimum start block of any active transaction
    if (transactionarray_num(jnl->jnl_txnqueue) > 0) {
        daddr_t new_checkpoint = transactionarray_get(jnl->jnl_txnqueue, 0)->txn_startblk;
        
        if (new_checkpoint != old_checkpoint) {
            // Update checkpoint on superblock and try to write it
            struct sfs_fs *sfs = jnl->jnl_fs->fs_data;
            sfs->sfs_super.sp_ckpoint = new_checkpoint;
            sfs->sfs_super.sp_txnid = jnl->jnl_txnid_next;
            sfs->sfs_superdirty = true;
            int err = sfs_writesuper(sfs);
            
            // Update checkpoint in journal
            if (err == 0)
                jnl->jnl_checkpoint = new_checkpoint;
        }
    }
}

int
sfs_jnlmount(struct sfs_fs *sfs, uint64_t txnid_next, daddr_t checkpoint)
{
    struct journal *jnl = kmalloc(sizeof(struct journal));
    if (jnl == NULL)
        return ENOMEM;
    jnl->jnl_lock = lock_create("SFS Journal Lock");
    if (jnl->jnl_lock == NULL) {
        kfree(jnl);
        return ENOMEM;
    }
    jnl->jnl_txnqueue = transactionarray_create();
    if (jnl->jnl_txnqueue == NULL) {
        lock_destroy(jnl->jnl_lock);
        kfree(jnl);
        return ENOMEM;
    }
    
    // Set other fields
    jnl->jnl_fs = &sfs->sfs_absfs;
    jnl->jnl_bottom = SFS_JNLSTART(sfs->sfs_super.sp_nblocks);
    jnl->jnl_top = jnl->jnl_bottom + SFS_JNLSIZE(sfs->sfs_super.sp_nblocks);
    
    // set default values
    jnl->jnl_txnid_next = txnid_next;
    jnl->jnl_checkpoint = checkpoint;
    
    if (!sfs->sfs_super.sp_clean) {
        // need recovery
        int err = sfs_recover(sfs, &jnl->jnl_checkpoint, &jnl->jnl_txnid_next);
        if (err) {
            transactionarray_destroy(jnl->jnl_txnqueue);
            lock_destroy(jnl->jnl_lock);
            kfree(jnl);
            return err;
        }
        
        // so that FSOP_SYNC will not try to sync the journal
        sfs->sfs_jnl = NULL;
        
        // sync the changes to disk
        err = FSOP_SYNC(&sfs->sfs_absfs);
        if (err) {
            transactionarray_destroy(jnl->jnl_txnqueue);
            lock_destroy(jnl->jnl_lock);
            kfree(jnl);
            return err;
        }
        
        // update the superblock
        sfs->sfs_super.sp_ckpoint = jnl->jnl_checkpoint;
        sfs->sfs_super.sp_txnid = jnl->jnl_txnid_next;
        sfs->sfs_superdirty = true;
        err = sfs_writesuper(sfs);
        if (err) {
            transactionarray_destroy(jnl->jnl_txnqueue);
            lock_destroy(jnl->jnl_lock);
            kfree(jnl);
            return err;
        }
    }
    
    jnl->jnl_base = jnl->jnl_checkpoint;
    jnl->jnl_current = 0;
    jnl->jnl_blkoffset = 0;
    
    sfs->sfs_jnl = jnl;
    return 0;
}

