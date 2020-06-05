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

struct lock file_lock;

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
	lock_init(&file_lock);
}

/* Initialize the file mapped page */
bool
file_map_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	// printf("file map initializer\n");

	struct file_page *file_page = &page->file;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_map_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_map_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	return true;
}

/* Destory the file mapped page. PAGE will be freed by the caller. */
static void
file_map_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread *curr=thread_current();
	off_t write;
	printf("file map destroy: %p\n", page->va);
	// if(pml4_is_dirty(curr->pml4, file_page->f)){
	// 	printf("file is dirty\n");
	// pml4_set_dirty(curr->pml4, page->va, 0);
	pml4_set_accessed(curr->pml4, page->va, 0);

	if(pml4_is_dirty(curr->pml4, page->va)){
		printf("pml4 is dirty\n");
		/*** writes page->va into file_page->f ***/
		write=file_write_at(file_page->f, page->va, 
			(off_t)file_page->read_bytes, file_page->ofs);
		// printf("file write at: %d\n", write);
	}
	// }

	file_close(file_page->f);
	// printf("file map destroy finish\n");
}

bool
lazy_file_segment(struct page *page, void *aux){
	struct mmap_file *f=aux;
	void *addr=page->va;
	// void *addr=page->frame->kva;
	off_t read;
	// printf("offset: %d\n", page->file.ofs);
	/*** file into addr ***/
	// syscall_lock_acquire();
	read=file_read_at(f->file, addr, (off_t)f->read_bytes, page->file.ofs);
	// syscall_lock_release();
	// if(file_read_at(f->file, addr, (off_t)f->read_bytes, f->ofs)
	// 	<(off_t)f->read_bytes){
	// printf("file read at read bytes: %ld, ofs: %d\n", f->read_bytes, page->file.ofs);
	// 		// return false;
	// 		// return true;
	// 	}
	if(read<(off_t)f->read_bytes){
		// printf("addr: %p, zero bytes: %ld\n", addr+read, f->read_bytes-read);
		memset(addr+read, 0, f->read_bytes-read);
		// printf("lazy file segment finish\n");
	}
	// printf("page writable : %d\n", page -> writable);
	// void *addr=page->frame->kva;
	// printf("lazy file segment file: %p\n", f->file);
	// if(file_read_at(f->file, addr, (off_t)f->read_bytes, f->ofs)
	// 	<(off_t)f->read_bytes)
	// 		return false;
	// printf("lazy file segment: %d\n", *(int *)addr);
	// page -> mapped = true;
	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// void *va=addr;
	struct mmap_file *mmap_file;
	struct thread *curr=thread_current();
	struct page *page;
	// off_t ofs=offset;
	// size_t read_bytes=length;
	if(addr==0 || length==0) return NULL;
	printf("do mmap: %p\n", addr);

	mmap_file=(struct mmap_file *) malloc(sizeof(struct mmap_file));

	list_init(&mmap_file->page_list);
	// mmap_file->file=file_reopen(file);
	mmap_file->va=addr;

	list_push_back(&curr->mmap_list, &mmap_file->file_elem);
	// printf("list push back: %p\n", mmap_file->file);
	// printf("do mmap mmap list: %ld\n", list_size(&curr->mmap_list));

	while(length>0){
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		
		mmap_file->file=file_reopen(file);
		mmap_file->ofs=offset;
		mmap_file->read_bytes=page_read_bytes; 

		void *aux=mmap_file;

		// if (spt_find_page(&curr -> spt, addr) != NULL) {
		// 	if (spt_find_page(&curr -> spt, addr) -> mapped) {
		// 		return NULL;
		// 	}
		// }
		// list_push_back(&curr->mmap_list, &mmap_file->file_elem);
		if(!vm_alloc_page_with_initializer(VM_FILE, addr, writable, 
			lazy_file_segment, aux)){
				// printf("vm alloc page with intializer false\n");
				return NULL;
			}
		
		page=spt_find_page(&curr->spt, addr);
		list_push_back(&mmap_file->page_list, &page->mmap_elem);
		// printf("do mmap page list: %ld, addr: %p\n", list_size(&mmap_file->page_list), page->va);

		page -> mapped = true;
		page->file.f=mmap_file->file;
		page->file.ofs=mmap_file->ofs;
		page->file.read_bytes=mmap_file->read_bytes;

		// pml4_set_dirty(curr->pml4, page->va, 0);
		// printf("mmap page: %p\n", page->va);
		// printf(page->mapped? "mapped\n" : "unmapped\n");

		length-=page_read_bytes;
		addr+=PGSIZE;
		offset+=PGSIZE;
		// printf("do mmap file: %p\n", page->file.f);
	}
	// mmap_file->ofs=ofs;
	// mmap_file->read_bytes=read_bytes;
	// printf("finish mmap va: %p\n", mmap_file->va);
	// printf("memory: %d\n", *(int *)mmap_file->va);
	pml4_set_dirty(curr->pml4, mmap_file->va, 0);
	pml4_set_accessed(curr->pml4, mmap_file->va, 0);
	// printf(!pml4_is_dirty(curr->pml4, mmap_file->va) ? "mmap file clean\n": "mmap file dirty\n");
	return mmap_file->va;
	// return page->va;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *curr=thread_current();
	struct list_elem *e=list_begin(&curr->mmap_list);
	struct mmap_file *fp;
	// printf("do munmap\n");
	while(e!=list_end(&curr->mmap_list)){
		fp=list_entry(e, struct mmap_file, file_elem);
		// printf(!pml4_is_dirty(curr->pml4, fp->va) ? "mmap file clean\n": "mmap file dirty\n");
		// printf("do munmap mmap list: %ld, file: %p\n", list_size(&curr -> mmap_list), fp->file);
		// printf("s : %p\n", fp -> va);
		if(fp->va==addr){
			struct list_elem *m=list_begin(&fp->page_list);
			// printf("do munmap page list: %ld\n", list_size(&fp->page_list));

			while(m!=list_end(&fp->page_list)){
				// off_t ofs=fp->ofs;
				
				// size_t read_bytes=fp->read_bytes;
				// size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
				struct page *p=list_entry(m, struct page, mmap_elem);
				// printf("unmap page: %p\n", p->va);
				// if (p == NULL) continue;
				if (p -> mapped) {
					m=list_next(m);

					p -> unmapped = true;
					p -> mapped = false;

					list_remove(&p -> mmap_elem);
					file_map_destroy(p);
					pml4_clear_page(curr->pml4, p->va);
					// printf("while file map destroy : %d\n", p -> file.ofs);
				}
				else{
					// printf("p is not mapped\n");
					m=list_next(m);
				}
			}
			// printf("not same\n");
			
			e=list_next(e);
			// printf("file elem remove\n");
			list_remove(&fp->file_elem);
			// free(fp);
			
		}
		else {
			// printf("not same\n");
			e = list_next(e);
		}
	}
	// file_close(fp->file);
	// free(fp);
	// printf("munmap finished\n");
	// printf("length of page list: %ld\n", list_size(&fp->page_list));
	// printf("do munmap mmap list: %ld\n", list_size(&curr->mmap_list));
	// printf(list_empty(&fp->page_list) ? "empty\n" : "no\n");
}
