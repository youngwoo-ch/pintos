#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

struct lock file_lock;
struct file *open_file (char *file_name);

#endif /* userprog/syscall.h */
