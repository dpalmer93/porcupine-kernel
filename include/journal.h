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

#ifndef _JOURNAL_H_
#define _JOURNAL_H_

#include <types.h>
#include <array.h>
#include <buf.h>
#include <sfs.h>

// Assume, for sanity, that journal entries will be 128 bytes,
// independent of file system.  (VFS already makes a similar
// assumption for buffers.)
#define JE_PER_BLOCK 4
#define JE_SIZE 128

struct journal {
    struct bufarray *jnl_blks;       // dynamic array of journal blocks
    daddr_t          jnl_top;
    daddr_t          jnl_bottom;
    daddr_t          jnl_current;    // current journal block on disk being written to
    daddr_t          jnl_checkpoint; // address of first dirty journal block
    int              jnl_blkoffset;  // number of entries in jnl_entries
    struct fs       *jnl_fs;
    struct lock     *jnl_lock;       // journal lock
};

/* Commands to write entries */
int jnl_write_entry(struct journal *jnl, struct jnl_entry *je, daddr_t *written_blk);
int jnl_write_start(struct transaction *txn, daddr_t *written_blk);
int jnl_write_commit(struct transaction *txn, daddr_t *written_blk);
int jnl_write_abort(struct transaction *txn, daddr_t *written_blk);
int jnl_add_datablock_inode(struct transaction *txn, uint32_t ino, daddr_t childblk, int slot);
int jnl_add_datablock_indirect(struct transaction *txn, daddr_t parentblk, daddr_t childblk, int slot);
int jnl_new_inode(struct transaction *txn, uint32_t ino, uint16_t inotype);
int jnl_write_dir(struct transaction *txn, uint32_t ino, int slot, struct sfs_dir *dir);
int jnl_remove_inode(struct transaction *txn, uint32_t ino);
int jnl_remove_datablock_inode(struct transaction *txn, uint32_t ino, daddr_t childblk, int slot); 
int jnl_remove_datablock_indirect(struct transaction *txn, daddr_t parentblk, daddr_t childblk, int slot); 
int jnl_set_size(struct transaction *txn, uint32_t ino, uint32_t size);
int jnl_set_linkcount(struct transaction *txn, uint32_t ino, uint16_t linkcount);

// Sync all journal buffers to disk
int jnl_sync(struct journal *jnl);

int sfs_jnlmount(struct sfs_fs *sfs);


#endif /* _JOURNAL_H_ */
