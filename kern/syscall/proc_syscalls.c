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
#include <uio.h>

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
sys_waitpid(pid_t pid, userptr_t statusp, int options, int *err)
{
#if OPT_WAITPID
  
  struct proc *p;
  struct proc *cur = curproc;
  int *s=NULL;

  if(statusp == NULL){
    *err = 0;
    return -1; 
  }

  if(pid == cur->p_pid){
    *err = ECHILD;
    return -1;
  }

  if(pid == curproc->p_ppid){
    *err = ECHILD;
    return -1;
  }

  void *check = NULL;
  check = (void*) kmalloc(sizeof(void*));

  *err = copyin(statusp, check, sizeof(void*));
  if(*err)
    return -1;
  

  if(pid <= 0){
    *err = ECHILD;
    return -1;
  }

  s = (int*) kmalloc(sizeof(int));
  if(s == NULL) return -1;
  
  p = proc_search_pid(pid);
  if(p == NULL){
    *err = ECHILD;
    return -1;
  }

  (void)cur; //debugging purposes
  if(options != 0){ /* options not handled */
    *err = EINVAL;
    return -1; 
  }

  s = (int*) kmalloc(sizeof(int));
  if(s == NULL) return -1;
  
  p = proc_search_pid(pid);
  if(p == NULL){
    *err = ECHILD;
    return -1;
  }
  /**s = 444;
  *err = copyout((void*)s, statusp, sizeof(int));
    if(*err)
      return -1;*/

  *s = proc_wait(p);
  if (statusp!=NULL){
    //*(int*)statusp = s;
    //cur = curproc;
    *err = copyout((void*)s, statusp, sizeof(int));
    if(*err)
      return -1;
  }
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

  proc_file_table_copy(curproc, newp);

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
  
  char **kargs=NULL;
  size_t count;
  void *check=NULL;
  int i=0;
  int err;
  //counting number of incoming arguments

  err=copyin((userptr_t)uargs, &check, sizeof(void*));
  if(err)
    return err;

  /*
  while(uargs[*argc] != NULL){
    (*argc)++;
  }
  */
  while(check != NULL){
    err = copyin((userptr_t)uargs + i * 4, &check, sizeof(void*));
    if(err)
      return err;
    i++;
    *argc+=1;
  }
  (*argc)--;
  /*
  while((err = copyin((userptr_t)uargs + i * 4, &check, sizeof(check))) == 0 ) {
		if(check == NULL)
			break;
		err = copyinstr( (userptr_t)check, *kargs, sizeof(kargs), NULL );
		if( err ) 
			return err;
		
		++i;
		*argc += 1;
  }*/
  
  kargs = (char**) kmalloc(*argc * sizeof(char*));

  //loop for copying incoming arguments from user space to kernel space
  for(int i=0;i<(*argc);i++){
    kargs[i] = (char*) kmalloc(ARG_MAX * sizeof(char));
    err = copyinstr((userptr_t) uargs[i], kargs[i], ARG_MAX, &count);
    if(err){
      *argc = i;
      *args = kargs;
      return err;
    }
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
  int i, j, err;
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
    err = copyout((void*)&ptr, (userptr_t) *stackptr, sizeof(char*));
    if(err){
      kfree(padding);
      return err;
    }
    *stackptr += sizeof(char*);
  }
  ptr = NULL; //last string pointer
  err = copyout((void*)&ptr, (userptr_t) *stackptr, sizeof(char*));
  if(err){
    kfree(padding);
    return err;
  }
  *stackptr += sizeof(char*);

  //pushing the strings (arguments) in the correct order with correct padding into the stack
  for(i=0;i<argc;i++){
    err = copyoutstr(argv[i], (userptr_t) *stackptr, strlen(argv[i])+1, &cnt);
    if(err){
      kfree(padding);
      return err;
    }
    if(cnt != (strlen(argv[i])+1))
      panic("Copied wrong number of bytes!\n");
    *stackptr += (strlen(argv[i]) + 1);
    for(j=0;j<padding[i];j++){
      err = copyout(&term, (userptr_t) *stackptr, sizeof(char));
      if(err){
        kfree(padding);
        return err;
      }
      *stackptr += sizeof(char);
    }
  }

  //updating stackptr
  *stackptr -= size;
  kfree(padding);

  return 0;
}

