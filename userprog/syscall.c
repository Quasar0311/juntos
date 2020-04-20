#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
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

	/*** pseudo code ***/
	// get stack pointer from interrupt intr_frame *f
	// get system call number from stack
	// switch(system call number){
	// 	case the number is halt:
	// 		call halt function;
	// 		break;
	// 	case the number is exit:
	// 		call exit function;
	// 		break;
	// 	default
	// 		call thread_exit function;
	// }
	
	/*** implement syscall_handler using 
	system call number stored in the user stack ***/
	switch(*(int *)(if_->R.rax)){
		case 0:
			halt();
			break;

		case 1:
			exit();
			break;

		case 2:
			fork();
			break;

		case 3:
			exec();
			break;

		case 4:
			wait();
			break;

		case 5:
			create();
			break;

		case 6:
			remove();
			break;

		case 7:
			open();
			break;

		case 8:
			filesize();
			break;

		case 9:
			read();
			break;

		case 10:
			write();
			break;

		case 11:
			seek();
			break;

		case 12:
			tell();
			break;

		case 13:	
			close();
			break;

		default:
			thread_exit();
	}
	/*** check if stack pointer is user virtual address
	check if argument pointer is user virtual address ***/
	
	// printf ("system call!\n");
	// thread_exit ();
}

void
check_address(void *addr){
	/*** check if addr is user virtual address
	else process terminates with exit state -1 ***/
	if(!is_user_vaddr(addr)) exit(-1);
}

void
get_argument(struct intr_frame *_if, int *arg, int count){
	/*** push arguments stored in user stack to kernel 
	check if addr is in user virtual address ***/
	for(int i=count; i>=0; i--){
		if_rsp-=sizeof(char *);
		check_address(if_->rsp);
		strlcpy((char *)&argv[i], (char *)if_->rsp, sizeof(char *));
	}
}