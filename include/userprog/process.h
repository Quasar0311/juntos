#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct load_file{
    struct file *file;
    off_t ofs;
    size_t read_bytes;
    size_t zero_bytes;
    struct inode *inode;
};

/*** create file descriptor for a file object ***/
int process_add_file(struct file *f);
/*** return file object address by searching process 
file descriptor talbe ***/
struct file *process_get_file(int fd);
/*** close the file of file descriptor and initialize entry ***/
void process_close_file(int fd);

/*** return child_list process descriptor by pid ****/
struct thread *get_child_process(int pid);
/*** remove process descriptor of parent process's child_list ****/
void remove_child_process(struct thread *cp);

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
