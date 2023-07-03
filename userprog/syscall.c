#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/flags.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "vm/vm.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "kernel/stdio.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void check_address(const uint64_t *uaddr);
struct page *check_virtual_address (void *addr);
void check_valid_buffer (void* buffer, unsigned size, void* rsp, bool to_write);
bool check_valid_fd (int fd);

static struct file *fd_get_file (int fd);
int fd_append_file (struct file *file);
void fd_remove_file (int fd);

void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *f);
int exec(char *file_name);
int wait (tid_t pid);
int create (const char *file, unsigned initial_size);
int remove (const char *file);
int open(const char *file_name);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int dup2(int oldfd, int newfd);

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);

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

	lock_init (&file_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

	thread_current ()->rsp_stack = f->rsp;
	// TODO: Your implementation goes here.
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt ();
		break;
	case SYS_EXIT:
		exit (f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork (f->R.rdi, f);
		break;
	case SYS_EXEC:
		check_virtual_address (f->R.rdi);
		if (exec (f->R.rdi) < 0) 
			exit (TID_ERROR);
		break;
	case SYS_WAIT:
		f->R.rax = wait (f->R.rdi);
		break;
	case SYS_CREATE:
		check_virtual_address (f->R.rdi);
		f->R.rax = create (f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		check_virtual_address (f->R.rdi);
		f->R.rax = remove (f->R.rdi);
		break;
	case SYS_OPEN:
		check_virtual_address (f->R.rdi);
		f->R.rax = open (f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize (f->R.rdi);
		break;
	case SYS_READ:
		check_valid_buffer (f->R.rsi, f->R.rdx, f->rsp, true);
		f->R.rax = read (f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		check_valid_buffer (f->R.rsi, f->R.rdx, f->rsp, false);
		f->R.rax = write (f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek (f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell (f->R.rdi);
		break;
	case SYS_CLOSE:
		close (f->R.rdi);
		break;
	case SYS_DUP2:
		f->R.rax = dup2 (f->R.rdi, f->R.rsi);
		break;
	case SYS_MMAP:
		f->R.rax = mmap (f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
		break;
	case SYS_MUNMAP:
		munmap (f->R.rdi);
		break;
	default:
		break;
	}
}

/* additional function */
void check_address (const uint64_t *uaddr) {
	struct thread *curr = thread_current();
	
	if (uaddr == NULL || !is_user_vaddr(uaddr) || pml4_get_page(curr->pml4, uaddr) == NULL) {
		exit(TID_ERROR);
	}
}

struct page *check_virtual_address (void *addr) {
	if (addr == NULL || !is_user_vaddr (addr)) {
		exit (TID_ERROR);
	}
	
	return spt_find_page (&thread_current ()->spt, addr);
}

void check_valid_buffer (void* buffer, unsigned size, void* rsp, bool to_write) {
	struct page* page;

	if (buffer <= USER_STACK && buffer >= rsp) {
		return;
	}

	for (int i = 0; i < size; i++) {
		page = check_virtual_address (buffer + i);
		if (page == NULL) {
			exit (TID_ERROR);
		}
		if (to_write == true && page->writable == false) {
			exit (TID_ERROR);
		}
	}
}

bool check_valid_fd (int fd) {
	return fd < 0 || fd >= FDCOUNT_LIMIT;
}

static struct file *fd_get_file (int fd) {
	struct thread *curr = thread_current ();

	if (check_valid_fd (fd))
		return NULL;
	
	return curr->fd_table[fd];
}

int fd_append_file (struct file *file) {
	struct thread *curr = thread_current ();
	struct file **fd_table = curr->fd_table;

	if (check_valid_fd (curr->fd_idx)) {
		return -1;
	}
	
	while (fd_table[curr->fd_idx] != NULL) {
		curr->fd_idx++;

		if (check_valid_fd (curr->fd_idx))
			return -1;
	}

	fd_table[curr->fd_idx] = file;

	return curr->fd_idx;
}

void fd_remove_file (int fd) {
	struct thread *curr = thread_current ();

	if (check_valid_fd (fd))
		return;
	
	curr->fd_table[fd] = NULL;
}



/* system call function */
void halt(void) {
	power_off ();
}

void exit(int status) {
	struct thread *curr = thread_current();

	curr->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	
	thread_exit();
}

tid_t fork(const char *thread_name, struct intr_frame *f) {
	return process_fork (thread_name, f);
}

int exec(char *file_name) {
	int file_size = strlen (file_name) + 1;
	char *file_copy = palloc_get_page (PAL_ZERO);

	if (file_copy == NULL)
		exit (TID_ERROR);

	strlcpy (file_copy, file_name, file_size);

	if (process_exec (file_copy) < 0)
		return -1;

	NOT_REACHED ();
	return 0;
}

int wait (tid_t pid) {
	return process_wait (pid);
}

int create (const char *file, unsigned initial_size) {
	return filesys_create (file, initial_size);
}

int remove (const char *file) {
	return filesys_remove (file);
}

int open(const char *file_name) {
	struct file *file = filesys_open (file_name);

	if (file == NULL) {
		return -1;
	}

	int fd = fd_append_file (file);

	if (fd == -1) {
		file_close (file);
	}

	return fd;
}

int filesize(int fd) {
	struct file *file = fd_get_file (fd);

	if (file == NULL)
		return -1;
	
	return file_length (file);
}

int read(int fd, void *buffer, unsigned size) {
	lock_acquire (&file_lock);

	struct thread *curr = thread_current ();
	struct file *file = fd_get_file (fd);

	int ret = -1;

	if (file == NULL){
		ret = -1;
	}
	else if (file == STD_IN + 1) {
		if (curr->count_stdin == 0){
			NOT_REACHED ();
			fd_remove_file (fd);
			ret = -1;
		}
		else {
			int i;
			unsigned char *buf = buffer;

			for (i = 0; i < size; i++) {
				char c = input_getc ();
				*buf++ = c;
				
				if (c == '\0')
					break;
			}

			ret = i;
		}
	}
	else if (file == STD_OUT + 1) {
		ret = -1;
	}
	else {
		struct file *file = fd_get_file (fd);
		ret = (file == NULL) ? -1 : file_read (file, buffer, size);
	}

	lock_release (&file_lock);

	return ret;

	/* before dup2 */
	// int ret = -1;

	// if (fd == STD_IN) {
	// 	int i;
	// 	unsigned char *buf = buffer;

	// 	for (i = 0; i < size; i++) {
	// 		char c = input_getc ();
	// 		*buf++ = c;
			
	// 		if (c == '\0')
	// 			break;
	// 	}

	// 	ret = i;
	// }
	// else if (fd == STD_OUT) {
	// 	ret = -1;
	// }
	// else {
	// 	struct file *file = fd_get_file (fd);
	// 	ret = (file == NULL) ? -1 : file_read (file, buffer, size);
	// }

	// lock_release (&file_lock);

	// return ret;
}

int write(int fd, const void *buffer, unsigned size) {
	lock_acquire (&file_lock);

	struct thread *curr = thread_current ();
	struct file *file = fd_get_file (fd);

	int ret = -1;

	if (file == NULL) {
		ret = -1;
	}
	else if (file == STD_IN + 1) {
		ret = -1;
	}
	else if (file == STD_OUT + 1) {
		if (curr->count_stdout == 0) {
			NOT_REACHED ();
			fd_remove_file (fd);
			ret = -1;
		}
		else {
			putbuf (buffer, size);
			ret = size;
		}
	}
	else {
		struct file *file = fd_get_file (fd);
		ret = (file == NULL) ? -1 : file_write (file, buffer, size);
	}

	lock_release (&file_lock);

	return ret;

	/* before dup2 */
	// int ret = -1;

	// if (fd == STD_IN) {
	// 	ret = -1;
	// }
	// else if (fd == STD_OUT) {
	// 	putbuf (buffer, size);
	// 	ret = size;
	// }
	// else {
	// 	struct file *file = fd_get_file (fd);
	// 	ret = (file == NULL) ? -1 : file_write (file, buffer, size);
	// }

	// lock_release (&file_lock);

	// return ret;
}

void seek(int fd, unsigned position) {
	struct file *file = fd_get_file (fd);

	if (file == NULL || fd <= STD_OUT)
		return;
	
	file_seek (file, position);
}

unsigned tell(int fd) {
	struct file *file = fd_get_file (fd);

	if (file == NULL || fd <= STD_OUT)
		return;
	
	return file_tell (file);
}

void close(int fd) {
	lock_acquire (&file_lock);

	struct thread *curr = thread_current ();
	struct file *file = fd_get_file (fd);

	if (file == NULL || check_valid_fd (fd)) {
		lock_release (&file_lock);
		return;
	}
	
	if (fd == STD_IN || file == STD_IN + 1)
		curr->count_stdin--;
	if (fd == STD_OUT || file == STD_OUT + 1)
		curr->count_stdout--;
	
	fd_remove_file (fd);

	if (fd <= STD_OUT || file == STD_IN + 1 || file == STD_OUT + 1) {
		lock_release (&file_lock);
		return;
	}

	if (file->count_dup == 0) {
		file_close (file);
	}
	else {
		file->count_dup--;
	}

	lock_release (&file_lock);

	/* before dup2 */
	// struct file *file = fd_get_file (fd);

	// if (file == NULL || fd <= STD_OUT)
	// 	return;
	
	// fd_remove_file (fd);

	// file_close (file);
}

int dup2(int oldfd, int newfd) {
	struct thread *curr = thread_current ();
	struct file **fd_table = curr->fd_table;
	struct file *file = fd_get_file (oldfd);

	if (file == NULL)
		return -1;
	
	if (oldfd == newfd)
		return newfd;
	
	if (file == STD_IN + 1) {
		curr->count_stdin++;
	}
	else if (file == STD_OUT + 1) {
		curr->count_stdout++;
	}
	else {
		file->count_dup++;
	}

	close (newfd);
	fd_table[newfd] = file;

	curr->dup_table[oldfd] = newfd;
	curr->dup_table[newfd] = oldfd;

	return newfd;
}

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
    if (offset % PGSIZE != 0) {
        return NULL;
    }

    if (pg_round_down (addr) != addr || is_kernel_vaddr (addr) || addr == NULL || (long long) length <= 0)
        return NULL;
    
    if (fd == STD_IN || fd == STD_OUT)
        exit (TID_ERROR);
    
    if (spt_find_page (&thread_current ()->spt, addr))
        return NULL;

    struct file *file = fd_get_file (fd);

    if (file == NULL)
        return NULL;

    void * ret = do_mmap (addr, length, writable, file, offset);

    return ret;
}

void munmap (void *addr) {
    do_munmap (addr);
}