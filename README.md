# SOA-Progetto: Block-level data management service

  

  

## Autore

  

  

* 👨‍💻: Enrico D'Alessandro (matricola: 0306424)

  

  

  

## Indice

  

  

1. [Introduzione](#introduzione)

  

  

2. [Strutture dati utilizzate](#strutture-dati-utilizzate)

  

  

3. [System call](#system-call)

  

  

4. [File operation](#file-operation)

  

  

5. [Concorrenza](#concorrenza)

  

  

6. [Software utente](#software-utente)

  

  

7. [Utilizzo](#utilizzo)

  

  

  

## Introduzione

  

  

Il progetto prevede la realizzazione di un modulo kernel che implementi un device driver per la gestione di molteplici blocchi di memoria, dove ciascun blocco di memoria può ospitare un messaggio utente. Ogni blocco ha una dimensione pari a 4KB, dove:

  

  

* X byte mantengono dati utente;

  

  

* 4KB-X byte mantengono dei metadati per la gestione del dispositivo.

  

  

  

Il device driver è in grado di supportare sia alcune system call sia alcune file operation. Il software di inizializzazione del modulo del kernel si occupa di registrare le system call, il file system e il device driver con le file operation implementate.

  

  

  

Le system call implementate sono:

  

  

1.  ```int put_data(char *source, size_t size)``` inserisce in un blocco inizialmente non valido, ovvero libero, fino a *size* byte del contenuto del buffer *source*. Restituisce l'indice del blocco che è stato sovrascritto in caso di successo, mentre restituisce l'errore ENOMEM nel caso in cui non ci siano blocchi liberi.

  

  

2.  ```int get_data(int offset, char *destination, size_t size)``` legge fino a *size* byte del blocco di indice *offset* e riporta i dati letti nel buffer *destination* da consegnare all'utente. Restituisce il numero di byte copiati nel buffer *destination* in caso di successo, mentre restituisce l'errore ENODATA nel caso in cui il blocco specificato non sia valido.

  

  

3.  ```int invalidate_data(int offset)``` invalida il blocco di indice *offset* (elimina logicamente un messaggio rendendo il blocco nuovamente disponibile per sovrascritture). Restituisce l'errore ENODATA nel caso in cui non ci siano dati validi associati al blocco specificato dal parametro offset.

  

  

  

Le file operation sono invece:

  

  

1.  ```int onefilefs_open(struct inode *inode, struct file *file)``` apre il dispositivo come stream di byte.

  

  

2.  ```int onefilefs_release(struct inode *inode, struct file *file)``` chiude il file associato al dispositivo.

  

  

3.  ```ssize_t onefilefs_read(struct file *filp, char *buf, size_t len, loff_t *off)``` legge solo i blocchi correntemente validi, e li legge esattamente nell'ordine con cui i rispettivi dati sono stati scritti con la system call put_data() (per cui gli indici dei blocchi non sono rilevanti).

  

  

  

Da specifiche del progetto il dispositivo deve poter essere montato su qualunque directory del file system del sistema e, per semplicità, il device driver può supportare un solo montaggio per volta. Quando il dispositivo non è montato, qualunque system call o file operation deve fallire restituendo l'errore ENODEV.

  

  

  

## Strutture dati utilizzate

  
  

### Superblocco del dispositivo

Il superblocco del dispositivo è composto dai seguenti campi:

*  ```uint64_t version``` indica la versione del file system.

*  ```uint64_t magic``` indica il magic number associato al file system.

*  ```uint64_t block_size``` indica la dimensione di ciascun blocco di memoria che compone il dispositivo.

*  ```unsigned int first_valid``` indica il numero del primo blocco valido.

*  ```unsigned int last_valid``` indica il numero dell'ultimo blocco valido.

  

I due campi ```first_valid``` e ```last_valid``` permettono di mantenere l'ordine in cui le scritture sono state eseguite, in particolare definiscono la testa e la coda di una lista collegata. Tuttavia, la lista collegata non è mappata su una struttura dati diversa dal dispositivo a blocchi, ma sono i metadati stessi dei blocchi a puntare al blocco successivo.

  

### Layout del blocco

  

  

Il blocco sul dispositivo ha una dimensione di 4KB: 4 byte sono riservati per i metadati mentre i restanti sono a disposizione per i messaggi utente. I 4 byte dei metadati sono così organizzati:

  

  

* 1 bit di validità: indica se il blocco è valido, ovvero se contiene correntemente dati utente, oppure se invalido, ovvero se eliminato logicamente e disponibile per sovrascritture;

  

  

* 31 bit indicanti il numero del prossimo blocco valido (informazione utile per mantenere un ordinamento temporale dei messaggi che vengono inseriti).

  

  

  

### Struttura dati mantenuta in RAM

  

  

A supporto delle operazioni del modulo viene utilizzata anche una struttura dati mantenuta in RAM:

  

```

filesystem_info {

  unsigned int mounted;

  atomic_t usage;

  struct mutex write_lock;

};

```

  

*  ```unsigned int mounted``` indica se il file system risulta montato all'interno del sistema o meno. Tale campo viene consultato all'inizio di ogni system call e file operation per stabilire se l'operazione può essere eseguita o meno.

  

*  ```atomic_t usage``` indica il numero di thread che stanno correntemente utilizzando il file system. Quando diverso da zero il file system non può essere smontato dal sistema.

  

*  ```struct mutex write_lock``` mutex utilizzato per coordinare tra loro le operazioni di scrittura sul dispositivo (put_data() e invalidate_data()).

  

  

  

## System call

  
  

### int put_data(char *source, size_t size)

  

  

1. Il contatore dei thread che stanno utilizzando il file system viene incrementato di 1 in modo atomico mediante una ```__sync_fetch_and_add()```.

  

  

2. Vengono effettuati dei sanity check sui valori dei parametri passati.

  

  

3. Se i controlli vanno a buon fine, il contenuto del buffer utente ```source``` viene riversato in un buffer di livello kernel mediante una ```copy_from_user()```.

  

  

4. Si cerca un blocco correntemente libero da poter sovrascrivere.

  
  

5. Il blocco selezionato viene aggiornato come valido e non più disponibile per sovrascritture. Inoltre, ne viene aggiornato anche il contenuto.

  

  

6. Viene sovrascritto il superblocco del dispositivo, in cui vengono aggiornati i valori dei campi ```first_valid``` e ```last_valid``` in maniera opportuna.

  

  

7. Si decrementa di 1 in modo atomico il contatore dei thread che stanno correntemente utilizzando il file system.

  

  

  

### int get_data(int offset, char *destination, size_t size)

  

  

1. Il contatore dei thread che stanno utilizzando il file system viene incrementato di 1 in modo atomico mediante una ```__sync_fetch_and_add()```.

  

  

2. Vengono effettuati dei sanity check sui valori dei parametri passati.

  

  

3. Si controlla se il blocco identificato dal parametro ```offset+2``` è correntemente valido. Se il blocco non è valido, la system call termina con l'errore ENODATA, mentre se il blocco è presente si prosegue con gli step successivi.

  

  

4. Si restituisce il contenuto nel buffer ```destination``` utente mediante una ```copy_to_user()```.

  

  

5. Si decrementa di 1 in modo atomico il contatore dei thread che stanno correntemente utilizzando il file system.

  

  

  

### int invalidate_data(int offset)

  

  

1. Il contatore dei thread che stanno utilizzando il file system viene incrementato di 1 in modo atomico mediante una ```__sync_fetch_and_add()```.

  

  

2. Vengono effettuati dei sanity check sui valori dei parametri passati.

  

  

3. Si controlla se il blocco ad indice ```offset+2``` è già stato invalidato e, in caso affermativo, la system call termina con l'errore ENODATA, altrimenti si prosegue con i passi successivi.

  

  

4. Si invalida il blocco target e si aggiornano i metadati del blocco precedente ed eventualmente anche i campi ```first_valid``` e ```last_valid``` del superblocco.

  

  

5. Si decrementa di 1 in modo atomico il contatore dei thread che stanno correntemente utilizzando il file system.

  

  

  

## File operation

  

  

### int onefilefs_open(struct inode *inode, struct file *file)

  

1. Viene controllato se il dispositivo è montato.

  

2. Viene controllato se il file è stato aperto in modalità read-only.

  

3. Il file viene effettivamente aperto.

  

  

### ssize_t onefilefs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)

  

  

1. Il contatore dei thread che stanno utilizzando il file system viene incrementato di 1 in modo atomico mediante una ```__sync_fetch_and_add()```.

  

  

2. Vengono effettuati dei sanity check sui valori dei parametri passati.

  

  

3. Si scorre la lista dei messaggi validi e, per ogni blocco valido, si concatena il suo contenuto al buffer utente con una ```copy_to_user()```.

  

  

4. Si decrementa di 1 in modo atomico il contatore dei thread che stanno correntemente utilizzando il file system.

  

  

### int onefilefs_release(struct inode *inode, struct file *file)

  

  

1. Viene controllato se il dispositivo è montato.

  

2. Il file viene effettivamente chiuso.

  

  

## Concorrenza

  

Per sincronizzare i thread che operano sulle strutture dati precedenti è stato seguito l'approccio RCU. E' opportuno analizzare i possibili scenari di concorrenza che si possono venire a presentare per evitare eventuali inconsistenze.

  

*  ```get_data()```: in questo caso si utilizza soltanto srcu_read_lock(), ovvero il contatore atomico utilizzato dai lettori per determinare il grace period. Concorrentemente a un lettore possono accedere al dispositivo sia altri lettori che gli scrittori.

  

*  ```put_data()```: in questo caso si utilizza il write_lock per coordinare gli scrittori tra loro, cosa che non viene direttamente garantita dalla sincronizzazione basata su RCU.

  

*  ```invalidate_data()```: anche in questo caso si utilizza il write_lock sfruttato anche dalla put_data().

  

  

  

## Software utente

  

  

Per testare i servizi offerti dal modulo del kernel implementato nel presente progetto sono stati sviluppati due programmi di livello user, presenti nella sotto-cartella ```user/```, di cui uno interattivo (```user.c```) e uno no (```test.c```). Il software utente interattivo presenta un menu di scelta grazie al quale è possibile:

  

  

* montare il file system

  

  

* smontare il file system

  

  

* invocare la system call ```put_data()```

  

  

* invocare la system call ```get_data()```

  

  

* invocare la system call ```invalidate_data()```

  

  

  

Il software non interattivo genera invece un insieme di thread che invocano concorrentemente le tre system call testando così i diversi possibili scenari. Il risultato del test può essere analizzato aprendo un terminale ed utilizzando il comando ```dmesg``` con i privilegi di root (oltre che dai messaggi di output prodotti dal test stesso).

  

  

  

## Utilizzo

  

  

Il modulo per il kernel Linux è stato sviluppato sulla versione 5.15.0-76-generic. Per inserire correttamente il modulo all'interno del kernel è prima necessario inserire il modulo ```the_usctm.ko``` (necessario per la scoperta dell'indirizzo corrente della system call table) seguendo le istruzioni nel make file presente all'interno della sotto-cartella ```syscall-table/```.

  

  

### Parametri configurabili prima della compilazione

  

Prima di effettuare la compilazione è possibile configurare i seguenti parametri:

  

1. Nel Makefile nella directory principale bisogna configurare ```NBLOCKS```, che rappresenta il numero di blocchi di dati da inserire nell'immagine. Attenzione: ```NBLOCKS``` include anche il superblocco e l'inode (ad esempio, ```NBLOCKS=6``` indica che si stanno inserendo nell'immagine 4 blocchi dati).

  

2. Nel file header ```common_header.h``` bisogna:

  

* In ```NBLOCKS``` inserire lo stesso valore del punto precedente;

  

* Cambiare ```IMAGE_PATH``` con il proprio percorso corretto del file immagine.

  

3. Commentare/decommentare la ```#define SYNC_WRITE_BACK``` nel file header ```utils_header.h``` se si vuole che la scrittura sul device avvenga in maniera asincrona/sincrona.

  

  

### Compilazione del modulo e montaggio del file system

  

Per compilare ed inserire correttamente il modulo è necessario eseguire i seguenti comandi (è richiesto essere utente root):

  

1.  ```make all``` per compilare tutti i file necessari;

  

2.  ```make insmod``` per inserire il modulo e registrare le nuove system call, il device driver e il file system;

  

2.  ```make create-fs``` per formattare il file immagine (block device logico);

  

3.  ```make mount-fs``` per montare il file system.

  

  

### Clean up

  

Per smontare il file system e rimuovere il modulo è necessario eseguire i seguenti comandi (è richiesto essere utente root):

  

1.  ```make umount-fs``` per smontare il file system;

  

2.  ```make rmmod``` per rimuovere il modulo;

  

3.  ```make clean``` per rimuovere tutti i file generati dal comando ```make all```.

  

4. Spostarsi nella sotto-cartella ```syscall-table/``` ed eseguire il comando ```make rmmod``` per rimuovere il modulo per la scoperta dell'indirizzo corrente della system call table.
