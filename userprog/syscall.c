#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
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
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"

struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address (uint64_t reg);
void syscall_halt (void);
pid_t syscall_fork(const char *thread_name, struct intr_frame *parent_frame);
int syscall_exec(const char *cmd_line);
int syscall_wait (pid_t pid);
bool syscall_create (const char *file, unsigned initial_size);
bool syscall_remove (const char *file);
int syscall_open(const char *file);
int syscall_filesize(int fd);
int syscall_read(int fd, void *buffer, unsigned size);
int syscall_write(int fd, void *buffer, unsigned size);
void syscall_seek(int fd, unsigned position);
unsigned syscall_tell(int fd);
void syscall_close(int fd);
int syscall_dup2(int oldfd, int newfd);
void *syscall_mmap (void *addr, size_t length, int writable, int fd, off_t offset);
// void syscall_munmap (void *addr);

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
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	int fork;
	/*** implement syscall_handgler using 
	system call number stored in the user stack ***/
	int *number=(int *)&f->R.rax;
	struct thread *curr=thread_current();
	
	switch(*number){
		/*** SYS_HALT ***/
		case 0:
			syscall_halt();
			break;

		/*** SYS_EXIT ***/
		case 1:
			syscall_exit((int)f->R.rdi);
			break;

		/*** SYS_FORK ***/
		case 2:
			check_address((uint64_t)f->R.rdi);
			fork=syscall_fork((char *)f->R.rdi, f);
			f->R.rax = fork;
			break;
		
		/*** SYS_EXEC ***/
		case 3:
			check_address((uint64_t)f->R.rdi);
			f->R.rax=syscall_exec((char *)f->R.rdi);
			break;
		
		/*** SYS_WAIT ***/
		case 4:
			f -> R.rax = syscall_wait((int) f -> R.rdi);
			break;

		/*** SYS_CREATE ***/
		case 5:
			check_address((uint64_t) f -> R.rdi);
			f -> R.rax = syscall_create((char *) f -> R.rdi, (unsigned) f -> R.rsi);
			break;

		/*** SYS_REMOVE ***/
		case 6:
			check_address((uint64_t) f -> R.rdi);
			f -> R.rax = syscall_remove((char *) f -> R.rdi);
			break;

		/*** SYS_OPEN ***/
		case 7:
			// thread_current() -> tf.rsp = f -> rsp;
			curr->kernel_rsp=f->rsp; 
			
			check_address(f -> R.rdi);
			f -> R.rax = syscall_open((char *)f->R.rdi);
			break;

		/*** SYS_FILESIZE ***/
		case 8:
			f -> R.rax = syscall_filesize((int)f->R.rdi);
			break;

		/*** SYS_READ ***/
		case 9:
			check_address(f -> R.rsi);
			curr->kernel_rsp=f->rsp; 
			// printf("rsi : %p, rsp: %p\n", f -> R.rsi, f->rsp);

			// if(f->R.rsi >= (void *)f->rsp - 8 && f->R.rsi+PGSIZE<(void *)USER_STACK+1024*1024){
			// 	printf("here\n");
			// 	return vm_stack_growth(f->R.rsi);
			// }
			
			if (f -> R.rsi < (void *) 0x600000) {
			// if (f -> R.rsi > (void *) f->rsp) {
			// if(spt_find_page(&curr->spt, (void *)f->R.rsi)->writable){
				// printf("invalid address\n");
				syscall_exit(-1);
			}

			f -> R.rax = syscall_read((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
			break;
		
		/*** SYS_WRITE ***/
		case 10:
			// thread_current() -> tf.rsp = f -> rsp;
			check_address(f -> R.rsi);
			f -> R.rax = syscall_write((int) f -> R.rdi, (void *) f -> R.rsi, (unsigned) f -> R.rdx);
			break; 
		
		/*** SYS_SEEK ***/
		case 11:
			syscall_seek((int)f->R.rdi, (unsigned)f->R.rsi);
			break;
		
		/*** SYS_TELL ***/
		case 12:
			f->R.rax=syscall_tell((int)f->R.rdi);
			break;
		
		/*** SYS_CLOSE ***/
		case 13:
			// thread_current() -> tf.rsp = f -> rsp;
			curr->kernel_rsp=f->rsp; 
			
			syscall_close((int)f->R.rdi);
			break;
		
		/*** SYS_DUP2 ***/
		case 21:
			f->R.rax=syscall_dup2((int)f->R.rdi, (int)f->R.rsi);
			break;

		/*** SYS_MMAP ***/
		case SYS_MMAP:
			// printf("sys mmap\n");
			// check_address(f->R.rdi);
			f->R.rax=syscall_mmap((void *)f->R.rdi, (size_t)f->R.rsi, (int)f->R.rdx, 
				(int)f->R.r10, (off_t)f->R.r8);
			break;
		
		/*** SYS_MUNMAP ***/
		case SYS_MUNMAP:
			check_address(f->R.rdi);
			syscall_munmap((void *)f->R.rdi);
			break;

		default:
			thread_exit();
			
	}

	/*** check if stack pointer is user virtual address
	check if argument pointer is user virtual address ***/
}

void
// struct page *
check_address (uint64_t reg) {
	/*** check if the address is in user address ***/
	
	if ((char *) reg == NULL) {
		syscall_exit(-1);
	}
	
	if (is_user_vaddr((char *) &reg)) {
		printf("bad address for address : %p\n", &reg);
		syscall_exit(-1);
	}

	// return spt_find_page(&thread_current()->spt, (void *)&reg);
}

void
syscall_halt (void) {
	power_off();
}

void
syscall_exit (int status) {
	struct thread *curr = thread_current();
	curr -> exit_status = status;
	printf("%s: exit(%d)\n", curr -> name, status);
	thread_exit();
}

pid_t
syscall_fork(const char *thread_name, struct intr_frame *parent_frame){
	struct intr_frame *parent_copy = parent_frame;
	
	return process_fork(thread_name, parent_copy);
}

int
syscall_exec (const char *cmd_line) {
	int ex;

	ex = process_exec((void *) cmd_line);
	
	if (ex == -1) {
		return TID_ERROR;
	}
	return ex;
}

int
syscall_wait (pid_t pid) {
	return process_wait(pid);
}

bool
syscall_create (const char *file, unsigned initial_size) {
	lock_acquire(&filesys_lock);
	if (!strcmp(file, "")) {
		lock_release(&filesys_lock);
		syscall_exit(-1);
		return false;
	}
	else if (strlen(file) >= 511) {
		lock_release(&filesys_lock);
		return 0;
	}
	else {
		lock_release(&filesys_lock);
		return filesys_create(file, (off_t) initial_size);
	}
	
}

bool 
syscall_remove (const char *file) {
	return filesys_remove(file);
}

int
syscall_open(const char *file){
	struct file *f;
	int fd=-1;
	struct thread *curr = thread_current();

	lock_acquire(&filesys_lock);
	
	f=filesys_open(file);
	fd=process_add_file(f);
	if (!strcmp(curr -> name, file)) {
		file_deny_write(f);
	}

	lock_release(&filesys_lock);
	return fd;
}

int
syscall_filesize(int fd){
	struct file *f;

	f = process_get_file(fd);
	if (f == NULL) {
		return -1;
	}
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
	
	else {
		bytes_read=file_read(f, buffer, (off_t) size);
	}
	lock_release(&filesys_lock);

	return bytes_read;
}

int
syscall_write(int fd, void *buffer, unsigned size){
	struct file *f;
	off_t bytes_read;
	struct thread *curr = thread_current();

	lock_acquire(&filesys_lock);
	// printf("writing : %d\n", curr -> tid);

	if (fd == 0) {
		syscall_exit(-1);
	}

	if(fd==1 && curr -> std_out == 1){
		putbuf((char *)buffer, size);
		lock_release(&filesys_lock);
		return size;
	}

	if (curr -> std_out == fd) {
		putbuf((char *)buffer, size);
		lock_release(&filesys_lock);
		return size;
	}

	/*** get file by using file descriptor ***/
	f=process_get_file(fd);
	if(f==NULL){
		lock_release(&filesys_lock);
		return 0;
	}
	
	else {
		bytes_read=file_write(f, buffer, size);
	}
	lock_release(&filesys_lock);
	return bytes_read;
}

void
syscall_seek(int fd, unsigned position){
	/*** get file by using file descriptor ***/
	struct file *f=process_get_file(fd);
	if (f == NULL) {
		return;
	}

	lock_acquire(&filesys_lock);

	/*** move offset of file by position ***/
	file_seek(f, position);

	lock_release(&filesys_lock);
}

unsigned 
syscall_tell(int fd){
	/*** get file by using file descriptor ***/
	struct file *f=process_get_file(fd);

	/*** return file offset ***/
	return file_tell(f);
}

void 
syscall_close(int fd){
	/*** close file by using file descriptor 
	and initialize entry ***/
	process_close_file(fd);
}

int 
syscall_dup2(int oldfd, int newfd){
	struct thread *curr=thread_current();
	struct file **copy_fd;
	
	if(curr->max_fd==256 && newfd>=256){
		copy_fd=calloc(512, sizeof(struct file*));
		for(int i=0; i<256; i++) copy_fd[i] = curr -> fd_table[i];
		for(int i=256; i<512; i++) copy_fd[i]=NULL;

		free(curr -> fd_table);
		curr -> fd_table = copy_fd;
		curr->max_fd=512;
	}

	if(oldfd==newfd) return newfd;

	if(oldfd==curr->std_in) curr->std_in=newfd;
	if(oldfd==curr->std_out) curr->std_out=newfd; 

	else if(oldfd<0||curr->fd_table[oldfd]==NULL) {
		return -1; 
	}
	
	if(curr->fd_table[newfd]!=NULL) {
		process_close_file(newfd);
	}

	curr->fd_table[newfd]=curr->fd_table[oldfd];

	if(newfd>=curr->next_fd) {
		curr->next_fd=newfd+1;
	}

	return newfd;
}

void *
syscall_mmap (void *addr, size_t length, int writable, int fd, off_t offset){
	void *va;
	if(fd==0 || fd==1) return NULL;
	// printf("syscall mmap addr: %p\n", addr);
	if(length==0) return NULL;

	if ((int) addr % 4096 != 0 || addr == NULL || addr==0) return NULL;

	if (offset % 4096 != 0) return NULL;

	if (!is_user_vaddr(addr)) return NULL;

	return do_mmap(addr, length, writable, process_get_file(fd), offset);
}

void 
syscall_munmap (void *addr){
	return do_munmap(addr);
}

void syscall_lock_acquire (void) {
	lock_acquire(&filesys_lock);
	return;
}

void syscall_lock_release (void) {
	lock_release(&filesys_lock);
	return;
}