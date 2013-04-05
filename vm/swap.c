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

#include <machine/vm.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <stat.h>
#include <swap.h>

static struct vnode     *swap_vnode;
static struct bitmap    *swap_map;
static struct lock      *swap_lock;

void
swap_bootstrap(void)
{
    int err;
    
    // use lhd0 (RAW) for swap
    err = vfs_open("lhd0raw:", O_RDWR, 0, swap_vnode)
    if (err)
        panic("swap_bootstrap: %s\n", strerr(err));
    
    // determine size of disk
    struct stat swap_stat;
    err = VOP_STAT(swap_vnode, &swap_stat)
    if (err)
        panic("swap_bootstrap: %s\n", strerr(err));
    
    swap_map = bitmap_create(swap_stat.st_size / PAGE_SIZE);
    if (swap_map == NULL)
        panic("swap_bootstrap: Out of memory.\n");
    
    swap_lock = lock_create("Swap Lock");
    if (swap_lock == NULL)
        panic("swap_bootstrap: Out of memory.\n");
}

int
swap_get_free(swapidx_t *freeblk)
{
    int err;
    
    // use the bitmap to find a free block
    lock_acquire(swap_lock);
    err = bitmap_alloc(swap_map, freeblk);
    lock_release(swap_lock);
    
    return err;
}

void
swap_free(swapidx_t to_free)
{
    lock_acquire(swap_lock);
    bitmap_unmark(swap_map, to_free);
    lock_release(swap_lock);
}

int
swap_in(swapidx_t src, paddr_t dst)
{
    // set up UIO
    struct iovec swapin_iov = {
        .iov_ubase = PADDR_TO_KVADDR(dst),
        .iov_len = PAGE_SIZE
    };
    struct uio swapin_uio = {
        .uio_iov = &swap_iov,
        .uio_iovcnt = 1,
        .uio_offset = (off_t)src * PAGE_SIZE,
        .uio_resid = PAGE_SIZE,
        .uio_segflg = UIO_SYSSPACE,
        .uio_rw = UIO_READ,
        .uio_space = NULL
    }
    
    return VOP_READ(swap_vnode, swapin_uio);
}

int
swap_out(paddr_t src, swapidx_t dst)
{
    // set up UIO
    struct iovec swapout_iov = {
        .iov_ubase = PADDR_TO_KVADDR(src),
        .iov_len = PAGE_SIZE
    };
    struct uio swapout_uio = {
        .uio_iov = &swap_iov,
        .uio_iovcnt = 1,
        .uio_offset = (off_t)dst * PAGE_SIZE,
        .uio_resid = PAGE_SIZE,
        .uio_segflg = UIO_SYSSPACE,
        .uio_rw = UIO_WRITE,
        .uio_space = NULL
    }
    
    return VOP_READ(swap_vnode, swapout_uio);
}
