#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/fat.h"
#include "threads/synch.h"
#include <stdio.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk {
	disk_sector_t start;                /* First data sector. */
	off_t length;                       /* File size in bytes. */
	unsigned magic;                     /* Magic number. */
	uint32_t unused[125];               /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
	struct list_elem elem;              /* Element in inode list. */
	disk_sector_t sector;               /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
	struct lock extend_lock;
};

static bool
get_disk_inode(const struct inode *inode, struct inode_disk *inode_disk){
	disk_read(filesys_disk, inode->sector, inode_disk);
	return true;
}

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
// byte_to_sector (const struct inode *inode, off_t pos) {
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);
	// printf("pos : %d, inode start : %d\n", pos, inode -> data.start);
	if (pos < inode->data.length)
		return inode->data.start + pos / DISK_SECTOR_SIZE;
	else
		return -1;
}

// static disk_sector_t
// // byte_to_sector (const struct inode *inode, off_t pos) {
// byte_to_sector (const struct inode *inode, off_t pos) {
// 	ASSERT (inode != NULL);
// 	printf("pos : %d, inode start : %d\n", pos, inode -> data.start);
// 	// printf("fatget161 : %d\n", fat_get(2));
// 	if (pos < inode->data.length) {
// 		cluster_t cluster = inode -> data.start;
// 		for (int i = 0; i < pos / DISK_SECTOR_SIZE; i++) {
// 			cluster = fat_get(cluster);
// 		}
// 		// printf("cluster : %d\n", cluster_to_sector(cluster));
// 		return cluster_to_sector(cluster);
// 	}
// 	else
// 		return -1;
// }

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
disk_sector_t
inode_create (disk_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;
	disk_sector_t start;

	ASSERT (length >= 0);
	// printf("push\n");

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		cluster_t cluster;
		size_t sectors = bytes_to_sectors (length);
		static char zeros[DISK_SECTOR_SIZE];
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;

		// printf("sector, sectors, root sector : %d, %d, %d\n", sector, sectors, cluster_to_sector(sector));

		cluster = fat_create_chain(0);
		// printf("cluster: %d\n", cluster);
		disk_write(filesys_disk, cluster_to_sector(cluster), zeros);
		disk_inode -> start = cluster_to_sector(cluster);
		// printf("disk inode start: %d\n", disk_inode->start);

		disk_write(filesys_disk, sector, disk_inode);
 
		for(int i=1; i<sectors; i++){
			static char zeros[DISK_SECTOR_SIZE];
			cluster=fat_create_chain(cluster);
			// printf("cluster : %d, sector: %d\n", cluster, cluster_to_sector(cluster));
			disk_write(filesys_disk, cluster_to_sector(cluster), zeros);
			// if(i==0){
			// 	disk_inode->start=cluster_to_sector(cluster);
			// }
		}
		success=true;
		start=disk_inode->start;

		free (disk_inode);
	}
	// printf("succc : %d, %d\n", success, start);

	return start;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;
	// printf("inode open: %d\n", (int)sector);

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			// printf("inode reopen\n");
			return inode; 
		}
	}

	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	lock_init(&inode->extend_lock);
	disk_read (filesys_disk, inode->sector, &inode->data);
	// printf("inode_open inode: %d, length: %d\n", inode->sector, inode_length(inode));
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed) {
			// printf("fat remove chain\n");
			fat_remove_chain(inode->data.start, 0);
			fat_remove_chain(inode->sector, 0);
		}

		free (inode); 
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	// printf("inode remove\n");
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode/*inode*/, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE+1);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
			// pc_read(sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

static bool
inode_update_file_length(struct inode *inode, off_t start_pos, off_t end_pos){
	cluster_t cluster;
	static char zeros [DISK_SECTOR_SIZE];
	size_t sectors=bytes_to_sectors(end_pos);
	// printf("sectors: %d\n", sectors);
	struct inode_disk inode_disk=inode->data;

	inode_disk.length=end_pos;

	cluster=fat_create_chain(start_pos);
	printf("start pos: %d, cluster: %d\n", start_pos, cluster);
	disk_write(filesys_disk, cluster_to_sector(cluster), zeros);
	inode_disk.start = cluster_to_sector(cluster);

	printf("disk inode sector: %d, inode disk length: %d\n", inode->sector, inode_disk.length);
	disk_write(filesys_disk, inode->sector, &inode_disk);
	// disk_write(filesys_disk, start_pos, &inode_disk);

	for(int i=1; i<sectors; i++){
		cluster=fat_create_chain(cluster);
		printf("cluster : %d, sector: %d\n", cluster, cluster_to_sector(cluster));
		disk_write(filesys_disk, cluster_to_sector(cluster), zeros);
	}

	printf("disk read inode sector: %d\n", inode->sector);
	disk_read (filesys_disk, inode->sector, &inode->data);
	

	printf("inode disk length: %d, %d\n", inode_disk.length, inode_length(inode));
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	// printf("inode write at : %d, %d\n", size, offset);
	if (inode->deny_write_cnt)
		return 0;

	// lock_acquire(&inode->extend_lock);

	int old_length=inode_length(inode);
	// int write_end=offset+size-1;
	// printf("inode write at inode length: %d\n", old_length);

	// if(write_end>old_length-1){
	// 	printf("extensible file\n");
	// 	inode_update_file_length(inode, offset, offset+size);
	// }

	// lock_release(&inode->extend_lock);

	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		// printf("sector_idx : %d\n", sector_idx);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		// printf("inode length: %d, write: %d\n", inode_length(inode), (int)sector_idx);
		// printf("inode left, sector left, min left : %d, %d, %d\n", inode_left, sector_left, min_left);

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		// printf("chunk size : %d, %d, %d\n", chunk_size, DISK_SECTOR_SIZE, sector_ofs);
		if (chunk_size <= 0)
			break;
		// printf("yogi : %d, %d\n", sector_ofs, chunk_size);
		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			// printf("else\n");
			/* We need a bounce buffer. */
			// printf("inode : %p\n", &inode -> elem);
			if (bounce == NULL) {
				// printf("malloc\n");
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL) {
					// printf("malloc error\n");
					break;
				}
			}
			// printf("malloc complete\n");
			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
			// printf("else finish\n");
			// pc_write(sector_idx, buffer, bytes_written, chunk_size, sector_ofs);

		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);

	// printf("bytes written: %d\n", bytes_written);

	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}
