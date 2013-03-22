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
#include <vnode.h>
#include <kern/fcntl.h>

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
	struct vnode *newvn = NULL;
	int openerr = vfs_open(filename, flags, mode, &newvn);
	if (openerr)
		return openerr; // File open failed

	/* Find free slot in global open file table. */
	int i;
	for (i = 0; *((curthread->t_filetable)->refcount[i]) > 0; i++) {
		if (i > (__OPEN_MAX))
			return ENFILE;
	}

	int *newref = (int *)kmalloc(sizeof(int));
	*newref= 1;
	curthread->t_filetable->refcount[i] = newref;

	off_t *newpos = (off_t *)kmalloc(sizeof(off_t));
	*newpos= 0;
	curthread->t_filetable->posinfile[i] = newpos;

	curthread->t_filetable->vn[i] = newvn;
	curthread->t_filetable->filecount++;

	*retfd = i;
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

	struct vnode *oldvn = curthread->t_filetable->vn[fd];
	if ((oldvn == NULL) || (curthread->t_filetable->refcount[fd] == 0))
		return EBADF; // fill is closed.

	curthread->t_filetable->refcount[fd]--;
	/* When the ref coutn is 0, we can close it. */
	if ((curthread->t_filetable)->refcount[fd] == 0) {
		*(curthread->t_filetable->posinfile[fd]) = 0;
		vfs_close(oldvn);
	}
	curthread->t_filetable->vn[fd] = NULL;
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
	struct vnode *cons_vnode=NULL; 
	curthread->t_filetable = kmalloc(sizeof(struct filetable));
	if (curthread->t_filetable == NULL)
		return ENOMEM;

	/* Set values to NULL */
	int i;
	for (i = 0; i < __OPEN_MAX; i++)
		curthread->t_filetable->vn[i] = NULL;

	curthread->t_filetable->ft_spinlock = (struct spinlock *)kmalloc(sizeof(struct spinlock));
	// if null ENOMEM
	spinlock_init(curthread->t_filetable->ft_spinlock);
	char path[5];
  	strcpy(path, "con:");
  	int result = vfs_open(path, O_RDWR, 0, &cons_vnode);
  	if (result)
  		return result;

	/* STDIN, STDOUT, and STDERR */
  	curthread->t_filetable->filecount = 3;

	int *refc = (int *)kmalloc(sizeof(int));
	int *refc1 = (int *)kmalloc(sizeof(int));
	int *refc2 = (int *)kmalloc(sizeof(int));
	*refc= 1;
	*refc1 = 1;
	*refc2 = 1;
	curthread->t_filetable->refcount[0] = refc;
	curthread->t_filetable->refcount[1] = refc1;
	curthread->t_filetable->refcount[2] = refc2;

	off_t *p1 = (off_t *)kmalloc(sizeof(off_t));
	off_t *p2 = (off_t *)kmalloc(sizeof(off_t));
	off_t *p3 = (off_t *)kmalloc(sizeof(off_t));
	*p1= 0;
	*p2 = 0;
	*p3 = 0;
	curthread->t_filetable->posinfile[0] = p1;
	curthread->t_filetable->posinfile[1] = p2;
	curthread->t_filetable->posinfile[2] = p3;

	curthread->t_filetable->vn[0] = cons_vnode;
	curthread->t_filetable->vn[1] = cons_vnode;
	curthread->t_filetable->vn[2] = cons_vnode;

	/* Assign newly-initialized filetable to current thread. */
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
	/* Close each file in filetable. */
	int i;
	for (i = 0; i < __OPEN_MAX; i++) {
		/* If fd is closed then skip it. */
		if ((ft->vn[i] == NULL) || (*(ft->refcount[i]) == 0))
			continue;
		ft->refcount[i]--;
		/* When the ref coutn is 0, we can close it. */
		if (ft->refcount[i] == 0) {
			*(ft->posinfile[i]) = 0;
			vfs_close(ft->vn[i]);
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
