/* BEGIN A3 SETUP */
/* This file existed for A1 and A2, but has been completely replaced for A3.
 * We have kept the dumb versions of sys_read and sys_write to support early
 * testing, but they should be replaced with proper implementations that 
 * use your open file table to find the correct vnode given a file descriptor
 * number.  All the "dumb console I/O" code should be deleted.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <copyinout.h>
#include <synch.h>
#include <file.h>

/* This special-case global variable for the console vnode should be deleted 
 * when you have a proper open file table implementation.
 */
struct vnode *cons_vnode=NULL; 

/* This function should be deleted, including the call in main.c, when you
 * have proper initialization of the first 3 file descriptors in your 
 * open file table implementation.
 * You may find it useful as an example of how to get a vnode for the 
 * console device.
 */
void dumb_consoleIO_bootstrap()
{
  int result;
  char path[5];

  /* The path passed to vfs_open must be mutable.
   * vfs_open may modify it.
   */

  strcpy(path, "con:");
  result = vfs_open(path, O_RDWR, 0, &cons_vnode);

  if (result) {
    /* Tough one... if there's no console, there's not
     * much point printing a warning...
     * but maybe the bootstrap was just called in the wrong place
     */
    kprintf("Warning: could not initialize console vnode\n");
    kprintf("User programs will not be able to read/write\n");
    cons_vnode = NULL;
  }
}

/*
 * mk_useruio
 * sets up the uio for a USERSPACE transfer. 
 */
static
void
mk_useruio(struct iovec *iov, struct uio *u, userptr_t buf, 
	   size_t len, off_t offset, enum uio_rw rw)
{

	iov->iov_ubase = buf;
	iov->iov_len = len;
	u->uio_iov = iov;
	u->uio_iovcnt = 1;
	u->uio_offset = offset;
	u->uio_resid = len;
	u->uio_segflg = UIO_USERSPACE;
	u->uio_rw = rw;
	u->uio_space = curthread->t_addrspace;
}

/*
 * sys_open
 * just copies in the filename, then passes work to file_open.
 * You have to write file_open.
 * 
 */
int
sys_open(userptr_t filename, int flags, int mode, int *retval)
{
	char *fname;
	int result;

	if ( (fname = (char *)kmalloc(__PATH_MAX)) == NULL) {
		return ENOMEM;
	}

	result = copyinstr(filename, fname, __PATH_MAX, NULL);
	if (result) {
		kfree(fname);
		return result;
	}

	result =  file_open(fname, flags, mode, retval);
	kfree(fname);
	return result;
}

/* 
 * sys_close
 * You have to write file_close.
 */
int
sys_close(int fd)
{
	return file_close(fd);
}

/*
 * sys_dup2
 * dup2 clones the file handle oldfd onto the file handle
 * newfd. If newfd names an open file, that file is closed.
 *
 */
int
sys_dup2(int oldfd, int newfd, int *retval)
{
	/* Check fd ranges */
	if ((newfd < 0) || (newfd >= __OPEN_MAX) || (oldfd < 0) || (oldfd >= __OPEN_MAX)) {
		*retval = EBADF;
		return -1;
	}
	/* Is the old fd real */
	if ((curthread->t_filetable->oft[oldfd] == NULL)) {
		*retval = EBADF;
		return -1;
	}
	/* Trivial case of them already being the same, no work to do. */
	if (oldfd == newfd) {
		*retval = newfd;
		return 0;
	}
	*retval = newfd;
	/* If newfd names an open file, that file is closed, as per man page */
	if (curthread->t_filetable->oft[newfd] != NULL)
		sys_close(newfd);
	/* Redirect newfd to oldfd */
	curthread->t_filetable->oft[newfd] = curthread->t_filetable->oft[oldfd];
	/* Update ref count */
	curthread->t_filetable->oft[newfd]->refcount++;
	return 0;
}

