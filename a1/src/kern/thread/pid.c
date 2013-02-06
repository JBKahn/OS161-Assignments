/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process ID management.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <limits.h>
#include <lib.h>
#include <array.h>
#include <clock.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <pid.h>

/*
 * Structure for holding PID and return data for a thread.
 *
 * If pi_ppid is INVALID_PID, the parent has gone away and will not be
 * waiting. If pi_ppid is INVALID_PID and pi_exited is true, the
 * structure can be freed.
 */
struct pidinfo {
pid_t pi_pid;	// process id of this thread
pid_t pi_ppid;	// process id of parent thread
volatile bool pi_exited;	// true if thread has exited
int pi_exitstatus;	// status (only valid if exited)
struct cv *pi_cv;	// use to wait for thread exit
int pi_joinable;	// the pid is joinable
};


/*
 * Global pid and exit data.
 *
 * The process table is an el-cheapo hash table. It's indexed by
 * (pid % PROCS_MAX), and only allows one process per slot. If a
 * new pid allocation would cause a hash collision, we just don't
 * use that pid.
 */
static struct lock *pidlock;		// lock for global exit data
static struct pidinfo *pidinfo[PROCS_MAX]; // actual pid info
static pid_t nextpid;			// next candidate pid
static int nprocs;			// number of allocated pids



/*
 * Create a pidinfo structure for the specified pid.
 */
static
struct pidinfo *
pidinfo_create(pid_t pid, pid_t ppid)
{
	struct pidinfo *pi;

	KASSERT(pid != INVALID_PID);

	pi = kmalloc(sizeof(struct pidinfo));
	if (pi==NULL) {
		return NULL;
	}

	pi->pi_cv = cv_create("pidinfo cv");
	if (pi->pi_cv == NULL) {
		kfree(pi);
		return NULL;
	}

	pi->pi_pid = pid;
	pi->pi_ppid = ppid;
	pi->pi_exited = false;
	pi->pi_exitstatus = 0xbaad;  /* Recognizably invalid value */
        pi->pi_joinable = true; /* All processes are joinable when created. */

	return pi;
}

/*
 * Clean up a pidinfo structure.
 */
static
void
pidinfo_destroy(struct pidinfo *pi)
{
	KASSERT(pi->pi_exited == true);
	KASSERT(pi->pi_ppid == INVALID_PID);
	cv_destroy(pi->pi_cv);
	kfree(pi);
}

////////////////////////////////////////////////////////////

/*
 * pid_bootstrap: initialize.
 */
void
pid_bootstrap(void)
{
	int i;

	pidlock = lock_create("pidlock");
	if (pidlock == NULL) {
		panic("Out of memory creating pid lock\n");
	}

	/* not really necessary - should start zeroed */
	for (i=0; i<PROCS_MAX; i++) {
		pidinfo[i] = NULL;
	}

	pidinfo[BOOTUP_PID] = pidinfo_create(BOOTUP_PID, INVALID_PID);
	if (pidinfo[BOOTUP_PID]==NULL) {
		panic("Out of memory creating bootup pid data\n");
	}

	nextpid = PID_MIN;
	nprocs = 1;
}

/*
 * pi_get: look up a pidinfo in the process table.
 */
static
struct pidinfo *
pi_get(pid_t pid)
{
	struct pidinfo *pi;

	KASSERT(pid>=0);
	KASSERT(pid != INVALID_PID);
	KASSERT(lock_do_i_hold(pidlock));

	pi = pidinfo[pid % PROCS_MAX];
	if (pi==NULL) {
		return NULL;
	}
	if (pi->pi_pid != pid) {
		return NULL;
	}
	return pi;
}

/*
 * pi_put: insert a new pidinfo in the process table. The right slot
 * must be empty.
 */
static
void
pi_put(pid_t pid, struct pidinfo *pi)
{
	KASSERT(lock_do_i_hold(pidlock));

	KASSERT(pid != INVALID_PID);

	KASSERT(pidinfo[pid % PROCS_MAX] == NULL);
	pidinfo[pid % PROCS_MAX] = pi;
	nprocs++;
}

