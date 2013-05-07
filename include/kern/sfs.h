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

#ifndef _KERN_SFS_H_
#define _KERN_SFS_H_


/*
 * SFS definitions visible to userspace. This covers the on-disk format
 * and is used by tools that work on SFS volumes, such as mksfs.
 */

#define SFS_MAGIC         0xabadf001    /* magic number identifying us */
#define SFS_BLOCKSIZE     512           /* size of our blocks */
#define SFS_VOLNAME_SIZE  32            /* max length of volume name */
#define SFS_NDIRECT       15            /* # of direct blocks in inode */
#define SFS_DBPERIDB      128           /* # direct blks per indirect blk */
#define SFS_JNLFRACTION   10            /* # disk blks per journal blk */
#define SFS_NAMELEN       60            /* max length of filename */
#define SFS_DIRLEN        64            /* length of directory entry in bytes */
#define SFS_DIRPERBLK     8             /* number of directory entries per block */
#define SFS_SB_LOCATION   0             /* block the superblock lives in */
#define SFS_ROOT_LOCATION 1             /* loc'n of the root dir inode */
#define SFS_MAP_LOCATION  2             /* 1st block of the freemap */
#define SFS_NOINO         0             /* inode # for free dir entry */
#define SFS_JE_SIZE       128           /* journal entry size in bytes */
#define SFS_JE_PER_BLOCK  4             /* # of journal entries per disk block */

/* Number of bits in a block */
#define SFS_BLOCKBITS (SFS_BLOCKSIZE * CHAR_BIT)

/* Utility macro */
#define SFS_ROUNDUP(a,b)       ((((a)+(b)-1)/(b))*b)

/* Size of bitmap (in bits) */
#define SFS_BITMAPSIZE(nblocks) SFS_ROUNDUP(nblocks, SFS_BLOCKBITS)

/* Size of bitmap (in blocks) */
#define SFS_BITBLOCKS(nblocks)  (SFS_BITMAPSIZE(nblocks)/SFS_BLOCKBITS)

/* Size of journal (in blocks) */
#define SFS_JNLSIZE(nblocks) ((nblocks)/SFS_JNLFRACTION)

/* Start of journal */
#define SFS_JNLSTART(nblocks) ((nblocks) - SFS_JNLSIZE(nblocks))

/* File types for sfi_type */
#define SFS_TYPE_INVAL    0       /* Should not appear on disk */
#define SFS_TYPE_FILE     1
#define SFS_TYPE_DIR      2



/*
 * On-disk superblock
 */
struct sfs_super {
	uint32_t sp_magic;                  // Magic number, should be SFS_MAGIC
	uint32_t sp_nblocks;                // Number of blocks in fs
    uint32_t sp_ckpoint;                // Last journal checkpoint
    uint32_t sp_clean;                  // Was cleanly unmounted
    uint64_t sp_txnid;                  // Last transaction ID
	char sp_volname[SFS_VOLNAME_SIZE];	// Name of this volume
	uint32_t reserved[114];
};

/*
 * On-disk inode
 */
struct sfs_inode {
	uint32_t sfi_size;			/* Size of this file (bytes) */
	uint16_t sfi_type;			/* One of SFS_TYPE_* above */
	uint16_t sfi_linkcount;			/* # hard links to this file */
	uint32_t sfi_direct[SFS_NDIRECT];	/* Direct blocks */
	uint32_t sfi_indirect;			/* Indirect block */
	uint32_t sfi_dindirect;   /* Double indirect block */
	uint32_t sfi_tindirect;   /* Triple indirect block */
	uint32_t sfi_waste[128-5-SFS_NDIRECT];	/* unused space, set to 0 */
};

/*
 * On-disk directory entry
 */
struct sfs_dir {
	uint32_t sfd_ino;			/* Inode number */
	char sfd_name[SFS_NAMELEN];		/* Filename */
};


typedef enum {
    JE_INVAL = 0,       // invalid journal entry
    JE_START,           // first journal entry in a transaction
    JE_ABORT,           // last journal entry in a failed transaction
    JE_COMMIT,          // last journal entry in a successful transaction
    // Add data block je_childblk to inode je_ino.
    // It is the je_slot word in inode.
    JE_ADD_DATABLOCK_INODE,
    // Add data block je_childblk to indirect block je_parentblk.
    // It is the je_slot pointer in indirect.
    JE_ADD_DATABLOCK_INDIRECT,
    // Allocated a new inode block at block je_ino, with inumber je_ino, and type je_inotype
    JE_NEW_INODE,
    // Write je_dir into je_slot of the directory with inumber je_ino
    JE_WRITE_DIR,
    // Remove inode at block je_ino, with inumber je_ino
    JE_REMOVE_INODE,
    // Remove data block je_childblk from inode je_ino.  It is the je_slot word in the inode.
    JE_REMOVE_DATABLOCK_INODE,
    // Remove data block je_childblk from indirect block je_parentblk.
    // It is in the je_slot pointer in indirect.
    JE_REMOVE_DATABLOCK_INDIRECT,
    // Set the size of the file with inumber je_ino to je_size
    JE_SET_SIZE,
    // Set the linkcount of the file with inumber je_ino to je_linkcount
    JE_SET_LINKCOUNT
} je_type_t;

// 128 Bytes
struct jnl_entry {
    je_type_t           je_type; // type of operation
    uint64_t            je_txnid;
    uint32_t            je_ino;
    daddr_t             je_parentblk;
    daddr_t             je_childblk;
    int                 je_slot;
    uint32_t            je_size;
    uint16_t            je_inotype;
    uint16_t            je_linkcount;
    struct sfs_dir      je_dir;
    uint8_t             je_padding[SFS_JE_SIZE - 100];
} __attribute__((__packed__));


#endif /* _KERN_SFS_H_ */
