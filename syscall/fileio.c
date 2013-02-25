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
#include <kern/errno.h>
#include <uio.h>
#include <process.h>
#include <syscall.h>


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
        
    result = vfs_open(kfilename, flags, 0, &file);
    if (result) {
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
    
    fc = fdt_remove(fdt, fd);
    if (fc == NULL) {
        return EBADF;
    }
    
    fc_close(fc);
    return 0;
}

// Error stored in err
int
sys_read(int fd, userptr_t buf, size_t buflen, int *err)
{
    struct file_table *fdt;
    struct file_ctxt *fc;
    struct uio myuio;
    int result, amount_read;
    
    fdt = curthread->t_proc->ps_fdt;  
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        *err = EBADF;
        return -1;
    }
    
    lock_acquire(fc->fc_lock);
    
    myuio.uio_iov = {
        iov_ubase = buf;
        iov_len = buflen;
    };
    myuio.uio_iovcnt = 1;
    myuio.uio_offset = fc->fc_offset;
    myuio.uio_resid = buflen;
    myuio.uio_segflg = UIO_USERSPACE;
	myuio.uio_rw = UIO_READ;
	myuio.uio_space = curthread->t_proc->ps_addrspace;
    
    result = VOP_READ(fc->fc_vnode, &myuio);
    if (result) {
        *err = result;
        return -1;
    }
    
    amount_read = myuio.uio_offset - fc->fc_offset;
    fc->offset = myuio.uio_offset;
    
    lock_release(fc->fc_lock);
    
    return amount_read;
}

// Error stored in err
int
sys_write(int fd, const_userptr_t buf, size_t count, int *err)
{
    struct file_table *fdt;
    struct file_ctxt *fc;
    struct uio myuio;
    int result, amount_written;
    
    fdt = curthread->t_proc->ps_fdt;  
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        *err = EBADF;
        return -1;
    }
    
    lock_acquire(fc->fc_lock);
    
    myuio.uio_iov = {
        iov_ubase = buf;
        iov_len = buflen;
    };
    myuio.uio_iovcnt = 1;
    myuio.uio_offset = fc->fc_offset;
    myuio.uio_resid = count;
    myuio.uio_segflg = UIO_USERSPACE;
	myuio.uio_rw = UIO_WRITE;
	myuio.uio_space = curthread->t_proc->ps_addrspace;
    
    result = VOP_WRITE(fc->fc_vnode, &myuio);
    if (result) {
        *err = result;
        return -1;
    }
    
    amount_written = myuio.uio_offset - fc->fc_offset;
    fc->offset = myuio.uio_offset;
    
    lock_release(fc->fc_lock);
    
    return amount_written;
}

// Error stored in err
off_t 
sys_lseek(int fd, off_t offset, int whence, int *err)
{
    struct file_table *fdt;
    struct file_ctxt *fc;
    struct stat statbuf;
    off_t new_offset;
    
    fdt = curthread->t_proc->ps_fdt; 
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        *err = EBADF;
        return (off_t)-1;
    }
    
    lock_acquire(fc->fc_lock);
    
    VOP_STAT(fc->vnode, &statbuf);
    
    switch(whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = offset + fc_offset;
            break;
        case SEEK_END
            new_offset = statbuf.st_size + offset;
            break;
        default:
            *err = EINVAL;
            lock_release(fc->fc_lock);
            return (off_t)-1;
    }
    
    if (new_offset < 0)
    {
        *err = EINVAL;
        lock_release(fc->fc_lock);
        return (off_t)-1;
    }
    
    fc->fc_offset = new_offset;
    
    lock_release(fc->fc_lock);
    
    return new_offset;
    
}

int
sys_dup2(int old_fd, int new_fd, int *err)
{
    struct file_table *fdt;
    struct file_ctxt *fc;
    
    fdt = curthread->t_proc->ps_fdt;  
    
    fc = fdt_get(fdt, old_fd);
    if (fc == NULL) {
        *err = EBADF;
        return -1;
    }
    if (new_fd < 0 || new_fd >= OPEN_MAX)
    {
        *err = EBADF;
        return -1;
    }
    
    if (new_fd == old_fd)
        return new_fd;
    
    fc_incref(fc);
    fdt_replace(fdt, new_fd, fc);
    
    return new_fd;
    
}