/*
 * pi_drop: remove a pidinfo structure from the process table and free
 * it. It should reflect a process that has already exited and been
 * waited for.
 */
static
void
pi_drop(pid_t pid)
{
	struct pidinfo *pi;

	KASSERT(lock_do_i_hold(pidlock));

	pi = pidinfo[pid % PROCS_MAX];
	KASSERT(pi != NULL);
	KASSERT(pi->pi_pid == pid);

	pidinfo_destroy(pi);
	pidinfo[pid % PROCS_MAX] = NULL;
	nprocs--;
}

////////////////////////////////////////////////////////////

/*
 * Helper function for pid_alloc.
 */
static
void
inc_nextpid(void)
{
	KASSERT(lock_do_i_hold(pidlock));

	nextpid++;
	if (nextpid > PID_MAX) {
		nextpid = PID_MIN;
	}
}

/*
 * pid_alloc: allocate a process id.
 */
int
pid_alloc(pid_t *retval)
{
	struct pidinfo *pi;
	pid_t pid;
	int count;

	KASSERT(curthread->t_pid != INVALID_PID);

	/* lock the table */
	lock_acquire(pidlock);

	if (nprocs == PROCS_MAX) {
		lock_release(pidlock);
		return EAGAIN;
	}

	/*
	 * The above test guarantees that this loop terminates, unless
	 * our nprocs count is off. Even so, assert we aren't looping
	 * forever.
	 */
	count = 0;
	while (pidinfo[nextpid % PROCS_MAX] != NULL) {

		/* avoid various boundary cases by allowing extra loops */
		KASSERT(count < PROCS_MAX*2+5);
		count++;

		inc_nextpid();
	}

	pid = nextpid;

	pi = pidinfo_create(pid, curthread->t_pid);
	if (pi==NULL) {
		lock_release(pidlock);
		return ENOMEM;
	}

	pi_put(pid, pi);

	inc_nextpid();

	lock_release(pidlock);

	*retval = pid;
	return 0;
}

/*
 * pid_unalloc - unallocate a process id (allocated with pid_alloc) that
 * hasn't run yet.
 */
void
pid_unalloc(pid_t theirpid)
{
	struct pidinfo *them;

	KASSERT(theirpid >= PID_MIN && theirpid <= PID_MAX);

	lock_acquire(pidlock);

	them = pi_get(theirpid);
	KASSERT(them != NULL);
	KASSERT(them->pi_exited == false);
	KASSERT(them->pi_ppid == curthread->t_pid);

	/* keep pidinfo_destroy from complaining */
	them->pi_exitstatus = 0xdead;
	them->pi_exited = true;
	them->pi_ppid = INVALID_PID;

	pi_drop(theirpid);

	lock_release(pidlock);
}

/*
 * pid_detach - disavows interest in the child thread's exit status, so 
 * it can be freed as soon as it exits. May only be called by the
 * parent thread.
 */

int pid_detach(pid_t childpid) {
    lock_acquire(pidlock);
    struct pidinfo *pi_child = pi_get(childpid);


    // Return ESRCH if no thread could be found corresponding to the target pid,
    // pi_child
    if (pi_child == NULL) {
        lock_release(pidlock); /* released the lock before return */
        return ESRCH;
    }
    /* Return EINVAL if:
     * i)The thread corresponding to pi_child has been detached.
     * i.e. pi_joinable == false.
     * ii) The current thread is not the parent of pi_child.
     * iii) The pi_child is INVALID_PID.
     * iv) The pi_child is BOOTUP_PID. */
    if (pi_child->pi_joinable != true || pi_child->pi_ppid != curthread->t_pid || pi_child->pi_ppid == INVALID_PID || pi_child->pi_ppid == BOOTUP_PID) {
        lock_release(pidlock); /* released the lock before return */
        return EINVAL;
    }

    /* Mark pi_child not joinable. */
    pi_child->pi_joinable = false;

    /* Check if pi_child has exited and if so, frees the memory and drops it 
     * from the pid table */
    if (pi_child->pi_exited == true) {
        pi_drop(pi_child->pi_ppid);
    }

    lock_release(pidlock); /* released the lock before return */
    return 0;
}

