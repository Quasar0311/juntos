#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/synch.h"

void syscall_entry (void);
void check_address(void *addr);
void get_argument(struct intr_frame *_if);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/*** initialize filesys_lock ***/
	lock_init(&filesys_lock);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

// void
// check_address(void *addr){
// 	/*** check if addr is user virtual address
// 	else process terminates with exit state -1 ***/
// 	if(!is_user_vaddr(addr)) exit(-1);
// }

// void
// get_argument(struct intr_frame *_if){
// 	/*** push arguments stored in user stack to kernel 
// 	check if addr is in user virtual address ***/
// 	for(int i=count; i>=0; i--){
// 		if_->rsp-=sizeof(char *);
// 		check_address(if_->rsp);
// 		strlcpy((char *)&argv[i], (char *)if_->rsp, sizeof(char*));
// 	}
// }

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	/*** implement syscall_handler using 
	system call number stored in the user stack ***/
	int *number=(int *)&f->R.rax;

	switch(*number){
		case 10:
			syscall_write();
			break;

		default:
			thread_exit();
	}

	/*** check if stack pointer is user virtual address
	check if argument pointer is user virtual address ***/
	
	// printf("syscall num : %d\n", (int *)(f->R.rax));
	printf ("system call!\n");
	thread_exit ();
}

/*** acquire lock before read(), write() access to file ***/

int
syscall_open(const char *file){
	struct file *f;
	int fd=-1;

	// lock_acquire(&filesys_lock);
	f=filesys_open(file);
	fd=process_add_file(f);
	// lock_release(&filesys_lock);

	return fd;
}

int
syscall_filesize(int fd){
	struct file *f;

	f=process_get_file(fd);
	if(f==NULL) return -1;
	return file_length(f);
}

int
syscall_read(int fd, void *buffer, unsigned size){
	struct file *f;
	off_t bytes_read;

	lock_acquire(&filesys_lock);

	if(fd==0){
		for(int i=0; i<size; i++) 
			(uint8_t *)buffer[i]=input_getc();
		lock_release(&filesys_lock);
		return size;
	}

	f=process_get_file(fd);
	if(f==NULL){
		lock_release(&filesys_lock);
		return -1;
	}
	
	else bytes_read=file_read(f, buffer, size);
	lock_release(&filesys_lock);

	return bytes_read;
}

void
syscall_write(int fd, void *buffer, unsigned size){
	struct file *f;
	off_t bytes_read;

	lock_acquire(&filesys_lock);

	if(fd==0){
		putbuf((char *)buffer, size);
		lock_release(&filesys_lock);
		return size;
	}

	f=process_get_file(fd);
	if(f==NULL){
		lock_release(&filesys_lock);
		return -1;
	}
	
	else bytes_read=file_write(f, buffer, size);
	lock_release(&filesys_lock);

	return bytes_read;
}