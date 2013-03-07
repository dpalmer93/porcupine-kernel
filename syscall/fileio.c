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
#include <copyinout.h>
#include <current.h>
#include <stat.h>
#include <kern/seek.h>
#include <vfs.h>

// Error stored in err
int
sys_open(const_userptr_t filename, int flags, int* err)
{
    int result;
    size_t got;
    char kfilename[PATH_MAX];
    struct vnode *file;
    struct file_ctxt *fc;
    struct fd_table *fdt;
    
    // copy in filename to kernel space
    result = copyinstr(filename, kfilename, PATH_MAX, &got);
    if (result){
        *err = result;
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
        fc_close(fc);
        *err = EMFILE;
        return -1;
    }
    
    return result;
    
}

// Error is returned
int
sys_close(int fd)
{
    struct fd_table *fdt;
    struct file_ctxt *fc;

    fdt = curthread->t_proc->ps_fdt;
    
    fc = fdt_remove(fdt, fd);
    if (fc == NULL) {
        return EBADF;
    }
    
    fc_close(fc);
    return 0;
}

int
sys_dup2(int old_fd, int new_fd, int *err)
{
    struct fd_table *fdt;
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
    
    
    *err = fdt_replace(fdt, new_fd, fc);
    if (*err)
        return -1;
    
    fc_incref(fc);
    
    return new_fd;
    
}

// Error stored in err
int
sys_read(int fd, userptr_t buf, size_t buflen, int *err)
{
    struct fd_table *fdt;
    struct file_ctxt *fc;
    struct uio myuio;
    struct iovec uio_iov;
    int result, amount_read;
    
    fdt = curthread->t_proc->ps_fdt;  
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        *err = EBADF;
        return -1;
    }
    
    // set up iovec
    uio_iov.iov_ubase = buf;
    uio_iov.iov_len = buflen;
    // set up uio
    myuio.uio_iov = &uio_iov;
    myuio.uio_iovcnt = 1;
    myuio.uio_offset = fc->fc_offset;
    myuio.uio_resid = buflen;
    myuio.uio_segflg = UIO_USERSPACE;
	myuio.uio_rw = UIO_READ;
	myuio.uio_space = curthread->t_proc->ps_addrspace;
    
    lock_acquire(fc->fc_lock);
    result = VOP_READ(fc->fc_vnode, &myuio);
    if (result) {
        lock_release(fc->fc_lock);
        *err = result;
        return -1;
    }
    
    amount_read = myuio.uio_offset - fc->fc_offset;
    fc->fc_offset = myuio.uio_offset;
    
    lock_release(fc->fc_lock);
    
    return amount_read;
}

// Error stored in err
int
sys_write(int fd, const_userptr_t buf, size_t count, int *err)
{
    struct fd_table *fdt;
    struct file_ctxt *fc;
    struct uio myuio;
    struct iovec uio_iov;
    int result, amount_written;
    
    fdt = curthread->t_proc->ps_fdt;  
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL) {
        *err = EBADF;
        return -1;
    }
    
    // set up iovec
    uio_iov.iov_ubase = (userptr_t)buf;
    uio_iov.iov_len = count;
    // set up uio
    myuio.uio_iov = &uio_iov;
    myuio.uio_iovcnt = 1;
    myuio.uio_offset = fc->fc_offset;
    myuio.uio_resid = count;
    myuio.uio_segflg = UIO_USERSPACE;
	myuio.uio_rw = UIO_WRITE;
	myuio.uio_space = curthread->t_proc->ps_addrspace;
    
    lock_acquire(fc->fc_lock);
    result = VOP_WRITE(fc->fc_vnode, &myuio);
    if (result) {
        lock_release(fc->fc_lock);
        *err = result;
        return -1;
    }
    
    amount_written = myuio.uio_offset - fc->fc_offset;
    fc->fc_offset = myuio.uio_offset;
    
    lock_release(fc->fc_lock);
    
    return amount_written;
}

// Error stored in err
off_t
sys_lseek(int fd, off_t offset, int whence, int *err)
{
    struct fd_table *fdt;
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
    
    // stat the file to find the type and length
    VOP_STAT(fc->fc_vnode, &statbuf);
    
    // discard bottom 12 bits of mode
    mode_t ftype = statbuf.st_mode & S_IFMT;
    
    // make sure we can seek on this file
    switch (ftype)
    {
        case S_IFIFO:  // FIFO
        case S_IFSOCK:  // socket
        case S_IFCHR:   // character device
        case S_IFBLK: // block device
            lock_release(fc->fc_lock);
            *err = ESPIPE;
            return (off_t)-1;
        default:
            break;
    }
    
    // determine the new offset
    switch(whence)
    {
        case SEEK_SET: // offset + 0
            new_offset = offset;
            break;
        case SEEK_CUR: // offset + current offset
            new_offset = offset + fc->fc_offset;
            break;
        case SEEK_END: // offset + file length
            new_offset = offset + statbuf.st_size;
            break;
        default:
            lock_release(fc->fc_lock);
            *err = EINVAL;
            return (off_t)-1;
    }
    
    // check for integer overflow or negative offset
    if (new_offset < 0)
    {
        lock_release(fc->fc_lock);
        *err = EINVAL;
        return (off_t)-1;
    }
    
    fc->fc_offset = new_offset;
    
    lock_release(fc->fc_lock);
    
    return new_offset;
    
}

int
sys_fstat(int fd, userptr_t statbuf)
{
    int err;
    struct stat kstatbuf;
    struct fd_table *fdt;
    struct file_ctxt *fc;
    
    fdt = curthread->t_proc->ps_fdt;
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL)
        return EBADF;
    
    lock_acquire(fc->fc_lock);
    // call VFS to get the stats
    VOP_STAT(fc->fc_vnode, &kstatbuf);
    
    lock_release(fc->fc_lock);
    
    // copy the stat to the user buffer
    err = copyout(&kstatbuf, statbuf, sizeof(struct stat));
    if (err)
        return err;
    
    return 0;
}
