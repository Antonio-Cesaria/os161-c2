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
  
  
  //int padding=0;
  char **kargs;
  size_t count;

  while(uargs[*argc] != NULL)
    (*argc)++;
  //argc = sizeof(args)/sizeof(char*);
  kargs = (char**) kmalloc(*argc * sizeof(char*));

  for(int i=0;i<(*argc);i++){
    /*padding = 4 - (strlen(uargs[i])%4);*/
    kargs[i] = (char*) kmalloc((strlen(uargs[i])+1) * sizeof(char));
    //kprintf("lunghezza %d\n", strlen(uargs[i]));
    //memcpy(kargs[i], uargs[i], strlen(uargs[i]));
    copyinstr((userptr_t) uargs[i], kargs[i], strlen(uargs[i])+1, &count);
    if(count != (strlen(uargs[i])+1))
      panic("Copied wrong number of bytes!\n");
    /*for(int j=0;j<padding;j++)
      kargs[i][strlen(uargs[i])+j] = '\0';*/
  }
  *args = kargs;
  return 0;
}

int copyout_args(char** argv, vaddr_t *stackptr, int argc){
  /*
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
  int *padding = (int*) kmalloc(argc * sizeof(int)); //vettore padding stringhe
  char **ptr;
  
  (void)stackptr;

  for(i=0;i<argc;i++){
    size += (strlen(argv[i])+1); //1 per terminatore \0
    padding[i] = 4 - ( (strlen(argv[i]) % 4 )+1); //calcolo del padding necessario
    size += padding[i]; 
    size += sizeof(argv[i]); //per il puntatore alla stringa
    /*kprintf("padding[%d] = %d\n", i, padding[i]);
    kprintf("Size alla posizione %d = %d\n", i, size);*/
  }
  size += sizeof(char*); //NULL pointer to end args list
  // kprintf("Size totale = %d\n", size);
  *stackptr -= size;
  kprintf("stackptr = %8x\n", (int) *stackptr);
  int current_str_len=0;
  for(i=0;i<argc;i++){
    ptr = (char**) (*stackptr + (argc + 1 - i) * 4 + current_str_len);
    current_str_len += (strlen(argv[i])+padding[i]+1);
    copyout((void*)&ptr, (userptr_t) *stackptr, sizeof(char*));
    *stackptr += sizeof(char*);
  }
  ptr = NULL;
  copyout((void*)&ptr, (userptr_t) *stackptr, sizeof(char*));
  *stackptr += sizeof(char*);

  //inserimento delle stringhe nello stack
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

  *stackptr -= size;
  
  char** args = (char**) *stackptr;
  kprintf("nome prg = %s\n", args[0]);
  kprintf("nome primo param = %s\n", args[1]);
  kprintf("NULL = %s\n", args[argc]);
  
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
  //char* progname = NULL;
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
  copyin_args(&argv, args, &argc);

  kprintf("primo argomento: %s\n", (char*)argv[1]);
  kprintf("secondo argomento: %s\n", (char*)argv[2]);

  as = proc_setas(NULL);
	as_deactivate();
  as_destroy(as);

	/* Open the file. */
	result = vfs_open(argv[0], O_RDONLY, 0, &v);
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

	//COPYOUT!!!!!!!
  /*
  COPIARE ARGOMENTI (argv) NELLO USER STACK DEL NUOVO ADDRESS SPACE
  */
  copyout_args(argv, &stackptr, argc);
  kprintf("stackptr = %8x\n", (int) stackptr);

  enter_new_process(argc /*argc*/, (userptr_t) stackptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

  return 0;
}

#endif
