/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/thread.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "vm/file.h"
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "devices/disk.h"
#include <stdio.h>
#include <string.h>

struct list lru_list;
struct lock lru_list_lock;
struct list_elem *lru_clock;

struct lock hash_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	lru_list_init();
	lock_init(&hash_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *uninit_page=(struct page *)malloc(sizeof(struct page));

		switch(type){
			case VM_ANON:
				uninit_new(uninit_page, upage, init, type, aux, anon_initializer);
				break;

			case VM_FILE:
				uninit_new(uninit_page, upage, init, type, aux, file_map_initializer);
				break;

			// case VM_PAGE_CACHE:
			// 	uninit_new(uninit_page, upage, init, type, aux, page_cahce_initializer);
			// 	break;
		}

		uninit_page->writable=writable;
		uninit_page->is_loaded=false;
		uninit_page -> init = init;
		uninit_page -> aux = aux;
		uninit_page -> unmapped = false;
		uninit_page -> mapped = false;
		uninit_page -> lazy_file = false;
		
		/* TODO: Insert the page into the spt. */
		spt_insert_page(spt, uninit_page);

		return true;
	}
	else goto err;
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) { //find_vme
	struct page page;
	/* TODO: Fill this function. */
	struct hash_elem *e;

	lock_acquire(&hash_lock);

	if (hash_empty(&spt -> vm)) {
		lock_release(&hash_lock);
		return NULL;
	}

	page.va=pg_round_down(va);
	e=hash_find(&spt->vm, &page.page_elem);

	if (e == NULL) {
		lock_release(&hash_lock);
		return NULL;
	}

	lock_release(&hash_lock);

	return hash_entry(e, struct page, page_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	int succ = false;

	lock_acquire(&hash_lock);
	/* TODO: Fill this function. */
	if(hash_insert(&spt->vm, &page->page_elem)!=NULL) 
		succ=true;
	
	lock_release(&hash_lock);

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {

	lock_acquire(&hash_lock);
	hash_delete(&spt->vm, &page->page_elem);
	pml4_clear_page(thread_current() -> pml4, page -> va);
	vm_dealloc_page (page);
	lock_release(&hash_lock);
}

void 
lru_list_init(void){
	list_init(&lru_list);
	lock_init(&lru_list_lock);
	lru_clock=NULL;
}

static void
add_frame_to_lru_list(struct frame *frame){
	list_push_back(&lru_list, &frame->lru_elem);
}

static void
del_frame_from_lru_list(struct frame *frame){
	list_remove(&frame->lru_elem);
}

void
__free_frame(struct frame *frame){
	del_frame_from_lru_list(frame);
	palloc_free_page(frame->kva);
	free(frame);
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim, *start;
	/* TODO: The policy for eviction is up to you. */
	struct thread *curr=thread_current();

	if(lru_clock==NULL){
		lru_clock=list_begin(&lru_list);
	} 

	victim=list_entry(lru_clock, struct frame, lru_elem);
	start=victim;
	pml4_set_accessed(curr -> pml4, victim -> page -> va, 1);
	
	while(pml4_is_accessed(curr->pml4, victim->page->va)){
		pml4_set_accessed(curr->pml4, victim->page->va, 0);

		if(lru_clock==list_back(&lru_list)) 
			lru_clock=list_begin(&lru_list);

		else lru_clock=list_next(lru_clock); 

		victim=list_entry(lru_clock, struct frame, lru_elem);
		
		if(victim==start) return victim;
	}
	
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	struct thread *curr=thread_current();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);

	del_frame_from_lru_list(victim);
	pml4_clear_page(curr->pml4, victim->page->va);

	return victim; 
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame;
	/* TODO: Fill this function. */
	/*** allocate a new physical frame and 
	 * return its kernel virtual address ***/ 
	void *kva=palloc_get_page(PAL_USER|PAL_ZERO); 
	if(kva==NULL) {
		return vm_evict_frame(); 
	}

	frame=(struct frame *)malloc(sizeof(struct frame));
	frame->kva=kva; 
	frame->page=NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static bool
vm_stack_growth (void *addr) {
	void *pg_addr=pg_round_down(addr);

	if(!vm_alloc_page_with_initializer(VM_ANON, pg_round_down(addr), true, NULL, NULL))
		return false;

	if(!vm_claim_page(pg_addr))
		return false;

	return true;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = spt_find_page(spt, addr);
	void *rsp=(void *)f->rsp;
	struct mmap_file *mmap_file;

	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	if(!user){
		rsp=thread_current()->kernel_rsp;
	}
	
	if (page == NULL) {
		if(addr >= rsp - 8 && addr+PGSIZE<(void *)USER_STACK+1024*1024){
			return vm_stack_growth(addr);
		}
	}

	/*** valid page fault ***/
	if(page==NULL || is_kernel_vaddr(addr)|| !not_present){
		if(page!=NULL) free(page);
		return false;
	}

	if (page -> unmapped) {
		return false;
	}
	if (page -> lazy_file) {
		mmap_file = page -> aux;
		if ((mmap_file -> length / 4096) > 100) {
			for (int i = 0; i < ((mmap_file -> length / 4096) + 1); i++) {
				page = spt_find_page(spt, addr);
				vm_do_claim_page(page);
				addr += 4096;
			}
		}
	}
	
	/*** bogus page fault ***/
	return vm_do_claim_page (page); 
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	struct page *page;
	/* TODO: Fill this function */
	struct thread *curr=thread_current();
	
	page=spt_find_page(&curr->spt, va);
	if(page==NULL) return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	struct thread *curr=thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(!pml4_set_page(curr->pml4, page->va, frame->kva, page->writable)){
		__free_frame(frame);
		return false;
	}

	add_frame_to_lru_list(frame);
	
	return swap_in (page, frame->kva);
}

/*** hash_hash_func in hash_init ***/
static uint64_t
vm_hash_func(const struct hash_elem *e, void *aux UNUSED){
	struct page *p;
	
	p=hash_entry(e, struct page, page_elem);
	return hash_int((int)p->va);
}

/*** hash_less_func in hash_init ***/
static bool
vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
	return hash_entry(b, struct page, page_elem)->va >
		hash_entry(a, struct page, page_elem)->va;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) { //vm_init
	lock_acquire(&hash_lock);
	hash_init(&spt->vm, vm_hash_func, vm_less_func, NULL);
	lock_release(&hash_lock);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	struct hash_iterator i;
	struct frame *frame;
	void *frame_addr;
	struct page *page;
	struct thread *curr = thread_current();

	struct disk *swap_disk = disk_get(1, 1);

	lock_acquire(&hash_lock);

	hash_first(&i, &src -> vm);

	while (hash_next(&i) != NULL) {
		struct page *p, *newpage;
		struct anon_page *anon_page;

		p=hash_entry(hash_cur(&i), struct page, page_elem);

		if(!vm_alloc_page_with_initializer(page_get_type(p), p->va, 
			p->writable, p->init, p->aux)) {
				lock_release(&hash_lock);
				return false;
		}

		if (p -> operations ->type == 0) {
			continue;
		}

		if(!vm_claim_page(p->va)) {
			lock_release(&hash_lock);
			return false;
		}

		newpage=spt_find_page(dst, p->va);

		if(p->frame!=NULL) {
			memcpy(newpage->frame->kva, p->frame->kva, PGSIZE);
		}
	}

	lock_release(&hash_lock);

	return true;
}

static void
vm_destroy_func(struct hash_elem *e, void *aux UNUSED){
	destroy(hash_entry(e, struct page, page_elem));
	pml4_clear_page(thread_current() -> pml4, hash_entry(e, struct page, page_elem));

	return;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) { //vm_destroy
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	lock_acquire(&hash_lock);
	hash_destroy(&spt->vm, vm_destroy_func);
	
	lock_release(&hash_lock);

	return;
}