/*
 * sys_read
 * calls VOP_READ.
 * 
 * A3: This is the "dumb" implementation of sys_write:
 * it only deals with file descriptors 1 and 2, and 
 * assumes they are permanently associated with the 
 * console vnode (which must have been previously initialized).
 *
 * In your implementation, you should use the file descriptor
 * to find a vnode from your file table, and then read from it.
 *
 * Note that any problems with the address supplied by the
 * user as "buf" will be handled by the VOP_READ / uio code
 * so you do not have to try to verify "buf" yourself.
 *
 * Most of this code should be replaced.
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
	struct uio user_uio;
	struct iovec user_iov;
	int result;
	int offset = 0;
	struct file *toread;

	/* better be a valid file descriptor */
	if (fd < 0 || fd >= __OPEN_MAX)
	    return EBADF;

	toread = curthread->t_filetable->oft[fd];
	/* Is this an open file? If not, we can't read. */
	if ((toread == NULL) || (toread->refcount == 0))
		return EBADF;
	/* set up a uio with the buffer, its size, and the current offset */
	mk_useruio(&user_iov, &user_uio, buf, size,  toread->posinfile, UIO_READ);

	/* does the read */
	result = VOP_READ(toread->vn, &user_uio);
	if (result)
		return result;

	/* VOP read should have set uio_offset correctly so we can use that value.*/
	toread->filepos = user_uio.uio_offset;
	/* Size of buffer minus the size remaining in the buffer = size written.*/
	*retval = size - user_uio.uio_resid;

	return 0;
}

/*
 * sys_write
 * calls VOP_WRITE.
 *
 * A3: This is the "dumb" implementation of sys_write:
 * it only deals with file descriptors 1 and 2, and
 * assumes they are permanently associated with the
 * console vnode (which must have been previously initialized).
 *
 * In your implementation, you should use the file descriptor
 * to find a vnode from your file table, and then read from it.
 *
 * Note that any problems with the address supplied by the
 * user as "buf" will be handled by the VOP_READ / uio code
 * so you do not have to try to verify "buf" yourself.
 *
 * Most of this code should be replaced.
 */

int
sys_write(int fd, userptr_t buf, size_t len, int *retval)
{
	struct uio user_uio;
	struct iovec user_iov;
	int result;
	int offset = 0;
	struct file *towrite;

	/* better be a valid file descriptor */
	if (fd < 0 || fd >= __OPEN_MAX)
	    return EBADF;

	towrite = curthread->t_filetable->oft[fd];
	/* Is this an open file? If not, we can't read. */
	if ((towrite == NULL) || (towrite->refcount == 0))
		return EBADF;

    /* set up a uio with the buffer, its size, and the current offset */
    mk_useruio(&user_iov, &user_uio, buf, len, towrite->filepos, UIO_WRITE);

    /* does the write */
    result = VOP_WRITE(towrite->vn, &user_uio);
    if (result)
        return result;

	/* VOP read should have set uio_offset correctly so we can use that value.*/
	towrite->filepos = user_uio.uio_offset;
	/* Size of buffer minus the size remaining in the buffer = size written.*/
    *retval = len - user_uio.uio_resid;

    return 0;
}

/*
 * sys_lseek
 * 
 */
int
sys_lseek(int fd, off_t offset, int whence, off_t *retval)
{
	struct file *filetoseek;
	if (fd < 0 || fd >= __OPEN_MAX)
		return EBADF; /* Check fd range */

	filetoseek = curthread->t_filetable->oft[fd];

	/* Is this an open file? If not, we can't read. */
	if ((filetoseek == NULL) || (filetoseek->refcount == 0))
		return EBADF;

	/* Calculate new offset. */
	int newoffset;
	if (whence == SEEK_SET) {
		/* the file offset shall be set to offset bytes. */
		newoffset = offset;
	} else if (whence == SEEK_CUR) {
		/* the file offset shall be set to its current location plus offset. */
		newoffset = filetoseek->filepos + offset;
	} else if (whence == SEEK_END) {
		struct stat st;
		int err;
		err = VOP_STAT(filetoseek->vn, &st);
		if (err)
			return err;
		/* the file offset shall be set to the size of the file plus offset. */
		newoffset = st.st_size + offset;
	/* Bad argument passed */
	} else {
		return EINVAL;
	}

	 /* Check if seeking to the specified position within the file is legal. */
	err = VOP_TRYSEEK(filetoseek->vn, newoffset);
	if (err)
		return err;

	*retval = newoffset;
	return 0;
}