int sys_execv(char* progname, char** args, int *err){
  
  struct addrspace *as, *as_old;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
  char** argv = NULL;
  int argc=0;
  struct proc *p = curproc;
  struct lock *exec_lock;
  char *k_prgname; //kernel space progname
  mode_t file_or_dir;

  /* parameter is unused beacuse progname is yet present in args array (args[0]) */
  //(void)progname;
  (void)p;
  /* copy program arguments from user space to kernel space */

  if(progname == NULL || args == NULL){
    *err = EFAULT;
    return -1;
  }
    

  exec_lock = lock_create("exec_lock");
  lock_acquire(exec_lock);

  *err = copyin_args(&argv, args, &argc);
  if(*err){
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);
    return -1;
  }
  k_prgname = (char*) kmalloc(ARG_MAX*sizeof(char)+1);
  *err = copyinstr((userptr_t)progname, k_prgname, ARG_MAX*sizeof(char)+1, NULL);
  if(*err){
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);
    lock_release(exec_lock);
    kfree(k_prgname);
    return -1;
  }
  /* detach the current address space */
  as_old = proc_setas(NULL);
  as_deactivate();
  //as_destroy(as_old);

	/* Open the file of the executable. */
	*err = vfs_open(k_prgname, O_RDONLY, 0, &v);
	if (*err) {
    kfree(k_prgname);
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);

    proc_setas(as_old);
    //as_activate();
    lock_release(exec_lock);
		return -1;
	}

  *err = VOP_GETTYPE(v, &file_or_dir);
  if(*err){
    kfree(k_prgname);
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);
    proc_setas(as_old);
    lock_release(exec_lock);
    return -1;
  }
  if(file_or_dir == S_IFDIR){
    kfree(k_prgname);
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);
    proc_setas(as_old);
    lock_release(exec_lock);
    *err = EISDIR;
    return -1;
  }

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);	
	
  /* Create a new address space. */
	as = as_create();
	if (as == NULL) {
    kfree(k_prgname);
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);
		vfs_close(v);
    lock_release(exec_lock);
    *err = ENOMEM;
		return -1;
	}

  /* Switch to it and activate it. */
  
  as_activate();
  proc_setas(as);

	/* Load the executable. */
	*err = load_elf(v, &entrypoint);
	if (*err) {
    kfree(k_prgname);
		/* p_addrspace will go away when curproc is destroyed */
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);

    as_deactivate();
    as_destroy(as);

    proc_setas(as_old); 
    //as_activate();

    vfs_close(v);
    lock_release(exec_lock);
		return -1;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	*err = as_define_stack(as, &stackptr);
	if (*err) {
    kfree(k_prgname);
		/* p_addrspace will go away when curproc is destroyed */
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);

    as_deactivate();
    as_destroy(as);

    proc_setas(as_old); 
    //as_activate();

    vfs_close(v);
    lock_release(exec_lock);
		return -1;
	}

  /* copying program arguments back from kernel space to user space (new address space) */
  *err = copyout_args(argv, &stackptr, argc);
  if(*err){
    kfree(k_prgname);
		/* p_addrspace will go away when curproc is destroyed */
    for(int i=0;i<argc;i++)
      kfree(argv[i]);
    kfree(argv);

    as_deactivate();
    as_destroy(as);

    proc_setas(as_old); 
    //as_activate();

    vfs_close(v);
    lock_release(exec_lock);
		return -1;
  }
  
  as_destroy(as_old);
  kfree(k_prgname);
  strcpy(curproc->p_name, argv[0]);
  lock_release(exec_lock);
  /* launch the new process */
  enter_new_process(argc /*argc*/, (userptr_t) stackptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
  *err = EINVAL;
	return -1;

  return 0;
}

