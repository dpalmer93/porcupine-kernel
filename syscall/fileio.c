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

#include <types.h>
#include <errno.h>
#include <process.h>
#include <machine/trapframe.h>
#include <syscall.h>
#include <uio.h>


// Error stored in err
int
sys_open(const_userptr_t filename, int flags, int* err)
{
    int result;
    size_t got;
    char kfilename[PATH_MAX];
    struct vnode *file;
    struct file_ctxt *fc;
    struct fd_table fdt;
    
    // copy in filename to kernel space
    result = copyinstr(filename, kfilename, PATH_MAX, &got);
    if (result){
        *err = ENAMETOOLONG;
        return -1;
    }
        
    file = kmalloc(sizeof(struct vnode));
    if (file == NULL) {
        *err = ENOMEM;
        return -1;
    }    
        
    result = vfs_open(kfilename, int flags, 0, &file);
    if (result) {       
        kfree(file);
        *err = result;
        return -1;
    }

    fc = fc_create(file);
    if (fc == NULL) {
        vfs_close(file);
        *err = ENOMEM;
        return -1;
    }
    
    fdt = curthread->t_proc->ps_fdt;
    result = fdt_insert(fdt, fc);
    if (result == -1) {
        fc_destroy(fc);
        *err = EMFILE;
        return -1;
    }
    
    return result;
    
}

// Error is returned
int
sys_close(int fd)
{
    struct file_table *fdt;
    struct file_ctxt *fc;

    fdt = curthread->t_proc->ps_fdt;
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        return EBADF;
    }
    
    fc_close(fc);
    
    rw_wlock(fdt->fd_rw);
    fdt->fds[fd] = NULL;
    rw_wdone(fdt->fd_rw);
    
    return 0;
}

// Error stored in err
int
read(int fd, void *buf, size_t buflen, int *err)
{
    struct file_table *fdt;
    struct file_ctxt *fc;
    struct uio myuio;
    int result, amount_read;
    
    fdt = curthread->t_proc->ps_fdt;  
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        return EBADF;
    }
    
    rw_wlock(fc->fc_rw);
    
    myuio.uio_iov = (struct iovec *)buf;
    myuio.uio_iovcnt = 1;
    myuio.uio_offset = fc->fc_offset;
    myuio.uio_resid = buflen;
    myuio.uio_segflg = UIO_USERSPACE;
	myuio.uio_rw = UIO_READ;
	myuio.uio_uio_space = curthread->t_proc->ps_addrspace;
    
    result = VOP_READ(fc->fc_vnode, &myuio);
    if (result) {
        *err = result;
        return -1;
    }
    
    amount_read = myuio.uio_offset - fc->fc_offset;
    fc->offset = myuio.uio_offset;
    
    rw_wdone(fc->fc_rw);
    
    return amount_read;
}

// Error stored in err
int
write(int fd, const void *buf, size_t count, int *err)
{
    struct file_table *fdt;
    struct file_ctxt *fc;
    struct uio myuio;
    int result, amount_written;
    
    fdt = curthread->t_proc->ps_fdt;  
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        return EBADF;
    }
    
    rw_wlock(fc->fc_rw);
    
    myuio.uio_iov = (struct iovec *)buf;
    myuio.uio_iovcnt = 1;
    myuio.uio_offset = fc->fc_offset;
    myuio.uio_resid = count;
    myuio.uio_segflg = UIO_USERSPACE;
	myuio.uio_rw = UIO_WRITE;
	myuio.uio_uio_space = curthread->t_proc->ps_addrspace;
    
    result = VOP_WRITE(fc->fc_vnode, &myuio);
    if (result) {
        *err = result;
        return -1;
    }
    
    amount_written = myuio.uio_offset - fc->fc_offset;
    fc->offset = myuio.uio_offset;
    
    rw_wdone(fc->fc_rw);
    
    return amount_written;
}

// Error stored in err
off_t 
lseek(int fd, off_t offset, int whence, int *err)
{
    struct file_table *fdt;
    struct file_ctxt *fc;
    struct stat *statbuf;
    off_t new_offset;
    
    fdt = curthread->t_proc->ps_fdt; 
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        return EBADF;
    }
    
    rw_wlock(fc->fc_rw);
    
    VOP_STAT(fc->vnode, statbuf);
    
    switch(whence){
        case SEEK_SET:
            new_offset = offset;
        case SEEK_CUR:
            new_offset = offset + fc_offset;
        case SEEK_END
            new_offset = statbuf.st_size - offset;
        default:
            *err = EINVAL;
            return (off_t) -1;
    }
    
    if (new_offset > statbuf.st_size)
    {
        *err = EINVAL;
        return (off_t) -1;
    }
    else if (new_offset < 0)
    {
        *err = EINVAL;
        return (off_t) -1;
    }
    else
    fc->fc_offset = new_offset;
    
    rw_wdone(fc->fc_rw);
    
    return new_offset;
    
}