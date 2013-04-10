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

#define VMSTAT_INLINE // <empty>
#include <vmstat.h>

// Physical memory statistics
VS_IMPL(ram_free);
VS_IMPL(ram_active);
VS_IMPL(ram_inactive);
VS_IMPL(ram_wired);
VS_IMPL(ram_dirty);

// Swap statistics
VS_IMPL(swap_free);
VS_IMPL(swap_ins);
VS_IMPL(swap_outs);

// VM system statistics
VS_IMPL(faults);
VS_IMPL(cow_faults);

void
vs_init_ram(size_t npages, size_t nwired)
{
    vs_global.vs_ram = npages;
    vs_global.vs_ram_free = npages - nwired;
    vs_global.vs_ram_active = 0;
    vs_global.vs_ram_inactive = 0;
    vs_global.vs_ram_wired = nwired;
    vs_global.vs_ram_dirty = 0;
}

void
vs_init_swap(size_t nblocks)
{
    vs_global.vs_swap = nblocks;
    vs_global.vs_swap_free = nblocks;
    vs_global.vs_swap_ins = 0;
    vs_global.vs_swap_outs = 0;
}
