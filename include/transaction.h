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

#ifndef _TRANSACTION_H_
#define _TRANSACTION_H_

#include <array.h>
#include <journal.h>
#include <buf.h>

struct transaction {
    struct journal  *txn_jnl;       // journal transaction belongs to
    unsigned         txn_qindex     // index into journal transaction queue
    uint64_t         txn_id;        // unique & monotonic ID
    uint32_t         txn_bufcount;  // # of modified buffers not yet synced
    daddr_t          txn_startblk;  // disk block containing start entry
    daddr_t          txn_endblk;    // disk block containing commit/abort entry
    struct bufarray *txn_bufs;      // array of modified buffers
};

DECLARRAY(transaction);

int txn_start(struct journal *jnl, struct transaction **ret);
void txn_close(struct transaction *txn); // call on buffer sync
void txn_destroy(struct transaction *txn); // destroy a transaction (must be synced)
// Attaches a transaction to a buffer.  Touches the buffer
int txn_attach(struct transaction *txn, struct buf *b);
int txn_commit(struct transaction *txn);
int txn_abort(struct transaction *txn);
bool txn_issynced(struct transaction *txn);

#endif /* _TRANSACTION_H_ */
