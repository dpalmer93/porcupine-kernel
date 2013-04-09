/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008
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

#ifndef _KERN_VMSTAT_H_
#define _KERN_VMSTAT_H_

/*
 * The vmstat structure, for returning VM statistics via vmstat().
 */
struct vmstat {
    // Physical memory statistics
    int vs_ram;         // # of physical pages
    int vs_ram_free;    // # of free pages
    int vs_ram_active;  // # of pages in use
    int vs_ram_wired;   // # of pages that cannot be evicted
    int vs_ram_dirty;   // # of dirty pages
    
    // Swap statistics
    int vs_swap;        // # of swap blocks
    int vs_swap_free;   // # of available swap blocks
    int vs_swap_ins;    // # of page-ins since boot
    int vs_swap_outs;   // # of page-outs since boot
    
    // VM system statistics
    int vs_faults;      // # of times vm_fault() was called
    int vs_cow_faults;  // # of faults requiring copy-on-write
};

#endif /* _KERN_VMSTAT_H_ */
