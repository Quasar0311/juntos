/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

static bool *disk_table;
// int free_disk;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	int size;
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	// printf("anon init\n");
	size = (int) disk_size(swap_disk);

	disk_table = calloc(size / 8, sizeof(bool));
	for (int i = 0; i < (size / 8); i++) disk_table[i] = false;
	// printf("size: %d, disk table: %d\n", size, size/8);
	// free_disk=-1;
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// printf("hi anon kva : %016x\n", kva);
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

	anon_page -> disk_location = -1;
	// printf("finish anon initializer\n");
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	int disk_sector = anon_page -> disk_location;

	// printf("swap in disk sector: %d, kva: %p, va : %p\n", disk_sector, kva, page -> va);
	if (disk_sector != -1) {
		for (int i = 0; i < 8; i++) {
			disk_read(swap_disk, (disk_sector * 8) + i,
				kva + (512 * i));
		}
	}

	anon_page -> disk_location = -1;
	disk_table[disk_sector] = false;
	// printf("anon swap in disk sector: %d\n", disk_sector);
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	int free_disk = -1;
	void *page_addr = page -> frame -> kva;
	int size = (int) disk_size(swap_disk);
	// printf("swap_out\n");
	// if(disk_table[free_disk+1]){
		for (int i = 0; i < (size / 8); i++) {
			if (!disk_table[i]) {
				free_disk = i;
				disk_table[free_disk] = true;
				break;
			}
		}
	// }
	// else free_disk=free_disk+1;

	if (free_disk == -1) PANIC("NO MORE DISK AREA");

	for (int i = 0; i < 8; i++) {
		disk_write(swap_disk, (free_disk * 8) + i, 
			page_addr + (512 * i));
	}

	anon_page -> disk_location = free_disk;
	// printf("anon swap out free disk: %d, kva: %p, va : %p\n", free_disk, page->frame->kva, page -> va);
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// free(anon_page);
}
