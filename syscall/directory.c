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
#include <process.h>
#include <machine/trapframe.h>
#include <syscall.h>
#include <uio.h>
#include <current.h>
#include <vfs.h>
#include <copyinout.h>

int
sys_getdirentry(int fd, userptr_t buf, size_t buflen, int *err)
{
    struct fd_table *fdt;
    struct file_ctxt *fc;
    struct uio myuio;
    struct iovec uio_iov;
    int result;
    
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
    result = VOP_GETDIRENTRY(fc->fc_vnode, &myuio);
    if (result) {
        lock_release(fc->fc_lock);
        *err = result;
        return -1;
    }
    
    // update the offset...the offset "indexes" into the directory
    fc->fc_offset = myuio.uio_offset;
    
    lock_release(fc->fc_lock);
    
    // return length of directory entry name
    // VOP_GETDIRENTRY uses uio_offset as the directory
    // entry number, so we must use uio_resid to get the
    // number of bytes read.
    return buflen - myuio.uio_resid;
}

int
sys___getcwd(userptr_t buf, size_t buflen, int *err)
{
    struct uio myuio;
    struct iovec uio_iov;
    int result;
    
    uio_iov.iov_ubase = buf;
    uio_iov.iov_len = buflen;
    
    myuio.uio_iov = &uio_iov;
    myuio.uio_iovcnt = 1;
    myuio.uio_offset = 0;
    myuio.uio_resid = buflen;
    myuio.uio_segflg = UIO_USERSPACE;
	myuio.uio_rw = UIO_READ;
	myuio.uio_space = curthread->t_proc->ps_addrspace;
    
    result = vfs_getcwd(&myuio);
    if (result)
    {
        *err = result;
        return -1;
    }
        
    return myuio.uio_offset;

}

int
sys_chdir(const_userptr_t pathname, int *err)
{
    char kpathname[PATH_MAX];
    int result;
    size_t got;
    
    result = copyinstr(pathname, kpathname, PATH_MAX, &got);
    if (result){
        *err = result;
        return -1;
    }
    
    result = vfs_chdir(kpathname);
    if (result) {
        *err = result;
        return -1;
    }
    
    return 0;
}