int sys___getcwd(userptr_t buf, size_t size, int *retval){
  (void)buf;
  (void)size;

	int result, err;
	struct iovec iov;
	struct uio ku;
  char *kbuf;

  struct thread *t = curthread;
  (void)t;

  struct proc *p = curproc;
  (void)p;

  KASSERT( curthread != NULL);
  KASSERT( curthread->t_proc != NULL);

  if(buf == NULL)
    return EFAULT;

  kbuf = (char*) kmalloc(size*sizeof(char));
  if(kbuf == NULL)
    return -1; //correggi
  uio_kinit(&iov, &ku, kbuf, size, 0, UIO_READ);
  
  /*ku.uio_space = curthread->t_proc->p_addrspace;
  ku.uio_segflg = UIO_USERSPACE;*/

  result = vfs_getcwd(&ku);
  if (result) {
		kprintf("vfs_getcwd failed (%s)\n", strerror(result));
		return result;
	}
  
  /* null terminate */
	//kbuf[sizeof(kbuf)-1-ku.uio_resid] = 0;

  err = copyout((void*)kbuf, (userptr_t)buf, size);
  if(err)
    return err;
  *retval = size - ku.uio_resid;

  return 0;
}

int sys_chdir(userptr_t dir){
  (void)dir;
  struct vnode *v;
  char *kbuf=NULL;
  int result;

  if(dir == NULL)
    return EFAULT;

  result = copyin(dir, kbuf, sizeof(void*));
  if(result)
    return result;

  if(!strcmp((char*)dir, ""))
    return ENOTDIR;

  //need a copyinstr
  kbuf = (char*) kmalloc(strlen((char*)dir)*sizeof(char)+1);
  
  result = copyinstr(dir, kbuf, strlen((char*)dir)*sizeof(char)+1, NULL);
  if(result){
    kfree(kbuf);
    return result;
  }
  result = vfs_open(kbuf, O_RDONLY, 0, &v);
  if(result){
    kfree(kbuf);
    return result;
  }
  result = vfs_setcurdir(v);
  if(result){
    kfree(kbuf);
    return result;
  }
  vfs_close(v);
  //controllo
  return 0;
}

int sys_mkdir(userptr_t dir, mode_t mode){
  int result, err;
  size_t size;
  char *path_name;

  if(dir == NULL)
    return EFAULT;

  path_name = (char*) kmalloc(100*sizeof(char)); //correggi inserendo PATH_MAX costante
  if(path_name == NULL)
    return ENOMEM;
  
  err = copyinstr(dir, path_name, 100*sizeof(char), &size);
  if(err)
    return err;

  result = vfs_mkdir(path_name, mode);
  
  if(result){
    kfree(path_name);
    return result;
  }
  
  kfree(path_name);

  return 0;
}

int sys_rmdir(userptr_t dir){
  int result, err;
  size_t size;
  char *path_name;

  if(dir == NULL)
    return EFAULT;

  path_name = (char*) kmalloc(100*sizeof(char));  //correggere usando PATH_MAX costante
  if(path_name == NULL)
    return ENOMEM;

  err = copyinstr(dir, path_name, 100*sizeof(char), &size);
  if(err)
    return err;

  result = vfs_rmdir(path_name);
  if(result){
    kfree(path_name);
    return result;
  }

  kfree(path_name);

  return 0;
}

int sys_getdirentry(int fd, userptr_t buf, size_t buflen){
  struct vnode *vn;
  struct iovec io;
  struct uio ku;
  struct openfile *of;
  int result;
  char *kbuf;

  if(curproc->fileTable[fd] == NULL)
    return EBADF;
  
  of = (struct openfile *)curproc->fileTable[fd];
  if(of == NULL)
    return EBADF;
  vn = (struct vnode *)of->vn;

  kbuf = (char*) kmalloc(sizeof(char)*buflen);
  uio_kinit(&io, &ku, (void*)kbuf, buflen, 0, UIO_READ);

  //int spl = splhigh();
  result = VOP_GETDIRENTRY(vn, &ku);
  //splx(spl);

  if(result){
    kfree(kbuf);
    return result;
  }

  copyout((void*)kbuf, buf, buflen);
  kfree(kbuf);

  return 0;
}

#endif
