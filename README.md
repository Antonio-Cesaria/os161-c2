
# Shell Assignment C2 
Antonio Cesaria s290261 Lorenzo Cesetti s286896 Davide Casalegno s280103

## PROCESS HANDLING:
#### Process Structure

Abbiamo aggiunto alla struct process i seguenti campi:

     struct openfile *fileTable[OPEN_MAX];
     pid_t p_ppid; 
     int p_status;   
     pid_t p_pid;  
Ci servono per una corretta gestione dei file aperti di un processo e per l'implementazione delle funzionalità della waitpid().


#### Syscalls()

- ```int sys_fork(struct trapframe *ctf, pid_t *retval)```
 
Permette la creazione di un processo figlio a partire da una copia identica dell'address space del processo padre. Abbiamo gestito questa copia identica attraverso funzioni  di copia dell'address space e della file table.
Aggiungendo il campo pid_t p_ppid alla struct proc si mantiene il collegamento tra un processo e l'eventuale processo padre.


- ```int sys_waitpid(pid_t pid, userptr_t statusp, int options, int valid_anyway, int *err)```
- ```void sys_exit(int status)```

Risulta essere di fatto un wrapper della funzione di più basso livello "proc_wait()"  e si occupa dell'attesa della terminazione di  un processo figlio. Abbiamo inoltre inserito una serie di controlli dedicati alla gestione degli errori. Permette inoltre di  recuperare lo stato di uscita del figlio atteso. Tale stato di uscita viene specificato dal figlio grazie alla sys_exit(). E' stato necessario inserire _MKWAIT_EXIT(return_status); in proc_wait() per gestire lo stato di ritorno del figlio (i 30 bit alti del valore di ritorno)

- ```int sys__getcwd(userptr_t buf, size_t size,int*retval)```
- ```int sys_chdir(userptr_t dir)```

Funzioni che permettono la navigazione all'interno del FileSystem. Al loro interno vengono fatti opportuni controlli di errore prima di chiamare le rispettive operazioni di vfs (vfs_getcwd, vfs_setcurdir). 
Per poter utilizzare in maniera concreta queste syscall è necessaria la presenza di un FileSystem sottostante più elaborato (non parte di questo assignment)di emu-sfs (che non permette la creazione e la gestione di sottocartelle). Di conseguenza nel nostro progetto l'effetto che si avrà dall'esecuzione di queste syscall sarà un'esecuzione senza errori ma senza effetti tangibili (chdir termina correttamente ma senza effetto, get_cwd restituisce sempre la radice del filesystem).

### FILE HANDLING
 
- ```int sys_lseek( int fd, off_t offset, int whence, int64_t *retval )```

La syscall permette di implementare kernel-level la funzione lseek(). Se il riposizionamento va a buon fine, viene ritornato il valore 0 e la nuova posizione nel file è indicata in pos. Altrimenti viene ritornato un codice di errore e new_pos rimane invariato. La particolarità di questa syscall è che deve essere in grado di saper gestire correttamente valori a 64-bit in quanto sia offset che retval sono interi su 64-bit. Per gestire ciò bisogna tenere ben presente in quali registri è presente la variabile offset ($a2:$a3) e dove scrivere il valore di ritorno ($v0:$v1). Per facilitare queste operazioni sono state utilizzate delle semplici macro (MAKE_64BIT, GET_LO,GET_HI)


- ```int sys_dup2(int oldfd, int newfd)```

La syscall permette l'implementazione lato kernel della funzione dup2. Se il cambiamento è corretto  viene ritornato 0, altrimenti viene ritornato un codice di errore. L'implementazione di tale sys_call() permette alla shell di sfruttare i ridirezionamenti qualora questi siano possibili.

- ```int sys_open(userptr_t path, int openflags, mode_t mode, int *errp)```
- ```int sys_close(int fd)```
 
 Syscalls che permettono l'apertura e la chiusura di un file da parte di un processo. I file aperti di un processo 
 sono memorizzati nella relativa file table (singola per ogni processo). La open effettua una scansione lineare per associare un file descriptor libero (indice nella file table)  ad un file appena aperto. La close effettua le operazioni duali cancellando le relative entry nella file table dei file chiusi. 

