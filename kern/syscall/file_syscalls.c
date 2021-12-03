/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys_read and sys_write.
 * just works (partially) on stdin/stdout
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <clock.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>

#if OPT_FILE

#include <copyinout.h>
#include <vnode.h>
#include <vfs.h>
#include <limits.h>
#include <uio.h>
#include <proc.h>

#include <synch.h>

/* max num of system wide open files */
#define SYSTEM_OPEN_MAX (10*OPEN_MAX)

#define SEEK_SET      0      /* Seek relative to beginning of file */
#define SEEK_CUR      1      /* Seek relative to current position in file */
#define SEEK_END      2      /* Seek relative to end of file */

#define USE_KERNEL_BUFFER 0

/* system open file table */
/*
struct openfile {
  struct vnode *vn;
  off_t offset;	
  unsigned int countRef;
};
*/

struct openfile systemFileTable[SYSTEM_OPEN_MAX];

void openfileIncrRefCount(struct openfile *of) {
  if (of!=NULL)
    of->countRef++;
}

int file_get(struct proc *p, int fd, struct openfile **f );

#if USE_KERNEL_BUFFER

static int
file_read(int fd, userptr_t buf_ptr, size_t size) {
  struct iovec iov;
  struct uio ku;
  int result, nread;
  struct vnode *vn;
  struct openfile *of;
  void *kbuf;

  if (fd<0||fd>OPEN_MAX) return -1;
  of = curproc->fileTable[fd];
  if (of==NULL) return -1;
  vn = of->vn;
  if (vn==NULL) return -1;

  kbuf = kmalloc(size);
  uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_READ);
  result = VOP_READ(vn, &ku);
  if (result) {
    return result;
  }
  of->offset = ku.uio_offset;
  nread = size - ku.uio_resid;
  copyout(kbuf,buf_ptr,nread);
  kfree(kbuf);
  return (nread);
}

static int
file_write(int fd, userptr_t buf_ptr, size_t size) {
  struct iovec iov;
  struct uio ku;
  int result, nwrite;
  struct vnode *vn;
  struct openfile *of;
  void *kbuf;

  if (fd<0||fd>OPEN_MAX) return -1;
  of = curproc->fileTable[fd];
  if (of==NULL) return -1;
  vn = of->vn;
  if (vn==NULL) return -1;

  kbuf = kmalloc(size);
  copyin(buf_ptr,kbuf,size);
  uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_WRITE);
  result = VOP_WRITE(vn, &ku);
  if (result) {
    return result;
  }
  kfree(kbuf);
  of->offset = ku.uio_offset;
  nwrite = size - ku.uio_resid;
  return (nwrite);
}

#else

static int
file_read(int fd, userptr_t buf_ptr, size_t size) {
  struct iovec iov;
  struct uio u;
  int result;
  struct vnode *vn;
  struct openfile *of;

  if (fd<0||fd>OPEN_MAX) return -1;
  of = curproc->fileTable[fd];
  if (of==NULL) return -1;
  vn = of->vn;
  if (vn==NULL) return -1;

  iov.iov_ubase = buf_ptr;
  iov.iov_len = size;

  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_resid = size;          // amount to read from the file
  u.uio_offset = of->offset;
  u.uio_segflg =UIO_USERISPACE;
  u.uio_rw = UIO_READ;
  u.uio_space = curproc->p_addrspace;

  result = VOP_READ(vn, &u);
  if (result) {
    return result;
  }

  of->offset = u.uio_offset;
  return (size - u.uio_resid);
}

static int
file_write(int fd, userptr_t buf_ptr, size_t size) {
  struct iovec iov;
  struct uio u;
  int result, nwrite;
  struct vnode *vn;
  struct openfile *of;

  if (fd<0||fd>OPEN_MAX) return -1;
  of = curproc->fileTable[fd];
  if (of==NULL) return -1;
  vn = of->vn;
  if (vn==NULL) return -1;

  iov.iov_ubase = buf_ptr;
  iov.iov_len = size;

  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_resid = size;          // amount to read from the file
  u.uio_offset = of->offset;
  u.uio_segflg =UIO_USERISPACE;
  u.uio_rw = UIO_WRITE;
  u.uio_space = curproc->p_addrspace;

  result = VOP_WRITE(vn, &u);
  if (result) {
    return result;
  }
  of->offset = u.uio_offset;
  nwrite = size - u.uio_resid;
  return (nwrite);
}

#endif

/*
 * file system calls for open/close
 */
int
sys_open(userptr_t path, int openflags, mode_t mode, int *errp)
{
  int fd, i;
  struct vnode *v;
  struct openfile *of=NULL;; 	
  int result;

  result = vfs_open((char *)path, openflags, mode, &v);
  if (result) {
    *errp = ENOENT;
    return -1;
  }
  /* search system open file table */
  for (i=0; i<SYSTEM_OPEN_MAX; i++) {
    if (systemFileTable[i].vn==NULL) {
      of = &systemFileTable[i];
      of->vn = v;
      of->offset = 0; // TODO: handle offset with append
      of->countRef = 1;
      break;
    }
  }
  if (of==NULL) { 
    // no free slot in system open file table
    *errp = ENFILE;
  }
  else {
    for (fd=STDERR_FILENO+1; fd<OPEN_MAX; fd++) {
      if (curproc->fileTable[fd] == NULL) {
	curproc->fileTable[fd] = of;
	return fd;
      }
    }
    // no free slot in process open file table
    *errp = EMFILE;
  }
  
  vfs_close(v);
  return -1;
}

