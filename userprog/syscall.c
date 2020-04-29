#include "userprog/syscall.h"
// #include "lib/user/syscall.h"
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

struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address (uint64_t reg);
void syscall_halt (void);
pid_t syscall_fork(const char *thread_name, struct intr_frame *parent_frame);
int syscall_exec(const char *cmd_line);
int syscall_wait (pid_t pid);
bool syscall_create (const char *file, unsigned initial_size);
int syscall_open(const char *file);
int syscall_filesize(int fd);
int syscall_read(int fd, void *buffer, unsigned size);
int syscall_write(int fd, void *buffer, unsigned size);
void syscall_seek(int fd, unsigned position);
unsigned syscall_tell(int fd);
void syscall_close(int fd);


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
	//printf("system call no. : %d\n", *number);
	
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
			
			// memcpy(&thread_current()->fork_frame, &f, sizeof(struct intr_frame));
			// intr_dump_frame(f);
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
			break;

		/*** SYS_OPEN ***/
		case 7:
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
			f -> R.rax = syscall_read((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
			break;
		
		/*** SYS_WRITE ***/
		case 10:
			check_address(f -> R.rsi);
			f -> R.rax = syscall_write((int) f -> R.rdi, (void *) f -> R.rsi, (unsigned) f -> R.rdx);
			break; 
		
		/*** SYS_SEEK ***/
		case 11:
			syscall_seek((int)f->R.rdi, (unsigned)f->R.rsi);
			break;
		
		/*** SYS_TELL ***/
		case 12:
			syscall_tell((int)f->R.rdi);
			break;
		
		/*** SYS_CLOSE ***/
		case 13:
			syscall_close((int)f->R.rdi);
			break;

		default:
			thread_exit();
			
	}

	/*** check if stack pointer is user virtual address
	check if argument pointer is user virtual address ***/
	
	// printf("syscall num : %d\n", (int *)(f->R.rax));
	// printf ("system call!\n");
	
	//thread_exit ();
}

void
check_address (uint64_t reg) {
	/*** check if the address is in user address ***/
	
	if ((char *) reg == NULL) {
		//printf("null\n");
		syscall_exit(-1);
	}
	
	if (is_user_vaddr((char *) &reg)) {
		printf("bad address for address : %p\n", &reg);
		syscall_exit(-1);
	}
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

	// struct intr_frame *frame = &thread_current() -> fork_frame;
	// printf("parent rdi : %s\n", thread_current() -> tf.R.rdi);
	// memcpy(parent_frame, &thread_current() -> tf, sizeof(struct intr_frame));
	struct intr_frame *parent_copy = parent_frame;
	return process_fork(thread_name, parent_copy);
	//file_duplicate
}

int
syscall_exec (const char *cmd_line) {
	//printf("%s\n", cmd_line);
	if(process_exec((void *) cmd_line)==-1){
		syscall_exit(-1);
		return -1;
	};
}

int
syscall_wait (pid_t pid) {
	return process_wait(pid);
}

bool
syscall_create (const char *file, unsigned initial_size) {
	if (!strcmp(file, "")) {
		syscall_exit(-1);
		return false;
	}
	else if (strlen(file) >= 511) {
		//printf("hi : %d\n", false);
		return 0;
	}
	else {
		//printf("filesize : %d\n", strlen(file));
		return filesys_create(file, (off_t) initial_size);
	}
	
}

int
syscall_open(const char *file){
	struct file *f;
	int fd=-1;

	lock_acquire(&filesys_lock);
	//printf("file name: %s\n", file);
	f=filesys_open(file);
	fd=process_add_file(f);
	//printf("got fd : %d\n",fd);
	//printf("fd: %d, file open : %d\n", fd, thread_current() -> next_fd);
	lock_release(&filesys_lock);

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
	
	else bytes_read=file_read(f, buffer, (off_t) size);
	lock_release(&filesys_lock);

	return bytes_read;
}

int
syscall_write(int fd, void *buffer, unsigned size){
	struct file *f;
	off_t bytes_read;

	lock_acquire(&filesys_lock);

	if(fd==1){
		putbuf((char *)buffer, size);
		lock_release(&filesys_lock);
		return size;
	}

	/*** get file by using file descriptor ***/
	f=process_get_file(fd);
	if(f==NULL){
		lock_release(&filesys_lock);
		return -1;
	}
	
	else bytes_read=file_write(f, buffer, size);
	lock_release(&filesys_lock);

	return bytes_read;
}

void
syscall_seek(int fd, unsigned position){
	/*** get file by using file descriptor ***/
	struct file *f=process_get_file(fd);

	/*** move offset of file by position ***/
	file_seek(f, position);
}

unsigned syscall_tell(int fd){
	/*** get file by using file descriptor ***/
	struct file *f=process_get_file(fd);

	/*** return file offset ***/
	return file_tell(f);
}

void syscall_close(int fd){
	/*** close file by using file descriptor 
	and initialize entry ***/
	process_close_file(fd);
}