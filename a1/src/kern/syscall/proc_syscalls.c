/*
 * Process-related syscalls.
 * New for ASST1.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <pid.h>
#include <machine/trapframe.h>
#include <syscall.h>
#include <copyinout.h>
#include <signal.h>

/*
 * sys_fork
 * 
 * create a new process, which begins executing in md_forkentry().
 */


int
sys_fork(struct trapframe *tf, pid_t *retval)
{
	struct trapframe *ntf; /* new trapframe, copy of tf */
	int result;

	/*
	 * Copy the trapframe to the heap, because we might return to
	 * userlevel and make another syscall (changing the trapframe)
	 * before the child runs. The child will free the copy.
	 */

	ntf = kmalloc(sizeof(struct trapframe));
	if (ntf==NULL) {
		return ENOMEM;
	}
	*ntf = *tf; /* copy the trapframe */

	result = thread_fork(curthread->t_name, enter_forked_process, 
			     ntf, 0, retval);
	if (result) {
		kfree(ntf);
		return result;
	}

	return 0;
}

/*
 * sys_getpid
 * Placeholder to remind you to implement this.
 */
pid_t sys_getpid(void) {
    return curthread->t_pid;
}

/*
 * sys_waitpid
 * Placeholder comment to remind you to implement this.
 */
int sys_waitpid(pid_t pid, userptr_t status, int flags, int* error) {
    int status_int, retval, result;
    // The flag must be set to WNOHANG or 0, otherwise the EINVAL error is 
    // raised. 
    // WNOHANG == 1 and the above include statement satisfies netbeans but
    // the compiler is not satisfied.
    if (!(flags == 1 || flags == 0)) {
	*error = EINVAL;
        return -1;
    // If the status argument is an invalid argument, it will return EFAULT.
    } else if (status == NULL) {
	*error = EFAULT;
        return -1;
    }
    // ESRCH and ECHILD type error situations are handled by pid_join.
    retval = pid_join(pid, &status_int, flags);
    result = copyout(&status_int, status, sizeof (int));
    if (result) {
        *error = result;
        return -1;
    }
    return retval;
}

/*
 * sys_kill
 * Placeholder comment to remind you to implement this.
 */

int
sys_kill(pid_t targetpid, int signal, int* error) {
    
    /* Invalid signal given */
    if  (signal > 31 || signal < 1) {
        error =  (int *) EINVAL;
        return -1;
    }
    // Handle all valid types of signals.
    switch (signal) {
        case (SIGHUP):
            return pid_setkillsig(targetpid, signal, (int *) *error);
            break;
        case (SIGINT):
            return pid_setkillsig(targetpid, signal, (int *) *error);
            break;
        case (SIGQUIT):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGILL):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGTRAP):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGABRT):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGEMT):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGFPE):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGKILL):
            return pid_setkillsig(targetpid, signal, (int *) *error);
            break;
        case (SIGBUS):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGSEGV):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGSYS):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGPIPE):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGALRM):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGTERM):
            return pid_setkillsig(targetpid, signal, (int *) *error);
            break;
        case (SIGURG):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGSTOP):
            return pid_setkillsig(targetpid, signal, (int *) *error);
            // lot to do
            break;
        case (SIGTSTP):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGCONT):
            return pid_setkillsig(targetpid, signal, (int *) *error);
            // lot to do
            break;
        case (SIGCHLD):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGTTIN):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGTTOU):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGIO):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGXCPU):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGXFSZ):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGVTALRM):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGPROF):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGWINCH):
            return 0;
            break;
        case (SIGINFO):
            return 0;
            break;
        case (SIGUSR1):
            error = (int *) EUNIMP;
            return -1;
            break;
        case (SIGUSR2):
            error = (int *) EUNIMP;
            return -1;
            break;
    }
    return -1;
}
