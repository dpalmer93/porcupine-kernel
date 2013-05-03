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

int 
jnl_write_start(struct journal *jnl, uint64_t txnid)
{    
    struct jnl_entry *entry = kmalloc(sizeof(jnl_entry))
    if (entry == NULL) {
        return ENOMEM;
    }
    
    entry->je_type = JE_START;
    entry->je_txnid = txn_id;
    
    int err = jnl_write_entry(jnl, entry);
    
    free(entry);
    
    return err;
}

int 
jnl_write_commit(struct journal *jnl, uint64_t txnid)
{    
    struct jnl_entry *entry = kmalloc(sizeof(jnl_entry))
    if (entry == NULL) {
        return ENOMEM;
    }
    
    entry->je_type = JE_COMMIT;
    entry->je_txnid = txn_id;
    
    int err = jnl_write_entry(jnl, entry);
    
    free(entry);
    
    return err;
}

int 
jnl_write_abort(struct journal *jnl, uint64_t txnid)
{    
    struct jnl_entry *entry = kmalloc(sizeof(jnl_entry))
    if (entry == NULL) {
        return ENOMEM;
    }
    
    entry->je_type = JE_ABORT;
    entry->je_txnid = txn_id;
    
    int err = jnl_write_entry(jnl, entry);
    
    free(entry);
    
    return err;
}

int
jnl_write_entry(struct journal *jnl, struct jnl_entry *)
{
    struct *buf iobuffer;
    int err;
    struct iovec iov;
    struct uio *ku;
    
    // set up a uio to do the journal write
    uio_kinit(&iov, &ku, jnl_entry, JE_SIZE, 0, UIO_WRITE);
    
    lock_acquire(jnl->jnl_lock);
    
    // get next journal block if current is full
    if (jnl->jnl_num_entries == JE_PER_BLOCK) {
        jnl_next_block(jnl);
        jnl->jnl_num_entries = 0;
    }
    
    int offset = jnl->jnl_num_entries * JE_SIZE;
    
    // write journal entry to proper buffer
    err = buffer_get(jnl->jnl_fs, jnl->jnl_next, 512, &iobuffer);
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
    array_add(jnl->jnl_blks, iobuffer, &index)

    buffer_release(iobuffer);
    lock_release(jnl->jnl_lock);
    
    return 0;
}

// Gets the next physical block available for journal and sets it
void
jnl_next_block(struct journal *jnl)
{
    void (jnl);
    daddr_t next_block = jnl->jnl_next + 512
    if (next_block >= JOURNAL_TOP)
        next_block = JOURNAL_BOTTOM
    
    
    
    jnl->jnl->next = next_block;
    
}
