/*
 * Copyright (c) 2000, 2001
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
 
#include <process.h>
#include <limits.h>

 /*
  * Implements functions that operate on file descriptor tables and file contexts.
  * These functions are synchronized.
 */
 
struct fd_table *
fd_table_create(){

    struct fd_table fdt;

    fdt->fd_rw = rw_create("fdt rw lock");
    if(fdt->fd_rw == NULL) {
        panic("fd_table_create: rw_create failed\n");
    }

    for(int i = 0; i < MAX_FD; i++) {
        fdt->fds[i] = NULL;
    }

    return fdt;
}


void 
fd_table_destroy(struct fd_table * fdt)
{
    KASSERT(fdt != NULL);

    rw_destroy(fdt->fd_rw);

    /* Call fc_close() which may or may not free the file_ctx
       depending on the number of references */
    for(int i = 0; i < MAX_FD; i++) {
        fc_close(fdt->fds[i]
    }

    return;
}

struct fd_table *
fd_table_copy(struct fd_table *fdt)
{
    struct fd_table *new_fdt;
    
    new_fdt = fd_table_create();
    
    rw_rlock(fdt->fd_rw);
    for(int i = 0; i < MAX_FD; i++) {
        new_fdt->fds[i] = fdt->fds[i];
    }
    rw_rdone(fdt->fd_rw);
        
    return new_fdt;
}

file_ctxt *
fdt_get(struct fd_table *fdt, int fd)
{
    struct file_ctxt fc;
    
    rw_rlock(fdt->fd_rw);
    fc = fdt->fds[fd];
    rw_rdone(fdt->fd_rw);
    
    return fc;
}

int fdt_insert(struct fd_table *fdt, struct file_ctxt *ctxt)
{
    rw_wlock(fdt->fd_rw);
    
    for(int i = 0; i < MAX_FD; i++) {
    
    }
    
    rw_wdone(fdt->fd_rw);
}
