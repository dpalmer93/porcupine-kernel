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
 *
 * process.h: Process system declarations.
 */

#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <types.h>
#include <thread.h>
#include <synch.h>
#include <vnode.h>

struct pid_set;

typedef enum _pstat_t {
    PS_ACTIVE;
    PS_ZOMBIE;
} pstat_t;

struct process {
    pid_t                ps_pid;
    pstat_t              ps_status;
    int                  ps_ret_val;
    struct thread       *ps_thread;
    struct fd_table     *ps_fdt;
    struct addrspace    *ps_addrspace;
    struct pid_set      *ps_children;
    struct cv           *ps_waitpid_cv;
    struct lock         *ps_waitpid_lock;
};

pid_t           process_create(void);
void            process_destroy(pid_t pid);
struct process *get_process_from_pid(pid_t pid);


struct file_ctxt {
    struct vnode       *fh_vnode;
    unsigned int        fh_refcount;
    off_t               fh_offset;
    size_t              fh_filesize;
    int                 fh_flags;
    struct lock        *fh_lock;
};

struct file_ctxt *fc_create(struct vnode *file);
void fc_close(struct file_ctxt *ctxt);

struct fd_table {
    file_ctxt          *fds[MAX_FD];
    struct rw_mutex     fd_rw;
}

struct fd_table *fd_table_create(void);
void             fd_table_destroy(struct fd_table *fdt);

// create a new, separately synchronized fdt referencing the
// same file contexts
struct fd_table *fd_table_copy(struct fd_table *fdt);

// access the FC associated to an FD (synchronized)
struct file_ctxt *fdt_get(struct fd_table *fdt, int fd);

// find an FD and associate it with the FC (synchronized)
// returns -1 on failure
int fdt_insert(struct fd_table *fdt, struct file_ctxt *ctxt);



#endif /* _PROCESS_H_ */
