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

#include <buf.h>
#include <journal.h>

#define MAX_JNLBUFS 128

int 
jnl_write_start(struct journal *jnl, uint64_t txnid, daddr_t *written_blk)
{    
    struct jnl_entry entry;
    entry.je_type = JE_START;
    entry.je_txnid = txn_id;
    
    return jnl_write_entry(jnl, &entry, written_blk);
}

int 
jnl_write_commit(struct journal *jnl, uint64_t txnid, daddr_t *written_blk)
{    
    struct jnl_entry entry;
    entry.je_type = JE_COMMIT;
    entry.je_txnid = txn_id;
    
    return jnl_write_entry(jnl, &entry, written_blk);
}

int 
jnl_write_abort(struct journal *jnl, uint64_t txnid, daddr_t *written_blk)
{    
    struct jnl_entry entry;
    entry.je_type = JE_ABORT;
    entry.je_txnid = txn_id;
    
    return jnl_write_entry(jnl, &entry, written_blk);
}

int
jnl_write_entry(struct journal *jnl, struct jnl_entry *entry, daddr_t *written_blk)
{
    struct *buf iobuffer;
    int err;
    struct iovec iov;
    struct uio *ku;
    
    // set up a uio to do the journal write
    uio_kinit(&iov, &ku, entry, JE_SIZE, 0, UIO_WRITE);
    
    lock_acquire(jnl->jnl_lock);
    
    // get next journal block if current is full
    if (jnl->jnl_num_entries == JE_PER_BLOCK) {
        jnl_next_block(jnl);
        jnl->jnl_num_entries = 0;
    }
    if (written_blk != NULL)
        *written_blk = jnl->jnl_current;
    
    int offset = jnl->jnl_num_entries * JE_SIZE;
    
    // write journal entry to proper buffer
    err = buffer_get(jnl->jnl_fs, jnl->jnl_current, 512, &iobuffer);
    if (err) {
        lock_release(jnl->jnl_lock);
        return err;
    }
    ioptr = buffer_map(iobuffer);
    err = uiomove(ioptr + offset, JE_SIZE, ku);
    if (err) {
        buffer_release(iobuffer);
        lock_release(jnl->jnl_lock);
        return err;
    }
    
    jnl->jnl_num_entries++;
    
    // mark the buffer as dirty and place it in the journal's bufarray
    buffer_mark_dirty(iobuffer);
    int index;
    array_add(jnl->jnl_blks, iobuffer, &index);
    // if there are too many buffers on the journal buffer, flush it
    if (array_num(jnl->jnl_blks) > MAX_JNLBUFS) {
        jnl_sync(jnl);
    }
    
    
    buffer_release(iobuffer);
    lock_release(jnl->jnl_lock);
    
    return 0;
}

// Gets the next physical block available for journal and sets jnl->current
// Must hold the journal lock
void
jnl_next_block(struct journal *jnl)
{
    KASSERT(lock_do_i_hold(jnl->jnl_lock));
    
    daddr_t next_block = jnl->jnl_current + 512;
    if (next_block >= JOURNAL_TOP) {
        next_block = JOURNAL_BOTTOM;
    }
    
    // We've hit our checkpoint so next block is unavailable
    // In this case, flush all the file system buffers
    if (next_block == jnl->checkpoint) {
        sync_fs_buffers(jnl->fs);
        jnl->checkpoint = JOURNAL_BOTTOM;
        next_block = JOURNAL_BOTTOM;
    }
    
    jnl->jnl_current = next_block;
    
    return;
}

int
jnl_add_datablock_inode(struct journal *jnl, uint64_t txnid, uint32_t ino, daddr_t childblk, int slot) 
{
    struct jnl_entry je;
    je.je_type = JE_ADD_DATABLOCK_INODE;
    je.je_txnid = txnid;
    je.je_ino = ino;
    je.je_childblk = childblk;
    je.je_slot = slot;
    
    return jnl_write_entry(jnl, &je, NULL);
}

int 
jnl_add_datablock_indirect(struct journal *jnl, uint64_t txnid, daddr_t parentblk, daddr_t childblk, int slot)
{
    struct jnl_entry je;
    je.je_type = JE_ADD_DATABLOCK_INDIRECT;
    je.je_txnid = txnid;
    je.je_parentblk = parentblk;
    je.je_childblk = childblk;
    je.je_slot = slot;
    
    return jnl_write_entry(jnl, &je, NULL);
}

int
jnl_new_inode(struct journal *jnl, uint64_t, txnid, uint32_t ino, uint16_t inotype)
{
    struct jnl_entry je;
    je.je_type = JE_NEW_INODE;
    je.je_txnid = txnid;
    je.je_ino = ino;
    je.je_inotype = inotype
    
    return jnl_write_entry(jnl, &je, NULL);
}

int 
jnl_write_dir(struct journal *jnl, uint64_t txnid, uint32_t ino, int slot, struct sfs_dir *dir)
{
    struct jnl_entry je;
    je.je_type = JE_WRITE_DIR;
    je.je_txnid = txnid;
    je.je_ino = ino;
    je.je_slot = slot;
    je.je_dir.sfd_ino = dir->sfd_ino;
    strcpy(je.je_dir.sfd_name, dir->sfd_name)
    
    return jnl_write_entry(jnl, &je, NULL);
}

int
jnl_remove_inode(struct journal *jnl, uint64_t txnid, uint32_t ino)
{
    struct jnl_entry je;
    je.je_type = JE_REMOVE_INODE;
    je.je_txnid = txnid;
    je.je_ino = ino;
    
    return jnl_write_entry(jnl, &je, NULL);
}

int 
jnl_remove_datablock_inode(struct journal *jnl, uint64_t txnid, uint32_t ino, daddr_t childblk, int slot)
{
    struct jnl_entry je;
    je.je_type = JE_REMOVE_DATABLOCK_INODE;
    je.je_txnid = txnid;
    je.je_ino = ino;    
    je.je_childblk = childblk;
    je.je_slot = slot;
    
    return jnl_write_entry(jnl, &je, NULL);
}

int jnl_remove_datablock_indirect(struct journal *jnl, uint64_t txnid, daddr_t parentblk, daddr_t childblk, int slot); 
{
    struct jnl_entry je;
    je.je_type = JE_REMOVE_DATABLOCK_INDIRECT;
    je.je_txnid = txnid;
    je.je_parentblk = parentblk;
    je.je_childblk = childblk;
    je.je_slot = slot;
    
    return jnl_write_entry(jnl, &je, NULL);
}

int 
jnl_sync(struct journal *jnl)
{
    for (unsigned i = 0; i < array_num(jnl->jnl_blks); i++) {
        buffer_sync(array_get(jnl->jnl_blks, i));
    }
}