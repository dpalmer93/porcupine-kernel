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
#include <machine/vm.h>
#include <kern/fcntl.h>
#include <spinlock.h>
#include <wchan.h>
#include <uio.h>
#include <vnode.h>
#include <vfs.h>
#include <stat.h>
#include <vmstat.h>
#include <swap.h>

static struct vnode     *swap_vnode;
static struct bitmap    *swap_map;
static struct spinlock   swap_lock;
static struct wchan     *swap_wchan;

void
swap_bootstrap(void)
{
    int err;
    
    // use lhd0 (RAW) for swap
    kprintf("Swap disk: lhd0\n\n");
    char swapnode_path[9] = "lhd0raw:";
    err = vfs_open(swapnode_path, O_RDWR, 0, &swap_vnode);
    if (err)
        panic("swap_bootstrap: %s\n", strerror(err));
    
    // determine size of disk
    struct stat swap_stat;
    err = VOP_STAT(swap_vnode, &swap_stat);
    if (err)
        panic("swap_bootstrap: %s\n", strerror(err));
    
    swap_map = bitmap_create(swap_stat.st_size / PAGE_SIZE);
    if (swap_map == NULL)
        panic("swap_bootstrap: Out of memory.\n");
    
    swap_wchan = wchan_create("Swap Wait Channel");
    if (swap_wchan == NULL)
        panic("swap_bootstrap: Out of memory.\n");
    
    spinlock_init(&swap_lock);
    
    // set up statistics
    vs_init_swap(swap_stat.st_size / PAGE_SIZE);
}

int
swap_get_free(swapidx_t *freeblk)
{
    int err;
    
    // use the bitmap to find a free block
    // we need a spinlock here: since this gets
    // called while holding the core map spinlock,
    // it cannot block
    spinlock_acquire(&swap_lock);
    err = bitmap_alloc(swap_map, freeblk);
    spinlock_release(&swap_lock);
    
    // update statistics
    if (!err)
        vs_decr_swap_free();
    
    return err;
}

void
swap_free(swapidx_t to_free)
{
    spinlock_acquire(&swap_lock);
    bitmap_unmark(swap_map, to_free);
    spinlock_release(&swap_lock);
    
    // update statistics
    vs_incr_swap_free();
}

int
swap_in(swapidx_t src, paddr_t dst)
{
    // update statistics
    vs_incr_swap_ins();
    
    // set up UIO
    struct iovec swapin_iov;
    struct uio swapin_uio;
    
    swapin_iov.iov_kbase = (void *)PADDR_TO_KVADDR(dst);
    swapin_iov.iov_len = PAGE_SIZE;
    
    swapin_uio.uio_iov = &swapin_iov;
    swapin_uio.uio_iovcnt = 1;
    swapin_uio.uio_offset = (off_t)src * PAGE_SIZE;
    swapin_uio.uio_resid = PAGE_SIZE;
    swapin_uio.uio_segflg = UIO_SYSSPACE;
    swapin_uio.uio_rw = UIO_READ;
    swapin_uio.uio_space = NULL;
    
    int err = VOP_READ(swap_vnode, &swapin_uio);
    
    // wake anyone who is waiting for a swap-in
    wchan_wakeall(swap_wchan);
    return err;
}

int
swap_out(paddr_t src, swapidx_t dst)
{
    // update statistics
    vs_incr_swap_outs();
    
    // set up UIO
    struct iovec swapout_iov;
    struct uio swapout_uio;
    
    swapout_iov.iov_kbase = (void *)PADDR_TO_KVADDR(src);
    swapout_iov.iov_len = PAGE_SIZE;
        
    swapout_uio.uio_iov = &swapout_iov;
    swapout_uio.uio_iovcnt = 1;
    swapout_uio.uio_offset = (off_t)dst * PAGE_SIZE;
    swapout_uio.uio_resid = PAGE_SIZE;
    swapout_uio.uio_segflg = UIO_SYSSPACE;
    swapout_uio.uio_rw = UIO_WRITE;
    swapout_uio.uio_space = NULL;
    
    return VOP_WRITE(swap_vnode, &swapout_uio);
}

void
swap_wait_lock(void)
{
    wchan_lock(swap_wchan);
}

void
swap_wait(void)
{
    wchan_sleep(swap_wchan);
}

void
swap_wait_unlock(void)
{
    wchan_unlock(swap_wchan);
}
