# os161-c2
Progetto C2 per il corso "Programmazione di Sistema"
implementazione sys_execv


Assignments C2 
Antonio Cesaria  sxxxxxxx Lorenzo Cesetti s286896 Davide Casalegno s280103
##Shell
###Process Handling:
  
    .PROCESS STRUCTURE
Abbiamo aggiunto alla struct process i seguenti campi:

     struct openfile *fileTable[OPEN_MAX];
     pid_t p_ppid; 
     int p_status;   
     pid_t p_pid;  
Ci servono per una corretta gestione dei file aperti di un processo e per l'implementazione delle funzionalità della waitpid().


    .SYSCALLS()

-1 int sys_execv(char *progname,char ** args, int *err)

La syscall si basa su due altre funzioni di supporto: copyin_args, copyout_args. La prima permette di copiare gli argomenti della sys_execv da spazio user a spazio kernel mentre la seconda si occupa di trasferire gli argomenti da spazio kernel al nuovo spazio di indirizzamento del processo. 
Copyout_args prevede inoltre il riempimento e l'allineamento dello stack con gli argomenti da passare a enter_new_process. 

-int sys__getcwd(userptr_t buf, size_t size,int*retval) & 
  int sys_chdir(userptr_t dir);

Funzioni che permettono la navigazione all'interno del FileSystem. Da specificare che necessitano di un FileSystem sottostante più elaborato di sfs per un effettivo funzionamento.

-int sys_fork(struct trapframe *ctf, pid_t *retval) :
 Permette la creazione di un processo figlio a partire da una copia identica dell'address space del processo padre. Abbiamo gestito questa copia identica attraverso funzioni  di copia dell'address space e della file table.
Aggiungendo il campo pid_t p_ppid alla struct proc si mantiene il collegamento tra un processo e l'eventuale processo padre.

-int sys_waitpid(pid_t pid, userptr_t statusp, int options, int valid_anyway, int *err) & void sys_exit(int status);

Risulta essere di fatto un wrapper della funzione di più basso livello "proc_wait()"  e si occupa dell'attesa della terminazione di  un processo figlio. Abbiamo inoltre inserito una serie di controlli dedicati alla gestione degli errori. Permette inoltre di  recuperare lo stato di uscita del figlio atteso. Tale stato di uscita viene specificato dal figlio grazie alla sys_exit(). E' stato necessario inserire _MKWAIT_EXIT(return_status); in proc_wait() per gestire lo stato di ritorno del figlio (i 30 bit alti ----)



.FILE HANDLING

- lseek(), 
-int sys_lseek(int fd, off_t pos, int whence, off_t *new_pos);

La syscall permette di implementare kernel-level la funzione lseek(). Se il riposizionamento va a buon fine, viene ritornato il valore 0 e la nuova posizione nel file è indicata in new_pos. Altrimenti viene ritornato un codice di errore e new_pos rimane invariato.



-  int sys_dup2(int oldfd, int newfd);

La syscall permette l'implementazione lato kernel della funzione dup2. Se il cambiamento è corretto  viene ritornato 0, altrimenti viene ritornato un codice di errore. L'implementazione di tale sys_call() permette alla shell di sfruttare i ridirezionamenti qualora questi siano possibili.


- int sys_close(int fd) & int sys_open(userptr_t path, int openflags, mode_t mode, int *errp)
 
 Syscalls che permettono l'apertura e la chiusura di un file da parte di un processo. I file aperti di un processo 
 sono memorizzati nella relativa file table (singola per ogni processo). La open effettua una scansione lineare per associare un file descriptor libero (indice nella file table)  ad un file appena aperto. La close effettua le operazioni duali cancellando le relative entry nella file table dei file chiusi. 

- int sys_read(int fd, userptr_t buf_ptr, size_t size, int *err) & int sys_write(int fd, userptr_t buf_ptr, size_t size, int *err);
Syscalls che permettono di leggere e scrivere sia da/su console che da/su file. Quando i file descriptor passati sono quelli relativi a operazioni su console, vengono invocate le funzioni standard per scrittura/lettura su/da console.
