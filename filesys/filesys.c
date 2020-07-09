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

	inode_create(cluster_to_sector(ROOT_DIR_CLUSTER), DISK_SECTOR_SIZE, 1);
	/*** first, root directory is for current cwd ***/
	thread_current() -> cwd = dir_open_root();
	// dir_close(thread_current() -> cwd);
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
	char file_name[strlen(name) + 1];
	// start = inode_create(inode_sector, initial_size);
	// printf("\nstart : %d\n", start);
	struct dir *dir = split_path(name, file_name);
	// printf("dir open success\n");
	bool success = dir != NULL;

	disk_sector_t sector = cluster_to_sector(fat_create_chain(inode_sector));
	// printf("sector for disk_inode at fs_create ; %d\n", sector);
	disk_sector_t success2 = inode_create(sector, initial_size, 0);
	bool success3 = dir_add(dir, file_name, sector);

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
	struct dir *add_dir;

	char file_name[strlen(name) + 1];

	struct dir *dir = split_path(name, file_name);
	bool success = dir != NULL;

	// printf("file name : %s\n", file_name);

	disk_sector_t sector = cluster_to_sector(fat_create_chain(inode_sector));
	disk_sector_t success2 = dir_create(sector, 16);

	bool success3 = dir_add(dir, file_name, sector);

	add_dir = dir_open(inode_open(sector));
	dir_add(add_dir, ".", sector);
	dir_add(add_dir, "..", inode_get_inumber(dir_get_inode(dir)));
	dir_close(add_dir);

	dir_close (dir);
	// printf("dir_create\n");
	return success && success3;
}

struct dir *
split_chdir (const char *path, char *file_name) {
	struct dir *dir;
	char *token, *save_ptr, *next_token, *save_ptr2;
	char *file_token;
	struct inode *inode = NULL;
	char *path1 = palloc_get_page(0);
	char *path2 = palloc_get_page(0);

	memcpy(path1, path, strlen(path) + 1);
	memcpy(path2, path, strlen(path) + 1);

	if (path1 == NULL || file_name == NULL) {
		return NULL;
	}
	if (strlen(path1) == 0) {
		return NULL;
	}

	if (path1[0] == '/') {
		dir = dir_open_root();
	}
	else {
		dir = dir_reopen(thread_current() -> cwd);
	}

	if (!strcmp(path1, "/")) {
		memcpy(file_name, path1, sizeof(char) * (strlen(path1) + 1));
		return dir;
	}

	next_token = strtok_r(path2, "/", &save_ptr2);

	for (token = strtok_r(path1, "/", &save_ptr); token != NULL;
			token = strtok_r(NULL, "/", &save_ptr)) {
		next_token = strtok_r(NULL, "/", &save_ptr2);

		if (dir_lookup(dir, token, &inode)) {
			// printf("lookup success\n");
			if (inode_is_dir(inode)) {
				// printf("lookup success2\n");
				dir_close(dir);
				dir = dir_open(inode);
				// printf("path dir : %s, %p\n", token, dir);
			}
		}
		else {
			return NULL;
		}

		file_token = token;
	}
	memcpy(file_name, file_token, sizeof(char) * (strlen(file_token) + 1));
	palloc_free_page(path1);
	palloc_free_page(path2);

	return dir;
}

struct dir *
split_path (const char *path, char *file_name) {
	struct dir *dir;
	char *token, *save_ptr, *next_token, *save_ptr2;
	char *file_token;
	struct inode *inode = NULL;
	char *path1 = palloc_get_page(0);
	char *path2 = palloc_get_page(0);

	memcpy(path1, path, strlen(path) + 1);
	memcpy(path2, path, strlen(path) + 1);

	// if (!strcmp(path1, "tar")) printf("hi\n");

	// printf("path : %s\n", path1);

	if (path1 == NULL || file_name == NULL) {
		// printf("null?\n");
		return NULL;
	}
	if (strlen(path1) == 0) {
		return NULL;
	}

	if (path1[0] == '/') {
		dir = dir_open_root();
	}
	else {
		dir = dir_reopen(thread_current() -> cwd);
	}

	if (!strcmp(path1, "/")) {
		memcpy(file_name, path1, sizeof(char) * (strlen(path1) + 1));
		return dir;
	}

	next_token = strtok_r(path2, "/", &save_ptr2);

	for (token = strtok_r(path1, "/", &save_ptr); token != NULL;
			token = strtok_r(NULL, "/", &save_ptr)) {
		next_token = strtok_r(NULL, "/", &save_ptr2);
		// printf("next_token : %s, token :  %s\n", next_token, token);
		if (next_token == NULL) {
			// printf("next token null\n");
			file_token = token;
			break;//a
		}
		// printf("token : %s\n", token);
		if (dir_lookup(dir, token, &inode)) {
			if (inode_is_dir(inode)) {
				dir_close(dir);
				dir = dir_open(inode);
				// printf("path dir : %s, %p\n", token, dir);
			}
		}

		file_token = token;
	}
	// printf("file_token : %s\n", file_token);
	memcpy(file_name, file_token, sizeof(char) * (strlen(file_token) + 1));
	palloc_free_page(path1);
	palloc_free_page(path2);

	return dir;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	char file_name[strlen(name) + 1];
	struct dir *dir = split_path(name, file_name);
	struct inode *inode = NULL;

	if (!strcmp(name, "/")) {
		return file_open(dir_get_inode(dir));
	}

	if (dir != NULL)
		dir_lookup (dir, file_name, &inode);
	dir_close (dir);

	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	char file_name[strlen(name) + 1];
	struct inode *inode;
	struct dir *dir = split_path(name, file_name);
	struct dir *dir2;
	bool success;
	char readdir_name[NAME_MAX + 1];
	struct thread *curr = thread_current();

	// printf("remove : %s, %s\n", file_name, name);
	if (!strcmp(name, "/")) {
		return false;
	}

	dir_lookup(dir, file_name, &inode);
	
	if (inode_is_dir(inode)) {
		dir2 = dir_open(inode);
		// printf("directory : %d\n", dir_element(dir));
		// printf("dir2, cur : %p, %p\n", dir_get_inode(dir2), dir_get_inode(thread_current() -> cwd));
		if (!dir_readdir(dir2, readdir_name) && dir_get_inode(dir2) != dir_get_inode(thread_current() -> cwd)) {
			// printf("suc\n");
			for (int i = 2; i < curr -> next_fd; i++) {
				if (curr -> fd_table[i] != NULL) {
					if (file_get_inode(curr -> fd_table[i]) == inode) {
						dir_close(dir);
						return false;
					}
				}
			}
			success = dir_remove(dir, file_name);
		}
	}
	else {
		// printf("file\n");
		success = dir_remove(dir, file_name);
	}
	
	// bool success = dir != NULL && dir_remove (dir, name);
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
