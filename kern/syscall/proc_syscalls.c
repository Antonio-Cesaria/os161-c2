/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <current.h>
#include <synch.h>
#include <test.h>
#include <vfs.h>
#include <vm.h>
#include <kern/fcntl.h>

/*
 * system calls for process management
 */

int copyin_args(char*** args, char** uargs);

void
sys__exit(int status)
{
#if OPT_WAITPID
  struct proc *p = curproc;
  p->p_status = status & 0xff; /* just lower 8 bits returned */
  proc_remthread(curthread);
  proc_signal_end(p);
#else
  /* get address space of current process and destroy */
  struct addrspace *as = proc_getas();
  as_destroy(as);
#endif
  thread_exit();

  panic("thread_exit returned (should not happen)\n");
  (void) status; // TODO: status handling
}

int
sys_waitpid(pid_t pid, userptr_t statusp, int options)
{
#if OPT_WAITPID
  struct proc *p = proc_search_pid(pid);
  int s;
  (void)options; /* not handled */
  if (p==NULL) return -1;
  s = proc_wait(p);
  if (statusp!=NULL) 
    *(int*)statusp = s;
  return pid;
#else
  (void)options; /* not handled */
  (void)pid;
  (void)statusp;
  return -1;
#endif
}

pid_t
sys_getpid(void)
{
#if OPT_WAITPID
  KASSERT(curproc != NULL);
  return curproc->p_pid;
#else
  return -1;
#endif
}

pid_t
<<<<<<< HEAD
sys_getpid2(struct proc* p)
{
#if OPT_WAITPID
/*
  KASSERT(curproc != NULL);
  return curproc->p_pid;
*/
=======
sys_getpid2(struct proc *p)
{
#if OPT_WAITPID
>>>>>>> 94767025a17de2ce5cd08c51f110fb555a7b5ef3
  KASSERT(p != NULL);
  return p->p_pid;
#else
  return -1;
#endif
}

#if OPT_FORK
static void
call_enter_forked_process(void *tfv, unsigned long dummy) {
  struct trapframe *tf = (struct trapframe *)tfv;
  (void)dummy;
  enter_forked_process(tf); 
 
  panic("enter_forked_process returned (should not happen)\n");
}

int sys_fork(struct trapframe *ctf, pid_t *retval) {
  struct trapframe *tf_child;
  struct proc *newp;
  int result;

  KASSERT(curproc != NULL);

  newp = proc_create_runprogram(curproc->p_name);
  if (newp == NULL) {
    return ENOMEM;
  }

  /* done here as we need to duplicate the address space 
     of thbe current process */
  as_copy(curproc->p_addrspace, &(newp->p_addrspace));
  if(newp->p_addrspace == NULL){
    proc_destroy(newp); 
    return ENOMEM; 
  }

  proc_file_table_copy(newp,curproc);

  /* we need a copy of the parent's trapframe */
  tf_child = kmalloc(sizeof(struct trapframe));
  if(tf_child == NULL){
    proc_destroy(newp);
    return ENOMEM; 
  }
  memcpy(tf_child, ctf, sizeof(struct trapframe));

  /* TO BE DONE: linking parent/child, so that child terminated 
     on parent exit */

  result = thread_fork(
		 curthread->t_name, newp,
		 call_enter_forked_process, 
		 (void *)tf_child, (unsigned long)0/*unused*/);

  if (result){
    proc_destroy(newp);
    kfree(tf_child);
    return ENOMEM;
  }

  *retval = newp->p_pid;

  return 0;
}

int copyin_args(char*** args, char** uargs){
  
  int argc=0;
  int padding=0;
  char **kargs;

  while(uargs[argc] != NULL)
    argc++;
  //argc = sizeof(args)/sizeof(char*);
  kargs = (char**)kmalloc(argc * sizeof(char*));

  for(int i=0;i<argc;i++){
    padding = 4 - (strlen(uargs[i])%4);
    kargs[i] = (char*) kmalloc((strlen(uargs[i])+padding) * sizeof(char));
    kprintf("lunghezza %d\n", strlen(uargs[i]));
    memcpy(kargs[i], uargs[i], strlen(uargs[i]));
    for(int j=0;j<padding;j++)
      kargs[i][strlen(uargs[i])+j] = '\0';
  }
  *args = kargs;
  return 0;
}

int sys_execv(char* prgname, char** args){
  /*struct addrspace *as;
  struct proc *c = curproc;
  char *to_pass;
  (void)c;
  kprintf("progname: %s\n", progname);
  to_pass = (char*)kmalloc(strlen(progname) * sizeof(char));
  strcpy(to_pass, progname);
  as = proc_setas(NULL);
	as_deactivate();
  as_destroy(as);
  (void)progname;
  runprogram(to_pass);*/

  struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
  char* progname = NULL;
  char** argv = NULL;
  int argc=0;

  /*progname = (char*)kmalloc(strlen(prgname) * sizeof(char));
  while(args[argc] != NULL)
    argc++;
  //argc = sizeof(args)/sizeof(char*);
  argv = (char**)kmalloc(argc * sizeof(char*));
  for(int i=0;i<argc;i++){
    argv[i] = (char*) kmalloc(strlen(args[i]) * sizeof(char));
    strcpy(argv[i], args[i]);
  }
  strcpy(progname, prgname);
  */
  (void)prgname;
  copyin_args(&argv, args);

  kprintf("primo argomento: %s\n", (char*)args[1]);

  as = proc_setas(NULL);
	as_deactivate();
  as_destroy(as);

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

  kprintf("primo argomento: %s\n", (char*)argv[1]);

	/* Warp to user mode. */
	
  enter_new_process(argc /*argc*/, (userptr_t) argv /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

  return 0;
}

#endif
