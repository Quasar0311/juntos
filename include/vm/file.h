#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page {
	// void *addr;
	struct file *f;
	off_t ofs;
    size_t read_bytes;
	// struct list_elem file_elem;
	// struct list page_list;
};

struct mmap_file{
	void *va; //int mapid
	struct file *file;
	off_t ofs;
    size_t read_bytes;
	struct list_elem file_elem;
	struct list page_list;
	size_t length;	
};

void vm_file_init (void);
bool file_map_initializer (struct page *page, enum vm_type type, void *kva);
bool lazy_file_segment(struct page *page, void *aux);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