/*
 * file system calls for open/close
 */
int
sys_close(int fd)
{
  struct openfile *of=NULL; 
  struct vnode *vn;

  if (fd<0||fd>OPEN_MAX) return -1;
  of = curproc->fileTable[fd];
  if (of==NULL) return -1;
  curproc->fileTable[fd] = NULL;

  if (--of->countRef > 0) return 0; // just decrement ref cnt
  vn = of->vn;
  of->vn = NULL;
  if (vn==NULL) return -1;

  vfs_close(vn);	
  return 0;
}

#endif

/*
 * simple file system calls for write/read
 */
int
sys_write(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd!=STDOUT_FILENO && fd!=STDERR_FILENO) {
#if OPT_FILE
    return file_write(fd, buf_ptr, size);
#else
    kprintf("sys_write supported only to stdout\n");
    return -1;
#endif
  }

  if(curproc->fileTable[fd]==NULL){
    for (i=0; i<(int)size; i++) {
      putch(p[i]);
    }
  }
  else
    return file_write(fd, buf_ptr, size);
  return (int)size;
}

int
sys_read(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd != STDIN_FILENO) {
#if OPT_FILE
    return file_read(fd, buf_ptr, size);
#else
    kprintf("sys_read supported only to stdin\n");
    return -1;
#endif
  }

  if(curproc->fileTable[fd] == NULL){
    for (i=0; i<(int)size; i++) {
      p[i] = getch();
      if (p[i] < 0) 
        return i;
    }
  }
  else{
    return file_read(fd, buf_ptr, size);
  }

  return (int)size;
}

int sys_fstat (int fd, struct stat *buf){
  (void)fd;
  (void)buf;
  return 0;
}

int sys_dup2(int oldfd,int newfd, int *retval){

    struct openfile * old_open;
    struct openfile * new_open;
    //struct lock *dup2_lock;
    struct proc *p = curproc;
    (void)p;

    if(oldfd<0)
        return EBADF;
    if(oldfd>=OPEN_MAX)
        return EBADF;
    if(newfd<0)
        return EBADF;
    if(newfd >= OPEN_MAX)
        return EBADF;
      

    //dup2_lock = lock_create("dup2_lock");
    //lock_acquire(dup2_lock);

    old_open=curproc->fileTable[oldfd];

    if(old_open==NULL)
        return EBADF;

    if(curproc->fileTable[newfd]!=NULL){
    //chiudo il newfd
    curproc->fileTable[newfd]->countRef--;
    VOP_DECREF(curproc->fileTable[newfd]->vn);

    //controllo che sia l'ultimo ???

    }

    new_open=old_open;

    curproc->fileTable[newfd]=new_open;
    curproc->fileTable[newfd]->countRef++;
    VOP_INCREF(old_open->vn);


    *retval=newfd;

    //lock_release(dup2_lock);

    return 0;

}

/*
 * find and return the file associated with the filedescriptor
 * inside the process. it will be returned locked.
*/

int file_get(struct proc *p, int fd, struct openfile **f ) {
    if( fd >= OPEN_MAX || fd < 0 )
        return EBADF;

    //lock(p->lock_fd)
    if( p->fileTable[fd] != NULL ) { 
        *f = p->fileTable[fd];
        //lock(f->lock) (?)
        //unlock(p->lock_fd)
        return 0;
    }

    //unlock(p->lock_fd)
    return EBADF;
}

int sys_lseek( int fd, off_t offset, int whence, int64_t *retval ) {
  struct proc        *p = NULL;
  struct openfile        *f = NULL;
  int            err;
  struct stat        st;
  off_t            new_offset;

  KASSERT( curthread != NULL );
  KASSERT( curthread->t_proc != NULL );

  p = curthread->t_proc;

  //try to open the file
  err = file_get( p, fd, &f );
  if( err )
    return err;

  //depending on whence, seek to appropriate location
  switch( whence ) {
    case SEEK_SET:
      new_offset = offset;
      break;

    case SEEK_CUR:
      new_offset = f->offset + offset;
      break;

    case SEEK_END:
      //if it is SEEK_END, we use VOP_STAT to figure out
      //the size of the file, and set the offset to be that size.
      err = VOP_STAT( f->vn, &st );
      if( err ) {
        //F_UNLOCK( f );
        return err;
      }

      //set the offet to the filesize.
      new_offset = st.st_size + offset;
      break;
    default:
      //F_UNLOCK( f );
      return EINVAL;
  }

  //use VOP_TRYSEEK to verify whether the desired
  //seeking location is proper.
  //da gestire meglio
  if(VOP_ISSEEKABLE( f->vn))
  {

    //adjust the seek.
    f->offset = new_offset;
    *retval = new_offset;
    //F_UNLOCK( f );
    return 0;
  }
  return ESPIPE;
}