- ```int sys_read(int fd, userptr_t buf_ptr, size_t size, int *err)```
- ```int sys_write(int fd, userptr_t buf_ptr, size_t size, int *err)```

Syscalls che permettono di leggere e scrivere sia da/su console che da/su file. Quando i file descriptor passati sono quelli relativi a operazioni su console, vengono invocate le funzioni standard per scrittura/lettura su/da console.


### EXECV

Parte centrale dell'assignment. La syscall si occupa di implementare kernel-level la funzione execv.
La funzione inizialmente controlla che gli argomenti siano non nulli (progname e args). Successivamente grazie all'ausilio della funzione int copyin_args(...) vengono copiati gli argomenti da spazio di indirizzamento user a kernel.

- ``` int copyin_args(char ***args, char **uargs, int *argc) ```

Questa funzione di supporto utilizza internamente copyin(...) per controllare la validità degli indirizzi e per contare il numero degli argomenti (argc). copyinstr() viene invece utilizzata per copiare effettivamente gli argomenti in un vettore di stringhe (di lunghezza ARG_MAX) allocato dinamicamente di lunghezza argc.


Viene copiato il progrname nello spazio kernel (anche se già presente negli argomenti). Dopodichè viene salvato l'address space corrente e viene aperto l'eseguibile (se presente) del nuovo programma da eseguire. Viene creato un nuovo spazio di indirizzamento relativo al nuovo processo che viene settato come quello attuale per il processo corrente, dove viene caricato il file elf aperto precedentemente mendiante la load_elf. In seguito viene creato anche lo stack per questo nuovo as dove verranno caricati gli argomenti precedentemente salvati nello spazio kernel. Il caricamento di questi argomenti è effettuato tramite la funzione copyout_args().

- ``` int copyout_args(char** argv, vaddr_t *stackptr, int argc) ```

Questa funzione di supporto si occupa del "corretto" caricamento degli argomenti nello stack dell'as appena creato. Per corretto si intende il rispetto del giusto layout (ordinamento e allinamento) dello stack, come mostrato in figura.

![stackptr](/stackptr.png)

Per poter ottenere questo risultato, è stato prima calcolata la dimensione totale del vettore di stringhe da inserire nello stack considerando anche il padding delle stringhe per mantenere l'allineamento di multipli di 4. Dopodichè viene costruito un vettore contenente i padding di tutte le stringhe. Successivamente lo stackptr viene decrementato e vengono in primo luogo copiati i puntatori (precalcolati) alle varie stringhe. Infine vengono "pushate" le stringhe nel corretto ordine e lo stackptr viene decrementato in modo da puntare alla prima posizione libera dello stack.


Dopo aver copiato gli argomenti nello stack del nuovo as, viene distrutto il vacchio as(as_old), viene invocata la funzione enter_new_process() che si occuperà di far ripartire l'esecuzione del processo con il nuovo address space. Il precedente address space viene distrutto solo alla fine in quanto in presenza di errori in ognuna delle operazioni intermedie, questo verrà ripristinato. La corretta implementazione può essere testata facilmente invocando la shell e chiamando un'eseguibile (per esempio palin).
Il corretto passaggio degli argomenti può essere facilmente testato con il programma testbin/argtest oltre che con l'esecuzione di un'eseguibile che richiede argomenti (Es. cat sys161.conf). 

### Suddivisione carico di lavoro
Le syscall fork, execv, waitipid, exit, open, close, read e write sono state realizzate in collaborazione in quanto parte di esse erano già state realizzate durante il corso ed altre come la execv richiedevano (dal nostro punto di vista) maggiore attenzione e collaborazione in quanto centrali per lo sviluppo del progetto.

In una fase finale c'è stata una chiara suddivisione del lavoro come segue:

Antonio Cesaria: sys_lseek(...)

Davide Casalegno: sys_dup2(...)

Lorenzo Cesetti: sys___getcwd(...) e sys_chdir(...)

Infine, l'ultima fase del lavoro ha riguardato la corretta integrazione dei singoli lavori individuali (grazie al supporto di un repo GitHub creato nella fase iniziale del progetto), la gestione dei codici di errore (principalmente utilizzando l'eseguibile testbin/badcall), testing generico e pulizia del codice.
