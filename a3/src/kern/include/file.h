/* BEGIN A3 SETUP */
/*
 * Declarations for file handle and file table management.
 * New for A3.
 */

#ifndef _FILE_H_
#define _FILE_H_

#include <kern/limits.h>

struct vnode;

/*
 * filetable struct
 * just an array, nice and simple.
 * It is up to you to design what goes into the array.  The current
 * array of ints is just intended to make the compiler happy.
 */
struct filetable {
	struct spinlock *ft_spinlock;
	struct vnode *vn[__OPEN_MAX];
	off_t *posinfile[__OPEN_MAX];
	int *refcount[__OPEN_MAX];
	//int mode[__OPEN_MAX];
	int filecount;
};

/* these all have an implicit arg of the curthread's filetable */
int filetable_init(void);
void filetable_destroy(struct filetable *ft);

/* opens a file (must be kernel pointers in the args) */
int file_open(char *filename, int flags, int mode, int *retfd);

/* closes a file */
int file_close(int fd);

/* A3: You should add additional functions that operate on
 * the filetable to help implement some of the filetable-related
 * system calls.
 */

#endif /* _FILE_H_ */

/* END A3 SETUP */
