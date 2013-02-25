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
 *
 * fdt.h: file descriptors, file contexts, and file descriptor tables.
 */

#ifndef _FDT_H_
#define _FDT_H_

#include <types.h>
#include <limits.h>
#include <synch.h> 
#include <vnode.h>

struct file_ctxt {
    struct vnode       *fc_vnode;
    unsigned int        fc_refcount;
    off_t               fc_offset;
    size_t              fc_filesize;
    int                 fc_flags;
    struct lock        *fc_lock;
};

struct file_ctxt *fc_create(struct vnode *file);
void fc_incref(struct file_ctxt *fc); // increment the refcount
void fc_close(struct file_ctxt *ctxt);

struct fd_table {
    struct file_ctxt          *fds[OPEN_MAX];
    struct rw_mutex           *fd_rw;
};

struct fd_table *fdt_create(void);
void             fdt_destroy(struct fd_table *fdt);

// create a new, separately synchronized FDT referencing the
// same file contexts
struct fd_table *fdt_copy(struct fd_table *fdt);

// find an FD and associate it with the FC (atomic)
// returns -1 on failure
int fdt_insert(struct fd_table *fdt, struct file_ctxt *fc);

// access the FC associated to an FD (atomic)
struct file_ctxt *fdt_get(struct fd_table *fdt, int fd);

// remove and return the FC associated to an FD (atomic)
struct file_ctxt *fdt_remove(struct fd_table *fdt, int fd);

// replace entry at FD with FC (atomic)
void fdt_replace(struct fd_table *fdt, int fd, struct file_ctxt *fc);

#endif /* _FDT_H_ */