/* really not "file" calls, per se, but might as well put it here */

/*
 * sys_chdir
 * 
 */
int
sys_chdir(userptr_t path)
{
	struct vnode *new_dir;
	int error;
	char *fullpath;

	if ((fullpath = (char *)kmalloc(__PATH_MAX)) == NULL)
		return ENOMEM;

	/* Copy path into fullpath. */
	error = copyinstr(path, fullpath, __PATH_MAX, NULL);
	if (error) {
		kfree(fullpath);
		return error;
	}

	/* Get the vnode for the new path/directory. */
	error = vfs_lookup(fullpath, &new_dir);
	if (error)
		return error;

	/* Set the new current working directory. */
	error = vfs_setcurdir(new_dir);
	if (error)
		return error;

	return 0;
}

/*
 * sys___getcwd
 *
 */
int
sys___getcwd(userptr_t buf, size_t buflen, int *retval)
{
	struct vnode *cwd_vn = curthread->t_cwd;
	struct uio user_uio;
	struct iovec user_iov;

	/* Error is there is no current working directory. */
	if (cwd_vn == NULL) {
		return ENOENT;
	}

	/* make uio with the buffer and the size of the cwd. */
	mk_useruio(&user_iov, &user_uio, buf, buflen, 0, UIO_READ);

	/* Compute pathname relative to filesystem root of the file and copy to the specified uio */
	int error = VOP_NAMEFILE(cwd_vn, &user_uio);
	if (error)
		return error;

	/* Size of buffer minus the size remaining in the buffer = size written.*/
	*retval = buflen - user_uio.uio_resid;

	return 0;
}

/*
 * sys_fstat
 */
int
sys_fstat(int fd, userptr_t statptr)
{
	struct file *statfile;
	struct stat statbuf;

	if (fd < 0 || fd >= __OPEN_MAX)
		 return EBADF;

	statfile = curthread->t_filetable->oft[fd];
	/* Is this an open file? If not, we can't stat it. */
	if ((statfile == NULL) || (statfile->refcount == 0))
		return EBADF;

	/* Put stats in statbuf. */
	int error = VOP_STAT(statfile->vn, &statbuf);
	if (error)
		return error;

	/* Copyout statbuf to statptr. */
	error = copyout(&statbuf, statptr, sizeof statbuf);
	if (error)
		return error;

	return 0;
}

/*
 * sys_getdirentry
 */
int
sys_getdirentry(int fd, userptr_t buf, size_t buflen, int *retval)
{
	struct uio user_uio;
	struct iovec user_iov;
	struct file *getdir;

	if (fd < 0 || fd >= __OPEN_MAX)
		 return EBADF;

	getdir = curthread->t_filetable->oft[fd];

	/* Is this an open file? There's not much we can do if it isn't. */
	if ((statfile == NULL) || (statfile->refcount == 0))
		return EBADF;

	/* Set up a uio*/
	mk_useruio(&user_iov, &user_uio, buf, buflen, 0, UIO_READ);

	/* Get the directory */
	int error = VOP_GETDIRENTRY(getdir->vn, &user_uio);
	if (error)
		return error;

	/* Size of buffer minus the size remaining in the buffer = size written.*/
	*retval = buflen - user_uio.uio_resid;

	return 0;
}

/* END A3 SETUP */
