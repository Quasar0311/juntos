/* file.c: Implementation of memory mapped file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/syscall.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <stdio.h>
#include <string.h>

static bool file_map_swap_in (struct page *page, void *kva);
static bool file_map_swap_out (struct page *page);
static void file_map_destroy (struct page *page);

// struct lock file_lock;

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_map_swap_in,
	.swap_out = file_map_swap_out,
	.destroy = file_map_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	// lock_init(&file_lock);
}

/* Initialize the file mapped page */
bool
file_map_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	printf("file map initializer, %p\n", page -> va);

	struct file_page *file_page = &page->file;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_map_swap_in (struct page *page, void *kva) {
	struct file_page *file_page = &page->file;

	file_read_at(file_page->f, kva, 
		(off_t)file_page->read_bytes, file_page->ofs);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_map_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread *curr = thread_current();

	if (pml4_is_dirty(curr -> pml4, page -> va)) {
		file_write_at(file_page->f, page->va, 
			(off_t)file_page->read_bytes, file_page->ofs);
	}

	pml4_set_dirty(curr -> pml4, page -> va, false);

	return true;
}

/* Destory the file mapped page. PAGE will be freed by the caller. */
static void
file_map_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread *curr=thread_current();
	off_t write;

	if(pml4_is_dirty(curr->pml4, page->va)){
		/*** writes page->va into file_page->f ***/
		// printf("pml4 is dirty\n");
		write=file_write_at(file_page->f, page->va, 
			(off_t)file_page->read_bytes, file_page->ofs);
	}
	
	if(pml4_get_page(curr->pml4, page->va)!=NULL)
		__free_frame(page->frame);

	file_close(file_page->f);
}

bool
lazy_file_segment(struct page *page, void *aux){
	struct mmap_file *f=aux;
	void *addr=page->va;
	off_t read;
	int iter = (f -> length) / 4096;
	printf("lazy_file : %p, page : %p, iter : %d\n", f -> file, addr, iter);
	/*** file into addr ***/
	
	// for (int i = 0; i < iter; i++) {
	// 	read=file_read_at(f->file, addr + (4096 * i), (off_t)f->read_bytes, page->file.ofs);
	// 	// if(read<(off_t)f->read_bytes){
	// 	// 	memset(addr+read, 0, f->read_bytes-read);
	// 	// }
	// }
	read=file_read_at(f->file, addr, (off_t)f->read_bytes, page->file.ofs);

	if(read<(off_t)f->read_bytes){
		memset(addr+read, 0, f->read_bytes-read);
	}

	pml4_set_dirty(thread_current()->pml4, f->va, 0);

	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	struct mmap_file *mmap_file;
	struct thread *curr=thread_current();
	struct page *page;

	mmap_file=(struct mmap_file *) malloc(sizeof(struct mmap_file));

	mmap_file->va=addr;
	mmap_file -> length = length;

	list_init(&mmap_file->page_list);
	list_push_back(&curr->mmap_list, &mmap_file->file_elem);
	printf("mmap length : %d\n", length);
	while(length>0){
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		
		mmap_file->file=file_reopen(file);
		mmap_file->ofs=offset;
		mmap_file->read_bytes=page_read_bytes; 

		void *aux=mmap_file;
		printf ("mmap page : %p\n", addr);
		if(!vm_alloc_page_with_initializer(VM_FILE, addr, writable, 
			lazy_file_segment, aux)){
				return NULL;
			}
		
		page=spt_find_page(&curr->spt, addr);
		list_push_back(&mmap_file->page_list, &page->mmap_elem);

		page -> mapped = true;
		page->file.f=mmap_file->file;
		page->file.ofs=mmap_file->ofs;
		page->file.read_bytes=mmap_file->read_bytes;

		length-=page_read_bytes;
		addr+=PGSIZE;
		offset+=PGSIZE;

		if (is_kernel_vaddr(addr)) return NULL;
	}

	return mmap_file->va;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *curr=thread_current();
	struct list_elem *e=list_begin(&curr->mmap_list);
	struct mmap_file *fp;

	while(e!=list_end(&curr->mmap_list)){
		fp=list_entry(e, struct mmap_file, file_elem);

		if(fp->va==addr){
			struct list_elem *m=list_begin(&fp->page_list);

			while(m!=list_end(&fp->page_list)){
				struct page *p=list_entry(m, struct page, mmap_elem);

				if (p -> mapped) {
					m=list_next(m);

					p -> unmapped = true;
					p -> mapped = false;

					list_remove(&p -> mmap_elem);
					file_map_destroy(p);
					pml4_clear_page(curr->pml4, p->va);
				}
				else {
					m = list_next(m);
				}
			}
			e=list_next(e);
			list_remove(&fp->file_elem);
		}
		else {
			e = list_next(e);
		}
	}
}
