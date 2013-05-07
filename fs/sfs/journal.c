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

#define MAX_JNLBUFS 128

int 
jnl_write_start(struct transaction *txn, daddr_t *written_blk)
{    
    struct jnl_entry entry;
    entry.je_type = JE_START;
    entry.je_txnid = txn->txn_id;
    
    return jnl_write_entry(txn->txn_jnl, &entry, written_blk);
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

// Gets the next physical block available for journal and sets jnl->current
// Must hold the journal lock
static
void
jnl_next_block(struct journal *jnl)
{
    KASSERT(lock_do_i_hold(jnl->jnl_lock));
    
    daddr_t next_block = jnl->jnl_current + 1;
    if (next_block >= jnl->jnl_top) {
        next_block = jnl->jnl_bottom;
    }
    
    // We've hit our checkpoint so next block is unavailable
    // In this case, flush all the file system buffers
    if (next_block == jnl->jnl_checkpoint) {
        sync_fs_buffers(jnl->jnl_fs);
    }
    
    jnl->jnl_current = next_block;
    
    return;
}

int
jnl_write_entry(struct journal *jnl, struct jnl_entry *entry, daddr_t *written_blk)
{
    struct buf *iobuffer;
    int err;
    struct iovec iov;
    struct uio ku;
    
    // set up a uio to do the journal write
    uio_kinit(&iov, &ku, entry, SFS_JE_SIZE, 0, UIO_WRITE);
    
    lock_acquire(jnl->jnl_lock);
    
    // get next journal block if current is full
    if (jnl->jnl_blkoffset == SFS_JE_PER_BLOCK) {
        jnl_next_block(jnl);
        jnl->jnl_blkoffset = 0;
    }
    if (written_blk != NULL)
        *written_blk = jnl->jnl_current;
    
    int offset = jnl->jnl_blkoffset * SFS_JE_SIZE;
    
    // write journal entry to proper buffer
    err = buffer_get(jnl->jnl_fs, jnl->jnl_current, 512, &iobuffer);
    if (err) {
        lock_release(jnl->jnl_lock);
        return err;
    }
    void *ioptr = buffer_map(iobuffer);
    err = uiomove(ioptr + offset, SFS_JE_SIZE, &ku);
    if (err) {
        buffer_release(iobuffer);
        lock_release(jnl->jnl_lock);
        return err;
    }
    
    jnl->jnl_blkoffset++;
    
    // mark the buffer as dirty and place it in the journal's bufarray
    buffer_mark_dirty(iobuffer);
    unsigned index;
    bufarray_add(jnl->jnl_blks, iobuffer, &index);
    // if there are too many buffers on the journal buffer, flush it
    if (bufarray_num(jnl->jnl_blks) > MAX_JNLBUFS) {
        jnl_sync(jnl);
    }
    
    
    buffer_release(iobuffer);
    lock_release(jnl->jnl_lock);
    
    return 0;
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
jnl_set_linkcount(struct transaction *txn, uint32_t ino, uint16_t size)
{
    if (txn == NULL)
        return 0;
    struct jnl_entry je;
    je.je_type = JE_SET_LINKCOUNT;
    je.je_txnid = txn->txn_id;
    je.je_ino = ino;
    je.je_size = size;
    
    return jnl_write_entry(txn->txn_jnl, &je, NULL);
}

int
jnl_sync(struct journal *jnl)
{
    int result;
    for (unsigned i = 0; i < bufarray_num(jnl->jnl_blks); i++) {
        result = buffer_sync_extern(bufarray_get(jnl->jnl_blks, i));
        if (result)
            return result;
    }
    return 0;
}

int 
sfs_jnlmount(struct sfs_fs *sfs)
{
    struct journal *jnl = kmalloc(sizeof(struct journal));
    if (jnl == NULL)
        return ENOMEM;
    jnl->jnl_blks = bufarray_create();
    if (jnl->jnl_blks == NULL) {
        kfree(jnl);
        return ENOMEM;
    }
    jnl->jnl_lock = lock_create("SFS Journal Lock");
    if (jnl->jnl_lock == NULL) {
        bufarray_destroy(jnl->jnl_blks);
        kfree(jnl);
        return ENOMEM;
    }    
    
    // Set other fields
    jnl->jnl_bottom = SFS_JNLSTART(sfs->sfs_super.sp_nblocks);
    jnl->jnl_top = jnl->jnl_bottom + SFS_JNLSIZE(sfs->sfs_super.sp_nblocks);
    
    jnl->jnl_checkpoint = sfs->sfs_super.sp_ckpoint;
    
    if (!sfs->sfs_super.sp_clean) {    
        // Recovery
    
    }

    jnl->jnl_blkoffset = 0;
    jnl->jnl_fs = &sfs->sfs_absfs;
    jnl->jnl_current = jnl->jnl_checkpoint;
     
    return 0;
}

