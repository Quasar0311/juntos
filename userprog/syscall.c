#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/init.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "include/filesys/filesys.h"
#include "include/userprog/process.h"
#include "include/filesys/file.h"
#include "include/devices/input.h"

struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void get_argument (struct intr_frame *f, int *arg, int count);
void check_address (void *addr);
void syscall_halt (void);
void syscall_exit (int status);
int syscall_open(const char *file);
int syscall_filesize(int fd);
int syscall_read(int fd, void *buffer, unsigned size);
int syscall_write(int fd, void *buffer, unsigned size);

void get_argument (struct intr_frame *f, int *arg, int count);
void check_address (void *addr);
void syscall_halt (void);
void syscall_exit (int status);


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

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	int arg[5];
	/*** implement syscall_handler using 
	system call number stored in the user stack ***/
	int *number=(int *)&f->R.rax;

	switch(*number){
		case 1:
			get_argument(f, arg, 1);
			syscall_exit(arg[0]);
			break;

		case 10:
			//get_argument(f, arg, 3);
			//printf("%d, %d, %d\n", arg[0], arg[1], arg[2]);
			//syscall_write(arg[0], (void *)&arg[1], (unsigned)arg[2]);
			//printf("size : %d\n", f -> R.rdx);
			syscall_write((int) f -> R.rdi, (void *) f -> R.rsi, (unsigned) f -> R.rdx);
			break; 

		default:
			printf ("system call!\n");
			thread_exit();
			
	}

	/*** check if stack pointer is user virtual address
	check if argument pointer is user virtual address ***/
	
	// printf("syscall num : %d\n", (int *)(f->R.rax));
	// printf ("system call!\n");
	
	//thread_exit ();
}

void
check_address (void *addr) {
	if (!is_user_vaddr(addr)) {
		printf("bad address for address : %p\n", addr);
		syscall_exit(-1);
	}
}

void
get_argument (struct intr_frame *f, int *arg, int count) {
	
	printf("original addr : %d\n", (int *) f -> R.rdi);
	// hex_dump(f -> rsp, (void *) f -> rsp, 300, 1);
	// for (i = 0; i < count; i++) {
	// 	// 0x158 + 8
	// 	addr = (void *) f -> rsp + 1 + i;
	// 	//addr += i * sizeof(char *);
	// 	check_address(addr);
	// 	printf("addr pointing : %d\n", f -> R.rdx);
	// 	arg[i] = *(int *) addr;
	// }
	if ((int *) f -> R.rdi != NULL) {
		int rdi;
		rdi = * (int *) f -> R.rdi;
		check_address((void *) f -> R.rdi);
		printf("arg0 : %d\n", rdi);
		arg[0] = *(int *) f -> R.rdi;
	}
	if ((int *) f -> R.rsi != NULL) {
		arg[1] = *(int *) f -> R.rsi;
	}
	if ((int *) f -> R.rdx != NULL) {
		arg[2] = *(int *) f -> R.rdx;
	}
	if ((int *) f -> R.r10 != NULL) {
		arg[3] = *(int *) f -> R.r10;
	}
	if ((int *) f -> R.r8 != NULL) {
		arg[4] = *(int *) f -> R.r8;
	}
	if ((int *) f -> R.r9 != NULL) {
		arg[5] = *(int *) f -> R.r9;
	}


}

void
syscall_halt (void) {
	power_off();
}

void
syscall_exit (int status) {
	struct thread *curr = thread_current();

	printf("%s: exit(%d)\n", curr -> name, status);
	thread_exit();
}

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
		/*** invalid use of void expression ***/
		uint8_t *buf=(uint8_t *)buffer;
		for(unsigned i=0; i<size; i++) 
			buf[i]=input_getc();
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

int
syscall_write(int fd, void *buffer, unsigned size){
	struct file *f;
	off_t bytes_read;
	// printf("write : %d\n", fd);
	lock_acquire(&filesys_lock);
	// printf("hi\n");
	if(fd==1){
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
