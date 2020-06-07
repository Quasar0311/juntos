/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/thread.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <stdio.h>
#include <string.h>

struct list lru_list;
struct lock lru_list_lock;
struct list_elem *lru_clock;

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
				// printf("case vm file\n");
				uninit_new(uninit_page, upage, init, type, aux, file_map_initializer);
				break;

			case VM_PAGE_CACHE:
				break;
		}

		uninit_page->writable=writable;
		uninit_page->is_loaded=false;
		uninit_page -> init = init;
		uninit_page -> aux = aux;
		uninit_page -> unmapped = false;
		uninit_page -> mapped = false;
		
		/* TODO: Insert the page into the spt. */
		spt_insert_page(spt, uninit_page);
		// printf("insert finish\n");

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

	if (hash_empty(&spt -> vm)) {
		return NULL;
	}

	page.va=pg_round_down(va);
	e=hash_find(&spt->vm, &page.page_elem);
	if (e == NULL) return NULL;

	return hash_entry(e, struct page, page_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	int succ = false;
	/* TODO: Fill this function. */
	if(hash_insert(&spt->vm, &page->page_elem)!=NULL) 
		succ=true;
		
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->vm, &page->page_elem);

	vm_dealloc_page (page);
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

static void
free_page(void *kva){

}

static void
__free_page(struct frame *frame){

}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim, *start;
	/* TODO: The policy for eviction is up to you. */
	struct thread *curr=thread_current();
	// printf("vm get victim: %p\n", victim->page->va);

	if(lru_clock==NULL){
		// printf("lru clock is null\n");
		lru_clock=list_begin(&lru_list);
	} 

	victim=list_entry(lru_clock, struct frame, lru_elem);
	start=victim;

	// printf("while: %p\n", victim->page->va);
	while(pml4_is_accessed(curr->pml4, victim->page->va)){
		pml4_set_accessed(curr->pml4, victim->page->va, 0);

		if(lru_clock==list_back(&lru_list)) 
			lru_clock=list_begin(&lru_list);
			
		else lru_clock=list_next(lru_clock); 

		victim=list_entry(lru_clock, struct frame, lru_elem);
		
		if(victim==start) break;
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	// printf("vm evict frame\n");
	del_frame_from_lru_list(victim);
	swap_out(victim->page);

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
	void *kva=palloc_get_page(PAL_USER); 
	if(kva==NULL) //PANIC("todo");
		return vm_evict_frame(); 

	frame=(struct frame *)malloc(sizeof(struct frame));
	frame->kva=kva; 
	// frame->pa=(void *)vtop(kva);
	frame->page=NULL;

	// printf("add frame to lru list\n");
	// add_frame_to_lru_list(frame);

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
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// if(page==NULL) printf("page is null\n");
	// if(is_kernel_vaddr(addr)) printf("is kernel vaddr\n");
	// if(user) rsp=(void *)f->rsp;
	
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
	// printf("vm claim page\n");
	
	page=spt_find_page(&curr->spt, va);
	if(page==NULL) return false;
	// printf("vm claim page 2\n");

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	struct thread *curr=thread_current();
	// printf("vm do claim page\n");
	
	/* Set links */
	frame->page = page;
	page->frame = frame;
	// printf("pml4 set page va: %p, kva: %p\n", page->va, frame->kva);

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// printf("writable : %d\n", page -> writable);
	if(!pml4_set_page(curr->pml4, page->va, frame->kva, page->writable)){
		palloc_free_page(frame->kva);
		free(frame);
		return false;
	}
	// pml4_set_dirty(curr -> pml4, page -> va, false);
	// printf("vm do claim page 2\n");
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
	// spt = (struct supplemental_page_table *) malloc(sizeof(struct hash));
	hash_init(&spt->vm, vm_hash_func, vm_less_func, NULL);
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

	hash_first(&i, &src -> vm);
	// printf("copy\n");
	while (hash_next(&i)) {
		struct page *p, *newpage;

		p=hash_entry(hash_cur(&i), struct page, page_elem);
		
		if(!vm_alloc_page_with_initializer(page_get_type(p), p->va, 
			p->writable, p->init, p->aux))
				return false;

		if(!vm_claim_page(p->va))
			return false;

		// printf("spt find page begin\n");
		newpage=spt_find_page(dst, p->va);
		// printf("memcpy begin: %p\n", p->frame->kva);
		if(p->frame!=NULL)
			memcpy(newpage->frame->kva, p->frame->kva, PGSIZE);
		// printf("memcpy finish\n");
		// if(page_get_type(p)==VM_FILE) printf("vm file\n");
	}
	
	return true;
}

static void
vm_destroy_func(struct hash_elem *e, void *aux UNUSED){
	destroy(hash_entry(e, struct page, page_elem));
	// free(hash_entry(e, struct page, page_elem));

	return;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) { //vm_destroy
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy(&spt->vm, vm_destroy_func);

	return;
}