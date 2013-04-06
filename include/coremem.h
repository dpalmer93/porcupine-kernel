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

#ifndef _COREMEM_H_
#define _COREMEM_H_

/*
 * Physical memory management operations:
 *
 * core_bootstrap - set up the core map (must be called after ram_bootstrap())
 *
 * core_acquire_frame - find and lock a free page frame for manipulation.
 *
 * core_release_frame - release a locked page frame after manipulating it.
 *
 * core_map_frame - map a page frame to a PTE and swap block
 *                  (must hold the page frame lock, i.e., have called
 *                  core_acquire_frame)
 *
 * core_reserve_frame - reserve a frame for kernel use.  Thereafter,
 *                  until the frame is freed, the frame's contents cannot be
 *                  evicted.
 *
 * core_free_frame - indicate that a page frame is no longer being used
 */
void    core_bootstrap(void);
paddr_t core_acquire_frame(void);
void    core_release_frame(paddr_t frame);
void    core_map_frame(paddr_t frame, struct pt_entry *pte, swapidx_t swapblk);
void    core_reserve_frame(paddr_t frame);
void    core_free_frame(paddr_t frame);

// start core cleaner thread
void core_cleaner_bootstrap(void);

#endif /* _COREMEM_H_ */
