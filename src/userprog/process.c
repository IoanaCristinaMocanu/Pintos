#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "vm/page.h"

//#define DEBUG

#ifdef DEBUG
#define PRINT(format) (printf (format))
#define PRINT_ONE_ARG(format, arg) (printf (format, arg))
#define PRINT_TWO_ARG(format, arg1, arg2) (printf (format, arg1, arg2))
#endif

#ifndef DEBUG
#define PRINT(format)
#define PRINT_ONE_ARG(format, arg)
#define PRINT_TWO_ARG(format, arg1, arg2)
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *file_name, char *args, void (**eip) (void),
                  void **esp);
static int push_arguments (void **esp, const char *file_name, char *args);
static void update_child_status (struct thread *parent, pid_t child_pid,
                                 int status);

struct command_line
{
	char *name;
	char *args;
};

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t process_execute (const char *file_name)
{
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	   Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	struct command_line cmd_line;

	cmd_line.name = strtok_r (fn_copy, " ", &cmd_line.args);

  	/* Deny writes to executable while the process is still running */
  	lock_acquire (&file_sys_lock);
  	thread_current()->executable = filesys_open (cmd_line.name);
  	if (thread_current()->executable)
    	file_deny_write (thread_current()->executable);
  	lock_release (&file_sys_lock);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (cmd_line.name, PRI_DEFAULT, start_process, &cmd_line);

	if (tid == TID_ERROR)
	{
		palloc_free_page (fn_copy);
    	return EXIT_FAIL;
  	}

	struct child_status *child = malloc (sizeof (struct child_status));

	if (child == NULL)
		return EXIT_FAIL;

	child->pid = tid;
	list_push_back (&thread_current ()->process_w.children_processes,
	                &child->child_elem);

	struct thread *child_t = get_thread (tid);

	/* Check if child process is already terminated (successfully or not)
	 and if not wait for it to finish loading */

	enum intr_level old_level = intr_disable ();
	if (is_thread (child_t) && child_t->status != THREAD_DYING)
		sema_down (&child_t->process_w.loaded_sema);

	intr_set_level (old_level);

	/* By this time the child process should have communicated its loaded status
	  to its parent */
	if (child->exit_status == EXIT_FAIL)
		return EXIT_FAIL;
	else
		return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void start_process (void *cmd_line_)
{
	struct command_line *cmd_line = (struct command_line *) cmd_line_;
	struct intr_frame if_;
	bool success;

	struct thread *cur = thread_current ();
	struct thread *parent = cur->process_w.parent_t;

	/* Initialize interrupt frame and load executable. */
	memset (&if_, 0, sizeof if_);
	if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
	if_.cs = SEL_UCSEG;
	if_.eflags = FLAG_IF | FLAG_MBS;
	success = load (cmd_line->name, cmd_line->args, &if_.eip, &if_.esp);

	/* If load failed, quit. */
	palloc_free_page (cmd_line->name);

	if (!success)
	{
		enum intr_level old_level = intr_disable ();

		if (is_thread (parent) && parent->status != THREAD_DYING)
			update_child_status (parent, cur->tid, LOADED_FAILED);

		intr_set_level (old_level);

		sema_up (&cur->process_w.loaded_sema);

		exit_fail ();
	}

	enum intr_level old_level = intr_disable ();

	if (is_thread (parent) && parent->status != THREAD_DYING)
		update_child_status (parent, cur->tid, LOADED_SUCCESS);

	intr_set_level (old_level);

	sema_up (&cur->process_w.loaded_sema);

	/* Start the user process by simulating a return from an
	   interrupt, implemented by intr_exit (in
	   threads/intr-stubs.S).  Because intr_exit takes all of its
	   arguments on the stack in the form of a `struct intr_frame',
	   we just point the stack pointer (%esp) to our stack frame
	   and jump to it. */
	asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
	NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.
 * If it was terminated by the kernel (i.e. killed due to an exception),
 * returns -1.
 * If TID is invalid or if it was not a child of the calling process, or if
 * process_wait() has already been successfully called for the given TID,
 * returns -1 immediately, without waiting. */
int process_wait (tid_t child_tid)
{
	struct list_elem *e;
	struct list *children = &thread_current ()->process_w.children_processes;
	struct child_status *child_s;

	/* There are no races on the children list because it is only modified
	  in an interrupts disabled context */

	for (e = list_begin (children); e != list_end (children); e = list_next (e))
	{
		child_s = list_entry (e, struct child_status, child_elem);
		if (child_s->pid == child_tid)
		{
			struct thread *child_t = get_thread (child_tid);

			/* If the child thread is still alive wait for it to finish
			  and then exit status will already be updated by the child */
			if (is_thread (child_t) && child_t->status != THREAD_DYING)
				sema_down (&child_t->process_w.finished_sema);

			list_remove (&child_s->child_elem);
			int status = child_s->exit_status;
			free (child_s);
			return status;
		}
	}

	/* If child was not in the list it is either not a valid child or
	  wait has already been called on it */
	return -1;
}

/* Free the current process's resources. */
void process_exit (void)
{
	struct thread *cur = thread_current ();
  	struct thread *parent = cur->process_w.parent_t;
	uint32_t *pd;

	enum intr_level old_level = intr_disable ();

	int exit_status = cur->process_w.exit_status;

	/* Free children processes list when terminating */
	struct process_wrapper *process_w = &cur->process_w;
	struct list *children = &process_w->children_processes;
	struct list_elem *e;

	ASSERT (process_w);
	ASSERT (children);

	/* Allow write back to executable once exited */
	if (!lock_held_by_current_thread (&file_sys_lock))
		lock_acquire (&file_sys_lock);

	if (parent->executable)
	{
		file_allow_write (parent->executable);
		file_close (parent->executable);
	}
	lock_release (&file_sys_lock);

	/* Print exiting message */
	printf ("%s: exit(%i)\n", cur->name, exit_status);
	for (e = list_begin (children); e != list_end (children);)
	{
		struct child_status *child;
		child = list_entry (e, struct child_status, child_elem);
		e = list_remove (e);
		free (child);
	}

	if (is_thread (parent) && parent->status != THREAD_DYING)
		update_child_status (parent, cur->tid, exit_status);

	sema_up (&cur->process_w.finished_sema);

	intr_set_level (old_level);

	/* Destroy the current process's page directory and switch back
	   to the kernel-only page directory. */
	pd = cur->pagedir;
	if (pd != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		       cur->pagedir to NULL before switching page directories,
		       so that a timer interrupt can't switch back to the
		       process page directory.  We must activate the base page
		       directory before destroying the process's page
		       directory, or our active page directory will be one
		       that's been freed (and cleared). */
		cur->pagedir = NULL;
		pagedir_activate (NULL);
		pagedir_destroy (pd);
	}
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void process_activate (void)
{
	struct thread *t = thread_current ();

	/* Activate thread's page tables. */
	pagedir_activate (t->pagedir);

	/* Set thread's kernel stack for use in processing
	   interrupts. */
	tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
	unsigned char e_ident[16];
	Elf32_Half e_type;
	Elf32_Half e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off e_phoff;
	Elf32_Off e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize;
	Elf32_Half e_phentsize;
	Elf32_Half e_phnum;
	Elf32_Half e_shentsize;
	Elf32_Half e_shnum;
	Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
	Elf32_Word p_type;
	Elf32_Off p_offset;
	Elf32_Addr p_vaddr;
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;
	Elf32_Word p_memsz;
	Elf32_Word p_flags;
	Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load (const char *file_name, char *args, void (**eip) (void), void **esp)
{
	struct thread *t = thread_current ();
	struct Elf32_Ehdr ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

#ifdef VM
	/* Create new Supplemental Page Table */
  	t->spt = create_supp_pt();
#endif

	/* Allocate and activate page directory. */
	t->pagedir = pagedir_create ();
	if (t->pagedir == NULL)
		goto done;
	process_activate ();

  	/* Acquire the file system lock as we will perform operations on the file
       system */
  	lock_acquire(&file_sys_lock);

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL)
	{
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr ||
	    memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 ||
	    ehdr.e_machine != 3 || ehdr.e_version != 1 ||
	    ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
	{
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Elf32_Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file))
				{
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint32_t file_page = phdr.p_offset & ~PGMASK;
					uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint32_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0)
					{
						/* Normal segment.
						   Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes =
						    (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
					}
					else
					{
						/* Entirely zero.
						   Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page, read_bytes,
					                   zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (esp))
		goto done;

	/* Start address. */
	*eip = (void (*) (void)) ehdr.e_entry;

	if (push_arguments (esp, file_name, args) != 0)
		goto done;

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);

	/* Release the file_sys_lock before returning */
	lock_release (&file_sys_lock);

	return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (Elf32_Off) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable)
{
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Calculate how to fill this page.
		       We will read PAGE_READ_BYTES bytes from FILE
		       and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* If VM is not defined, load the page here.
		 * Otherwise, load pages lazily */
//#ifndef VM
		/* Check if virtual page already allocated */
		struct thread *t = thread_current ();
		uint8_t *kpage = pagedir_get_page (t->pagedir, upage);

		if (kpage == NULL)
		{

			/* Get a new page of memory. */
			kpage = palloc_get_page (PAL_USER);
			if (kpage == NULL)
			{
				return false;
			}

			/* Add the page to the process's address space. */
			if (!install_page (upage, kpage, writable))
			{
				palloc_free_page (kpage);
				return false;
			}
		}

		/* Load data into the page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
		{
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);
//#endif
//
//#ifdef VM
//		struct supp_pt_entry *page;
//		/* Check if there are any bytes read from file */
//		if (page_zero_bytes == PGSIZE)
//		{
//		  	/* Install ZERO page */
//		    page = install_page_zero(thread_current()->spt, upage);
//		}
//		else
//		{
//		  	/* Install FILE page */
//		  	// page = install_file_page(...);
//		}
//
//		/* Check if installation was successful */
//		if (!page)
//		  return false;
//
//		ofs += PGSIZE;
//#endif

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool setup_stack (void **esp)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
		if (success)
			*esp = PHYS_BASE;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool install_page (void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	   address, then map our page there. */
	return (pagedir_get_page (t->pagedir, upage) == NULL &&
	        pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Pushes the arguments of the newly created user program on the stack
  using IF_.ESP as the stack pointer, ending in 0 as the return address.
  The arguments string is passed in ARGUMENTS and parsed using strtok_r(). */
static int push_arguments (void **esp, const char *file_name, char *args)
{
	int argc = 0; /* number of arguments */

	/* Initially addresses of arguments in the stack */
	char *arg_address[ARGS_MAX_COUNT];

	/* Memory used so far by arguments each ending in '\0' */
	int used_memory = 0;

	/* Push file_name first on the stack */

	int token_memory = sizeof (char) * strlen (file_name) + 1;
	if (used_memory + token_memory > ARGS_MAX_SIZE || argc + 1 > ARGS_MAX_COUNT)
		return -1;

	*esp -= sizeof (char);
	memset (*esp, 0, sizeof (char));
	*esp -= (token_memory - 1);
	arg_address[argc++] = *esp;
	memcpy (*esp, file_name, (token_memory - 1));
	used_memory += token_memory;

	/* Tokenize and push the rest of arguments on the stack */

	char *save_ptr;
	char *token = strtok_r (args, " ", &save_ptr);

	while (token != NULL)
	{
		int token_memory = sizeof (char) * strlen (token) + 1;
		if (used_memory + token_memory > ARGS_MAX_SIZE || argc + 1 > ARGS_MAX_COUNT)
			return -1;

		*esp -= sizeof (char);
		memset (*esp, 0, sizeof (char));

		*esp -= (token_memory - 1);
		arg_address[argc++] = *esp;
		memcpy (*esp, token, (token_memory - 1));

		used_memory += token_memory;

		token = strtok_r (NULL, " ", &save_ptr);
	}

	*esp = last_address_alligned (*esp) - sizeof (char *) * (argc + 1);

	for (int i = 0; i < argc + 1; i++)
	{
		if (i == argc)
		{
			memset (*esp + i * sizeof (char *), 0, sizeof (char *));
			continue;
		}
		memcpy (*esp + i * sizeof (char *), &arg_address[i], sizeof (char *));
	}

	char **argv = *esp;
	*esp -= sizeof (char **);
	memcpy (*esp, &argv, sizeof (char **));

	*esp -= sizeof (int);
	memcpy (*esp, &argc, sizeof (int));

	/* return address 0 */
	*esp -= sizeof (void (*) (void));
	memset (*esp, 0, sizeof (void (*) (void)));

	/* Testing should work after system calls are implemented */
#ifdef DEBUG
	hex_dump (0, *esp, PHYS_BASE - *esp, 1);
#endif

	return 0;
}

/* Called by child process to update its status inside the children list of
  its parent process. Updates the status of either LOADED or EXIT_STATUS
  depending on the STATUS_UPDATE_TYPE enum. */
static void update_child_status (struct thread *parent, pid_t child_pid,
                                 int status)
{
	struct list_elem *e;
	struct child_status *child;
	struct list *children = &parent->process_w.children_processes;

	// REVIEW: Can we use locks instead of interrupt disable

	for (e = list_begin (children); e != list_end (children); e = list_next (e))
	{
		child = list_entry (e, struct child_status, child_elem);
		if (child->pid == child_pid && child->exit_status != status)
		{
			child->exit_status = status;
			ASSERT (child->exit_status == status);
			break;
		}
	}
}
