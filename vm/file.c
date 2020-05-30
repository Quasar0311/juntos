/* file.c: Implementation of memory mapped file object (mmaped object). */

#include "vm/vm.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

static bool file_map_swap_in (struct page *page, void *kva);
static bool file_map_swap_out (struct page *page);
static void file_map_destroy (struct page *page);

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
}

/* Initialize the file mapped page */
bool
file_map_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_map_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_map_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file mapped page. PAGE will be freed by the caller. */
static void
file_map_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

static bool
lazy_file_segment(struct page *page, void *aux){
	struct file_page *f=aux;
	void *addr=page->va;

	if(file_read_at(f->file, addr, (off_t)f->read_bytes, f->ofs)
		<(off_t)f->read_bytes)
			return false;
	
	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	void *va=addr;

	if(addr==0 || length==0) return NULL;

	struct file_page *file_page=
			(struct file_page *) malloc(sizeof(struct file_page));
	struct thread *curr=thread_current();
	
	file_page->file=file;
	list_push_back(&curr->mmap_list, &file_page->file_elem);

	while(length>0){
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
	
		file_page->ofs=offset;
		file_page->read_bytes=page_read_bytes; 

		void *aux=file_page;

		if(!vm_alloc_page_with_initializer(VM_FILE, addr, writable, 
			lazy_file_segment, aux)){
				return NULL;
			}
		
		struct page *page=spt_find_page(&curr->spt, addr);
		list_push_back(&file_page->page_list, &page->mmap_elem);

		length-=page_read_bytes;
		addr+=PGSIZE;
		offset+=PGSIZE;
	}

	return va;
}

/* Do the munmap */
void
do_munmap (void *addr) {

}
