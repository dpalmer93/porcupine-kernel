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
 * File-related system call implementations.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <limits.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <syscall.h>
#include <fdt.h>
#include <process.h>

/*
 * sync - call vfs_sync
 */
int
sys_sync(void)
{
	int err;

	err = vfs_sync();
	if (err==EIO) {
		/* This is the only likely failure case */
		kprintf("Warning: I/O error during sync\n");
	}
	else if (err) {
		kprintf("Warning: sync: %s\n", strerror(err));
	}
	/* always succeed */
	return 0;
}

/*
 * mkdir - call vfs_mkdir
 */
int
sys_mkdir(userptr_t path, mode_t mode)
{
	char pathbuf[PATH_MAX];
	int err;

	(void) mode;

	err = copyinstr(path, pathbuf, sizeof(pathbuf), NULL);
	if (err) {
		return err;
	}
	else {
		return vfs_mkdir(pathbuf, mode);
	}
}

/*
 * rmdir - call vfs_rmdir
 */
int
sys_rmdir(userptr_t path)
{
	char pathbuf[PATH_MAX];
	int err;

	err = copyinstr(path, pathbuf, sizeof(pathbuf), NULL);
	if (err) {
		return err;
	}
	else {
		return vfs_rmdir(pathbuf);
	}
}

/*
 * rename - call vfs_rename
 */
int
sys_rename(userptr_t oldpath, userptr_t newpath)
{
	char *oldbuf;
	char *newbuf;
	int err;

	oldbuf = kmalloc(PATH_MAX);
	if (oldbuf == NULL) {
		return ENOMEM;
	}

	newbuf = kmalloc(PATH_MAX);
	if (newbuf == NULL) {
		kfree(oldbuf);
		return ENOMEM;
	}

	err = copyinstr(oldpath, oldbuf, PATH_MAX, NULL);
	if (err) {
		goto fail;
	}

	err = copyinstr(newpath, newbuf, PATH_MAX, NULL);
	if (err) {
		goto fail;
	}

	err = vfs_rename(oldbuf, newbuf);
 fail:
	kfree(newbuf);
	kfree(oldbuf);
	return err;
}

/*
 * fsync - call VOP_FSYNC
 */
int
sys_fsync(int fd)
{
    struct fd_table *fdt;
    struct file_ctxt *fc;
    
    fdt = curthread->t_proc->ps_fdt;
    
    fc = fdt_get(fdt, fd);
    if (fc == NULL)
        return EBADF;

	/*
	 * No need to lock the openfile - it cannot disappear under us,
	 * and we're not using any of its non-constant fields.
	 */
     
	return VOP_FSYNC(fc->fc_vnode);

}
