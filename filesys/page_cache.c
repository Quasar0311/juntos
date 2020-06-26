/* page_cache.c: Implementation of Page Cache (Buffer Cache). */

#include "vm/vm.h"
#include "filesys/filesys.h"
static bool page_cache_readahead (struct page *page, void *kva);
static bool page_cache_writeback (struct page *page);
static void page_cache_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations page_cache_op = {
	.swap_in = page_cache_readahead,
	.swap_out = page_cache_writeback,
	.destroy = page_cache_destroy,
	.type = VM_PAGE_CACHE,
};

tid_t page_cache_workerd;

void *p_page_cache;
struct page_cache **pc_table;
int pc_clock;

/* The initializer of file vm */
void
pagecache_init (void) {
	/* TODO: Create a worker daemon for page cache with page_cache_kworkerd */
	p_page_cache=malloc(64*DISK_SECTOR_SIZE);

	pc_table=calloc(64, sizeof(struct page_cache *));
	for(int i=0; i<64; i++) pc_table[i]=NULL;
}

/* Initialize the page cache */
bool
page_cache_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &page_cache_op;

}

/* Utilze the Swap in mechanism to implement readhead */
static bool
page_cache_readahead (struct page *page, void *kva) {
}

/* Utilze the Swap out mechanism to implement writeback */
static bool
page_cache_writeback (struct page *page) {
}

/* Destory the page_cache. */
static void
page_cache_destroy (struct page *page) {
}

/* Worker thread for page cache */
static void
page_cache_kworkerd (void *aux) {
}

static struct page_cache *
pc_lookup(disk_sector_t sector){
	for(int i=0; i<64; i++){
		if(pc_table[i]!=NULL && pc_table[i]->sector==sector)
			return pc_table[i]; 
	}

	return NULL;
}

static struct page_cache *
pc_select_victim(void){
	int i=pc_clock;
	struct page_cache *pc;

	while(pc_table[pc_clock]->clock!=0){
		pc_table[pc_clock]->clock--;

		if(pc_clock==63) pc_clock=0;
		else pc_clock++;	

		if(pc_clock==i) break;
	}

	pc=pc_table[pc_clock];

	if(pc->dirty){
		disk_write(filesys_disk, pc->sector, pc->data);
	}

	return pc;
}

static struct page_cache *
pc_get(void){
	struct page_cache *pc;

	for(i=0; i<64; i++){
		if(pc_table[i]==NULL){
			pc=(struct page_cache *)malloc(sizeof(struct page_cache));
		
			pc->data=(void *)(p_page_cache + i*DISK_SECTOR_SIZE);

			return pc;
		}
	}

	pc=pc_select_victim();
		
	pc->dirty=false;
	pc->used=false;
	pc->clock=0;

	return pc;
}

bool 
pc_read(disk_sector_t sector_idx, void *buffer, off_t bytes_read, 
    int chunk_size, int sector_ofs){
	struct page_cache *pc;

	pc=pc_lookup(sector_idx);

	if(pc!=NULL){
		memcpy(buffer+bytes_read, pc->data+sector_ofs, chunk_size);
		return true;
	}

	pc=pc_get();

	disk_read(filesys_disk, sector_idx, pc->data);
	memcpy(buffer+bytes_read, pc->data+sector_ofs, chunk_size);

	pc->sector=sector_idx;
	pc->clock++;

	return true;
}

bool
pc_write(disk_sector_t sector_idx, void *buffer, off_t bytes_written,
    int chunk_size, int sector_ofs){

}