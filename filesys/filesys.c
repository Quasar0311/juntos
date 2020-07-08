#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h"
#include "filesys/page_cache.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();

	inode_create(cluster_to_sector(ROOT_DIR_CLUSTER), DISK_SECTOR_SIZE);
	/*** first, root directory is for current cwd ***/
	thread_current() -> cwd = dir_open_root();
	dir_close(thread_current() -> cwd);
	// printf("root create: %d\n", cluster_to_sector(ROOT_DIR_CLUSTER));
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
	pc_writeback_all_entries();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	disk_sector_t start;
	// start = inode_create(inode_sector, initial_size);
	// printf("start : %d\n", start);
	struct dir *dir = dir_open_root ();
	// printf("dir open success\n");
	bool success = dir != NULL;
			// && free_map_allocate (1, &inode_sector)
			// && fat_create_chain(inode_sector)
			// && inode_create (inode_sector, initial_size)
			// && dir_add (dir, name, inode_sector));
			// &&dir_add(dir, name, start));
	disk_sector_t sector = cluster_to_sector(fat_create_chain(inode_sector));
	// printf("sector for disk_inode at fs_create ; %d\n", sector);
	disk_sector_t success2 = inode_create(sector, initial_size);
	bool success3 = dir_add(dir, name, sector);
	// if (!success && inode_sector != 0)
		// free_map_release (inode_sector, 1);
		// fat_remove_chain(inode_sector, 0);
	dir_close (dir);
	// printf("suc : %d, %d, %d\n", success, success2, success3);
	return success && success3;
}


bool
filesys_dir_create (const char *name) {
	disk_sector_t inode_sector = 0;
	disk_sector_t start;

	struct dir *dir = split_path(name);
	bool success = dir != NULL;

	disk_sector_t sector = cluster_to_sector(fat_create_chain(inode_sector));
	disk_sector_t success2 = dir_create(sector, 16);

	bool success3 = dir_add(dir, name, sector);

	dir_close (dir);
	return success && success3;
}

struct dir *
split_path (char *path, char *file_name) {
	struct dir *dir;
	char *save_ptr;
	struct inode *inode = NULL;

	if (path[0] == '/') {
		dir = dir_open_root();
	}
	else {
		dir = dir_reopen(thread_current() -> cwd);
	}

	path = strtok_r(path, "/", &save_ptr);
	while (path != NULL) {
		if (dir_lookup(dir, path, &inode)) {
			dir_close(dir);
			dir = dir_open(inode);
			thread_current() -> cwd = dir;
		}
		else {
			;
		}
		path = strtok_r(path, "/", &save_ptr);
	}

	return dir;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	/*** directory split ***/
	// char *token, *save_ptr;
	// char *name_copy = palloc_get_page(0);
	// memcpy(name_copy, name, strlen(name) + 1);

	// for (token == strtok_r(name_copy, "/", &save_ptr); token != NULL;
	// 		token = strtok_r(NULL, "/", &save_ptr)) {
	// 			printf("hi\n");
	// }

	// printf("filesys open\n");

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