/*
 * pid_exit 
 *  - sets the exit status of this thread (i.e. curthread). 
 *  - disowns children. 
 *  - if dodetach is true, children are also detached. 
 *  - wakes any thread waiting for the curthread to exit. 
 *  - frees the PID and exit status if the curthread has been detached. 
 *  - must be called only if the thread has had a pid assigned.
 */
void
pid_exit(int status, bool dodetach)
{
	struct pidinfo *my_pi;
	
	lock_acquire(pidlock);
	my_pi = pi_get(curthread->t_pid);

	KASSERT(my_pi != NULL);
	my_pi->pi_exitstatus = status;
        my_pi->pi_exited = true;
        // Wake up threads that are waiting for the current thread's pid to 
        // exit.
        if (my_pi->pi_joinable) 
            cv_signal(my_pi->pi_cv, pidlock);
        // Looping through processes and if we are the parent, we detatch them.
        if (dodetach) {
            int i;
            for (i=0; i<PROCS_MAX; i++)
                if (pidinfo[i] != NULL && pidinfo[i]->pi_ppid == my_pi->pi_pid)
                        pid_detach(pidinfo[i]->pi_pid);
        }
        // Checks if the current thread's pid has been detached, and if so its
        // pid is set to INVALID_PID and it's dropped from the process list and
        // freed.
        if (!my_pi->pi_joinable) {
            my_pi->pi_ppid = INVALID_PID;
            pi_drop(my_pi->pi_pid);
        }

	lock_release(pidlock);
}

/*
 * pid_join - returns the exit status of the thread associated with
 * targetpid as soon as it is available. If the thread has not yet 
 * exited, curthread waits unless the flag WNOHANG is sent. 
 *
 */
int pid_join(pid_t targetpid, int *status, int flags) {
    flags = flags;
    lock_acquire(pidlock);
    struct pidinfo *target = pi_get(targetpid);


    // Return ESRCH if no thread could be found corresponding to the target pid,
    // pi_child
    if (target == NULL) {
        lock_release(pidlock); /* released the lock before return */
        return ESRCH;
    }
    /* Return EINVAL if:
     * i)The thread corresponding to pi_child has been detached.
     * i.e. pi_joinable == false.
     * ii) The pi_child is INVALID_PID.
     * iii) The pi_child is BOOTUP_PID. */
    if (target->pi_joinable == false || target->pi_pid == INVALID_PID || target->pi_pid == BOOTUP_PID) {
        /* Checks if the thread corresponding to target has been detached or if target is INVALID_PID or BOOTUP_PID.*/
        lock_release(pidlock); /* release lock before returning */
        return EINVAL;
    }
    // Return EDEADLK if the argument's pid refers to the calling thread's pid.
    if (target->pi_pid == curthread->t_pid) {
        lock_release(pidlock); /* released the lock before return */
        return EDEADLK;
    }
    // If the WHNOHANG flag is set, then the the current thread doesn't wait
    // for the thread to exit and simply returns 0 if it has not exited,
    // otherwise it waits for the thread to exit.
    if (target->pi_exited == false) {
        if (flags == WNOHANG) {
            lock_release(pidlock); /* released the lock before return */
            return(0);
        }
        cv_wait(target->pi_cv, pidlock);
        KASSERT(target->pi_exited == true); //Assertion used for debugging.
    }
    
    // Grab the status of the exited thread for use by the calling thread.
    *status = target->pi_exitstatus;

    lock_release(pidlock); /* released the lock before return */
    // Detach the pid now.
    // Then return that value to indicate the success of the call to pid_join.
    // Should return 0.
    return pid_detach(target->pi_pid);
}
