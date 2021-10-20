/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <current.h>
/*
 * simple proc management system calls
 */
void
sys__exit(int status)
{
  struct proc *p = curproc;
  p->status = status;
  proc_remthread(curthread); /* remove current thread from process */
  V(p->end_sem);
  /* get address space of current process and destroy */
  /*struct addrspace *as = proc_getas();
  as_destroy(as);*/
  /* thread exits. proc data structure will be lost */
  thread_exit();

  panic("thread_exit returned (should not happen)\n");
  (void) status; // TODO: status handling
}

/*
 * waits a process with a specific pid 
 */
int
sys_waitpid(pid_t pid)
{
  int result;
  struct proc *p;
  p = proc_search_pid(pid);
  result = proc_wait(p);
  return result;
}

/*
 * returns the pid related to the given process
 */

pid_t
sys_getpid(struct proc *proc)
{
	KASSERT(proc != NULL);
	return proc->pid;
}
