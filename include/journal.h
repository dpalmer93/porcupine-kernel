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

#define JE_PER_BLOCK 4
#define JE_SIZE 128

struct journal {
    struct bufarray jnl_blks;     // dynamic array of journal blocks
    daddr_t         jnl_next;     // next journal block on disk
    int             jnl_num_entry; // number of entries in jnl_entries
    struct lock    *jnl_lock;     // journal lock
};

struct jnl_entry {
    je_type_t           je_type; // type of operation
    uint32_t            je_txnid;
    time_t              je_timestamp;
    uint32_t            je_blknum;
    union {
        struct je_dirent_t  je_dirent;  // modification to a directory
        struct je_inode_t   je_inode;   // modification to an inode
        struct je_idblk_t   je_idblk;   // modification to an indriect block
        struct je_bitmap_t  je_bitmap;  // modification to a bitmap block
    };
    uint8_t             padding[SOME_PADDING_SIZE];
};

typedef enum {
    JE_START,           // first journal entry in a transaction
    JE_ABORT,           // last journal entry in a failed transaction
    JE_COMMIT,          // last journal entry in a successful transaction
    JE_EMPTY,           // empty journal entry
    JE_INODE,
    JE_DIRENT,
    JE_INDIRECT,
    JE_BITMAP
} je_type_t;

int                 jnl_write_start(struct journal *, uint64_t txn_id);
int                 jnl_write_commit(struct journal *, uint64_t txn_id);
int                 jnl_write_abort(struct journal *, uint64_t txn_id);
int                 jnl_write_entry(struct journal *, struct jnl_entry *);
int                 jnl_sync(struct journal *, uint64_t txn_id); // flushes all journal buffers
int                 jnl_get_buffer(struct journal *jnl); // gets the buffer for the next journal block

void journal_bootstrap(void);


#endif /* _JOURNAL_H_ */
