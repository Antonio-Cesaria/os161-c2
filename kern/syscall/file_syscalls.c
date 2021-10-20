/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys_read and sys_write.
 * just works (partially) on stdin/stdout
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <vfs.h>
#if OPT_SYSCALLS
#include <limits.h>
#include <kern/errno.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <elf.h>


#endif

struct vnode* filetable[OPEN_MAX];
int n_files=0;

/*
 * simple file system calls for write/read
 */
int
sys_write(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;
  int result;
  struct iovec iov;
  struct uio ku;

  KASSERT(fd>=0);
  KASSERT(size>0);

  if(fd == STDOUT_FILENO || fd == STDERR_FILENO){
    for (i=0; i<(int)size; i++) {
      putch(p[i]);
    }
  }
  else{
    if(filetable[fd] == NULL){
	kprintf("file not found in read\n");
  	return -1;
    }
    kprintf("TUTTO OK, sto scrivendo su fd=%d\n", fd);
    uio_kinit(&iov, &ku, p, size, 0, UIO_WRITE);
    result = VOP_WRITE(filetable[fd], &ku);
    if(result)
      return result;
  }

  return (int)size;
}

int
sys_read(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;
  int result;
  struct iovec iov;
  struct uio ku;

  KASSERT(fd>=0);
  KASSERT(size>0);
  
  if(fd == STDIN_FILENO){
    for (i=0; i<(int)size; i++) {
    p[i] = getch();
    if (p[i] < 0) 
      return i;
    }
  }
  else{
    if(filetable[fd] == NULL){
	kprintf("file not found in read\n");
  	return -1;
    }
    kprintf("TUTTO OK, sto leggendo da fd=%d\n", fd);
    uio_kinit(&iov, &ku, buf_ptr, size, 0, UIO_READ);
    result = VOP_READ(filetable[fd], &ku);
    if(result)
      return result;
  }
  
  return (int)size;
/*
  
  char *p = (char *)buf_ptr;

  KASSERT(fd>=0);
  KASSERT(size>0);

  for (i=0; i<(int)size; i++) {
    p[i] = getch();
    if (p[i] < 0) 
      return i;
  }

  return (int)size;*/
}

int
sys_open(char* filename, int flags, mode_t mode)
{
  int fd=0, err=0;
  if (filename==NULL) {
    kprintf("can't open a file from NULL filename\n");
    return -1;
  }
  if(n_files==0){
    for(int i=0;i<OPEN_MAX;i++){
      filetable[i]=NULL;
    }
  }
  for(fd=4;fd<OPEN_MAX;fd++){
    if(filetable[fd]==NULL){
      err = vfs_open(filename, flags, mode, &filetable[fd]);
      if(err){
	kprintf("trying to open a file that does not exists\n");
	return (int)-1;
      }
      kprintf("TUTTO OK, sto aprendo con fd=%d\n", fd);
      n_files++;
      return fd;
    }
  }

  return (int)-1;
}

int 
sys_close(int fd){
  if(filetable[fd] == NULL){
	kprintf("nothing to close\n");
  	return -1;
  }
  else{
    filetable[fd]=NULL;
    n_files--;
  }
  return 0;
}
