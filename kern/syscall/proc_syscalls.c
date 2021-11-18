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
#include <stat.h>
#include <test.h>
#include <vfs.h>
#include <vm.h>
#include <kern/fcntl.h>
#include <vnode.h>

/*
 * system calls for process management
 */

int copyin_args(char*** args, char** uargs, int* argc);
int copyout_args(char** args, vaddr_t *stackptr, int argc);

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
sys_getpid2(struct proc* p)
{
#if OPT_WAITPID
/*
  KASSERT(curproc != NULL);
  return curproc->p_pid;
*/
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

int copyin_args(char*** args, char** uargs, int *argc){
  
  char **kargs;
  size_t count;

  //counting number of incoming arguments
  while(uargs[*argc] != NULL)
    (*argc)++;
  
  kargs = (char**) kmalloc(*argc * sizeof(char*));

  //loop for copying incoming arguments from user space to kernel space
  for(int i=0;i<(*argc);i++){
    kargs[i] = (char*) kmalloc((strlen(uargs[i])+1) * sizeof(char));
    copyinstr((userptr_t) uargs[i], kargs[i], strlen(uargs[i])+1, &count);
    //checks right number of bytes have been copied
    if(count != (strlen(uargs[i])+1))
      panic("Copied wrong number of bytes!\n");
  }
  *args = kargs;

  return 0;
}

int copyout_args(char** argv, vaddr_t *stackptr, int argc){
  /*
  dev notes
  1 - calcolare dimensione totale della matrice (NB considerando le sringhe paddate!!!!!)
  1a - Costruire un vettore contenente i padding di ogni stringa
  2 - decrementare stackptr in base alla size totale (NB anche i puntatori)
  3 - pushare nello stackptr aggiornato PRIMA i puntatori, POI le stringhe paddate a multipli di 4
      (MIPS accetta parametri allineati a multipli di 4 byte!!)
  */

  size_t size=0;
  int i, j;
  size_t cnt;
  char term = '\0';
  int *padding = (int*) kmalloc(argc * sizeof(int)); //string padding array
  char **ptr;
  
  (void)stackptr;

  for(i=0;i<argc;i++){
    size += (strlen(argv[i])+1); //+1 for \0 string terminator
    padding[i] = 4 - ( (strlen(argv[i]) % 4 )+1); //computing necessary padding
    size += padding[i]; 
    size += sizeof(argv[i]); //for string pointer
  }
  size += sizeof(char*); //NULL pointer to end args list
  
  /*decrementing stackptr in order to align to the correct byte where the data
  structure for the arguments will be placed */
  *stackptr -= size; 

  /*
  Loop for copying string pointers into user space stack (included NULL last pointer)
  */
  int current_str_len=0; 
  for(i=0;i<argc;i++){
    ptr = (char**) (*stackptr + (argc + 1 - i) * 4 + current_str_len);
    current_str_len += (strlen(argv[i])+padding[i]+1);
    copyout((void*)&ptr, (userptr_t) *stackptr, sizeof(char*));
    *stackptr += sizeof(char*);
  }
  ptr = NULL; //last string pointer
  copyout((void*)&ptr, (userptr_t) *stackptr, sizeof(char*));
  *stackptr += sizeof(char*);

  //pushing the strings (arguments) in the correct order with correct padding into the stack
  for(i=0;i<argc;i++){
    copyoutstr(argv[i], (userptr_t) *stackptr, strlen(argv[i])+1, &cnt);
    if(cnt != (strlen(argv[i])+1))
      panic("Copied wrong number of bytes!\n");
    *stackptr += (strlen(argv[i]) + 1);
    for(j=0;j<padding[i];j++){
      copyout(&term, (userptr_t) *stackptr, sizeof(char));
      *stackptr += sizeof(char);
    }
  }

  //updating stackptr
  *stackptr -= size;
  kfree(padding);

  return 0;
}

int sys_execv(char* progname, char** args){
  
  struct addrspace *as, *as_old;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
  char** argv = NULL;
  int argc=0;
  struct proc *p = curproc;

  mode_t file_or_dir;

  /* parameter is unused beacuse progname is yet present in args array (args[0]) */
  (void)progname;
  (void)p;
  /* copy program arguments from user space to kernel space */
  copyin_args(&argv, args, &argc);

  /* detach the current address space */
  as_old = proc_setas(NULL);
  as_deactivate();
  //as_destroy(as_old);

	/* Open the file of the executable. */
	result = vfs_open(argv[0], O_RDONLY, 0, &v);
	if (result) {
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);

    proc_setas(as_old);
    //as_activate();

		return result;
	}

  result = VOP_GETTYPE(v, &file_or_dir);
  if(file_or_dir == S_IFDIR){
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);
    proc_setas(as_old);
    return EISDIR;
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
  
  as_activate();
  proc_setas(as);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);

    as_deactivate();
    as_destroy(as);

    proc_setas(as_old); 
    //as_activate();

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

  /* copying program arguments back from kernel space to user space (new address space) */
  copyout_args(argv, &stackptr, argc);
  
  as_destroy(as_old);

  /* launch the new process */
  enter_new_process(argc /*argc*/, (userptr_t) stackptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

  return 0;
}

#endif
