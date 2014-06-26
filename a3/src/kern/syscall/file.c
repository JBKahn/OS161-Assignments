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
	char fname[__PATH_MAX];
	strcpy(fname, filename);
	struct vnode *newvn;

	if(curthread->t_filetable->filecount == __OPEN_MAX)
		return ENFILE;

	int openerr = vfs_open(fname, flags, mode, &newvn);
	if (openerr)
		return openerr; // File open failed

	/* Find free slot in open file table. */
	int i;
	for (i = 0; curthread->t_filetable->vn[i] != NULL; i++) {
		if (i > (__OPEN_MAX))
			return ENFILE;
	}

	curthread->t_filetable->vn[i] = newvn;
	curthread->t_filetable->filecount++;


	int *newref = (int *)kmalloc(sizeof(int));
	if (newref == NULL)
		return ENOMEM;
	*newref= 0;
	curthread->t_filetable->refcount[i] = newref;

	off_t *newpos = (off_t *)kmalloc(sizeof(off_t));
	if (newpos == NULL)
		return ENOMEM;
	*newpos= 0;
	curthread->t_filetable->posinfile[i] = newpos;

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
	spinlock_acquire(curthread->t_filetable->ft_spinlock);
	struct vnode *oldvn = curthread->t_filetable->vn[fd];
	if ((curthread->t_filetable->refcount[fd] == 0)) {
		spinlock_release(curthread->t_filetable->ft_spinlock);
		return EBADF; // file is closed.
	}
	if ((oldvn == NULL)) {
		spinlock_release(curthread->t_filetable->ft_spinlock);
		return EIO; // file is closed.
	}
	curthread->t_filetable->refcount[fd]--;
	/* When the ref coutn is 0, we can close it. */
	if ((curthread->t_filetable)->refcount[fd] == 0) {
		kfree(curthread->t_filetable->posinfile[fd]);
		kfree(curthread->t_filetable->refcount[fd]);
		spinlock_release(curthread->t_filetable->ft_spinlock);
		vfs_close(oldvn);
		spinlock_acquire(curthread->t_filetable->ft_spinlock);
	}
	curthread->t_filetable->vn[fd] = NULL;
	curthread->t_filetable->filecount--;
	spinlock_release(curthread->t_filetable->ft_spinlock);
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
	curthread->t_filetable->filecount = 0;
	int i;
	for (i = 0; i < __OPEN_MAX; i++) {
		curthread->t_filetable->vn[i] = NULL;
		curthread->t_filetable->refcount[i] = NULL;
		curthread->t_filetable->posinfile[i] = NULL;
	}

  	/* STDIN, STDOUT, and STDERR */
	for (i = 0; i < 3; i++) {
		char path[5];
		strcpy(path, "con:");
	  	int result = vfs_open(path, O_RDWR, 0, &cons_vnode);
	  	curthread->t_filetable->vn[i] = cons_vnode;
	  	if (result)
  			return ENODEV;
		int *refc = (int *)kmalloc(sizeof(int));
		if (refc == NULL)
			return ENOMEM;
		*refc= 1;
		curthread->t_filetable->refcount[i] = refc;

		off_t *pos = (off_t *)kmalloc(sizeof(off_t));
		if (pos == NULL)
			return ENOMEM;
		*pos= 0;
		curthread->t_filetable->posinfile[i] = pos;
		cons_vnode = NULL;
	}
	curthread->t_filetable->ft_spinlock = (struct spinlock *)kmalloc(sizeof(struct spinlock));
	if(curthread->t_filetable->ft_spinlock == NULL)
		return ENOMEM;
	spinlock_init(curthread->t_filetable->ft_spinlock);
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
	spinlock_acquire(ft->ft_spinlock);
	for (i = 0; i < __OPEN_MAX; i++) {
		/* If fd is closed then skip it. */
		if (ft->vn[i] == NULL)
			continue;
		if (*(ft->refcount[i]) > (off_t)0 )	
			ft->refcount[i]--;
		/* When the ref coutn is 0, we can close it. */
		if (ft->refcount[i] == 0) {
			vfs_close(ft->vn[i]);
			kfree(ft->posinfile[i]);
			kfree(ft->refcount[i]);
		}
	}
	spinlock_release(ft->ft_spinlock);
	spinlock_cleanup(ft->ft_spinlock);
	kfree(ft); /* Free memory */
	return;
}



/*
 * You should add additional filetable utility functions here as needed
 * to support the system calls.  For example, given a file descriptor
 * you will want some sort of lookup function that will check if the fd is
 * valid and return the associated vnode (and possibly other information like
 * the current file position) associated with that open file.
 */


/* END A3 SETUP */

int
check_valid_fd(int fd)
{
	KASSERT(spinlock_do_i_hold(curthread->t_filetable->ft_spinlock));
	/* better be a valid file descriptor */
	if (fd < 0 || fd >= __OPEN_MAX)
	    return EBADF;

	/* Is this an open file? If not, we can't use it. */
	if ((curthread->t_filetable->vn[fd] == NULL) || (curthread->t_filetable->refcount[fd] == 0))
		return EBADF;
	return 0;
}

struct filetable*
filetable_copy(void) {
	struct filetable *newtable;
	newtable = (struct filetable *)kmalloc(sizeof(struct filetable));
	newtable->ft_spinlock = curthread->t_filetable->ft_spinlock;

	spinlock_acquire(curthread->t_filetable->ft_spinlock);
	int i;
	for (i = 0; i < __OPEN_MAX; i++) {
		if(curthread->t_filetable->vn[i] != NULL){
			*(curthread->t_filetable->refcount[i]) = *(curthread->t_filetable->refcount[i]) + 1;
			newtable->vn[i] = curthread->t_filetable->vn[i];
			newtable->posinfile[i] = curthread->t_filetable->posinfile[i];
			newtable->refcount[i] = curthread->t_filetable->refcount[i];
		}
	}
	newtable->filecount = curthread->t_filetable->filecount;

	spinlock_release(curthread->t_filetable->ft_spinlock);
	return newtable;
}
