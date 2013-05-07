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

#include <buf.h>
#include <kern/sfs.h>

// Assume, for sanity, that journal entries will be 128 bytes,
// independent of file system.  (VFS already makes a similar
// assumption for buffers.)
#define JE_PER_BLOCK 4
#define JE_SIZE 128

struct journal {
    struct bufarray jnl_blks;       // dynamic array of journal blocks
    daddr_t         jnl_current;    // current journal block on disk being written to
    daddr_t         jnl_checkpoint; // address of first dirty journal block
    int             jnl_blkoffset;  // number of entries written to current block
    struct fs      *jnl_fs;         // journal file system
    struct lock    *jnl_lock;       // journal lock
};

// 128 Bytes
struct jnl_entry {
    je_type_t           je_type; // type of operation
    uint64_t            je_txnid;
    uint32_t            je_ino;
    daddr_t             je_parentblk;
    daddr_t             je_childblk;
    int                 je_slot;
    uint32_t            je_size;
    uint16_t            je_inotype;
    uint16_t            je_linkcount;
    struct sfs_dir      je_dir;
    uint8_t             je_padding[28];
};

typedef enum {
    JE_INVAL = 0,       // invalid journal entry
    JE_START,           // first journal entry in a transaction
    JE_ABORT,           // last journal entry in a failed transaction
    JE_COMMIT,          // last journal entry in a successful transaction
    // Add data block je_childblk to inode je_ino.
    // It is the je_slot word in inode.
    JE_ADD_DATABLOCK_INODE,
    // Add data block je_childblk to indirect block je_parentblk.
    // It is the je_slot pointer in indirect.
    JE_ADD_DATABLOCK_INDIRECT,
    // Allocated a new inode block at block je_ino, with inumber je_ino, and type je_inotype
    JE_NEW_INODE,
    // Write je_dir into je_slot of the directory with inumber je_ino
    JE_WRITE_DIR,
    // Remove inode at block je_ino, with inumber je_ino
    JE_REMOVE_INODE,
    // Remove data block je_childblk from inode je_ino.  It is the je_slot word in the inode.
    JE_REMOVE_DATABLOCK_INODE,
    // Remove data block je_childblk from indirect block je_parentblk.
    // It is in the je_slot pointer in indirect.
    JE_REMOVE_DATABLOCK_INDIRECT,
    // Set the size of the file with inumber je_ino to je_size
    JE_SET_SIZE,
    // Set the linkcount of the file with inumber je_ino to je_linkcount
    JE_SET_LINKCOUNT
} je_type_t;

/* Commands to write entries */
int jnl_write_entry(struct journal *jnl, struct jnl_entry *je, daddr_t *written_blk);
int jnl_write_start(struct transaction *txn, daddr_t *written_blk);
int jnl_write_commit(struct transaction *txn, daddr_t *written_blk);
int jnl_write_abort(struct transaction *txn, daddr_t *written_blk);
int jnl_add_datablock_inode(struct transaction *txn, uint32_t ino, daddr_t childblk, int slot);
int jnl_add_datablock_inode(struct transaction *txn, daddr_t parentblk, daddr_t childblk, int slot);
int jnl_new_inode(struct transaction *txn, uint32_t ino, uint16_t inotype);
int jnl_write_dir(struct transaction *txn, uint32_t ino, int slot, struct sfs_dir *dir);
int jnl_remove_inode(struct transaction *txn, uint32_t ino);
int jnl_remove_datablock_inode(struct transaction *txn, uint32_t ino, daddr_t childblk, int slot); 
int jnl_remove_datablock_indirect(struct transaction *txn, daddr_t parentblk, daddr_t childblk, int slot); 
int jnl_set_size(struct transaction *txn, uint32_t ino, uint32_t size);
int jnl_set_linkcount(struct transaction *txn, uint32_t ino, uint16_t linkcount);

// Sync all journal buffers to disk
int jnl_sync(struct journal *jnl);

int jnl_bootstrap(void);


#endif /* _JOURNAL_H_ */
