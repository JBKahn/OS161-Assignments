/* BEGIN A3 SETUP */
/*
 * File handles and file tables.
 * New for ASST3
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/unistd.h>
#include <file.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>
#include <vfs.h>

/*** openfile functions ***/

/*
 * file_open
 * opens a file, places it in the filetable, sets RETFD to the file
 * descriptor. the pointer arguments must be kernel pointers.
 * NOTE -- the passed in filename must be a mutable string.
 *
 * A3: As per the OS/161 man page for open(), you do not need
 * to do anything with the "mode" argument.
 */
int
file_open(char *filename, int flags, int mode, int *retfd)
{
	struct vnode *vn = NULL;
	int openerr = vfs_open(filename, flags, mode, &vn);
	if (openerr)
		return openerr; // File open failed

	/* Find free slot in global open file table. */
	int i;
	for (i = 0; goft[i].refcount > 0; i++) {
		if (i > (__OPEN_MAX))
			return ENFILE;
	}

	goft[i].posinfile = 0;
	goft[i].refcount = 1;
	goft[i].vn = vn;
	goft[i].mode = flags;

	/* Find a free slot in the process open file table. */
	int j = 0;
	for (j = 0; curthread->t_filetable->oft[j] != NULL; j++) {
		if (j > __OPEN_MAX)
			return EMFILE;
	}
	curthread->t_filetable->oft[j] = &goft[i];
	*retfd = j;
	return 0;
}


/* 
 * file_close
 * Called when a process closes a file descriptor.  Think about how you plan
 * to handle fork, and what (if anything) is shared between parent/child after
 * fork.  Your design decisions will affect what you should do for close.
 */
int
file_close(int fd)
{
	if ((fd < 0) || (fd > __OPEN_MAX))
		return EBADF; // File despriptor out of bounds

	struct file *gof = curthread->t_filetable->oft[fd];
	if ((gof == NULL) || (gof->refcount == 0))
		return EBADF; // fill is closed.

	gof->refcount--;
	/* When the ref coutn is 0, we can close it. */
	if (gof->refcount == 0) {
		gof->mode = 0;
		gof->posinfile = 0;
		vfs_close(gof->vn);
	}
	curthread->t_filetable->oft[fd] = NULL;
	return 0;
}

/*** filetable functions ***/

/*
 * filetable_init
 * pretty straightforward -- allocate the space, set up
 * first 3 file descriptors for stdin, stdout and stderr,
 * and initialize all other entries to NULL.
 *
 * Should set curthread->t_filetable to point to the
 * newly-initialized filetable.
 *
 * Should return non-zero error code on failure.  Currently
 * does nothing but returns success so that loading a user
 * program will succeed even if you haven't written the
 * filetable initialization yet.
 */

int
filetable_init(void)
{
	struct filetable *openfiletable;
	openfiletable = kmalloc(sizeof(struct filetable));
	if (openfiletable == NULL)
		return ENOMEM;

	/* Set values to NULL */
	int i;
	for (i = 0; i < __OPEN_MAX; i++)
		openfiletable->oft[i] = NULL;

	/* STDIN, STDOUT, and STDERR */
	openfiletable->oft[0] = &goft[0];
	openfiletable->oft[1] = &goft[1];
	openfiletable->oft[2] = &goft[1];

	/* Assign newly-initialized filetable to current thread. */
	curthread->t_filetable = openfiletable;
	return 0;
}

/*
 * filetable_destroy
 * closes the files in the file table, frees the table.
 * This should be called as part of cleaning up a process (after kill
 * or exit).
 */
void
filetable_destroy(struct filetable *ft)
{
	struct file *ofd;
	/* Close each file in filetable. */
	int i;
	for (i = 0; i < __OPEN_MAX; i++) {
		ofd = ft->oft[i];
		/* If fd is closed then skip it. */
		if ((ofd == NULL) || (ofd->refcount == 0))
			continue;
		ofd->refcount--;
		/* When the ref coutn is 0, we can close it. */
		if (ofd->refcount == 0) {
			ofd->mode = 0;
			ofd->posinfile = 0;
			vfs_close(ofd->vn);
		}
	}
	kfree(ft); /* Free memory */
}


/*
 * You should add additional filetable utility functions here as needed
 * to support the system calls.  For example, given a file descriptor
 * you will want some sort of lookup function that will check if the fd is
 * valid and return the associated vnode (and possibly other information like
 * the current file position) associated with that open file.
 */


/* END A3 SETUP */
