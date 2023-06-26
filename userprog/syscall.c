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

void check_address(const uint64_t *uaddr);

void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *f);
int exec(char *file_name);
int wait (tid_t pid);
int create (const char *file, unsigned initial_size);
int remove (const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

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
		if (exec (f->R.rdi) < 0) exit (-1);
		break;
	case SYS_WAIT:
		f->R.rax = wait (f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create (f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove (f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open (f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize (f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read (f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
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
		break;
	default:
		break;
	}
}

void check_address(const uint64_t *uaddr) {
	struct thread *curr = thread_current();

	if (uaddr == NULL || is_kernel_vaddr(uaddr) || pml4_get_page(curr->pml4, uaddr) == NULL) {
		exit(-1);
	}
}

void halt(void) {

}

void exit(int status) {
	struct thread *curr = thread_current();

	curr->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	
	thread_exit();
}

tid_t fork(const char *thread_name, struct intr_frame *f) {

}

int exec(char *file_name) {

}

int wait (tid_t pid) {

}

int create (const char *file, unsigned initial_size) {

}

int remove (const char *file) {

}

int open(const char *file) {

}

int filesize(int fd) {

}

int read(int fd, void *buffer, unsigned size) {

}

int write(int fd, const void *buffer, unsigned size) {
	printf ("%s", buffer);
}

void seek(int fd, unsigned position) {

}

unsigned tell(int fd) {

}

void close(int fd) {

}
