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

1.  ```int dev_open(struct inode *inode, struct file *file)``` apre il dispositivo come stream di byte.

2.  ```int dev_release(struct inode *inode, struct file *file)``` chiude il file associato al dispositivo.

3.  ```ssize_t dev_read(struct file *filp, char *buf, size_t len, loff_t *off)``` legge solo i blocchi correntemente validi, e li legge esattamente nell'ordine con cui i rispettivi dati sono stati scritti con la system call put_data() (per cui gli indici dei blocchi non sono rilevanti).

  

Da specifiche del progetto il dispositivo deve poter essere montato su qualunque directory del file system del sistema e, per semplicità, il device driver può supportare un solo montaggio per volta. Quando il dispositivo non è montato, qualunque system call o file operation deve fallire restituendo l'errore ENODEV.

  

## Strutture dati utilizzate

### Layout del blocco

Il blocco sul dispositivo ha una dimensione di 4KB: 4 byte sono riservati per i metadati mentre i restanti sono a disposizione per i messaggi utente. I 4 byte dei metadati sono così organizzati:

* 1 bit di validità: indica se il blocco è valido, ovvero se contiene correntemente dati utente, oppure se invalido, ovvero se eliminato logicamente e disponibile per sovrascritture;

* 31 bit indicanti il numero del prossimo blocco valido (informazione utile per mantenere un ordinamento temporale dei messaggi che vengono inseriti).

  

### Strutture dati ausiliari

Quando il file system viene montato, nel kernel vengono inizializzate delle strutture dati per la gestione dei messaggi utente. In particolare, nell'operazione di montaggio si individua il primo blocco valido per ricostruire in memoria RAM quella che è la lista dei soli blocchi validi. A partire dal blocco valido è possibile identificare tutti i successivi blocchi validi in maniera iterativa mediante i 31 bit nei metadati rappresentanti il numero del prossimo blocco valido. Strutture dati:

*  ```struct Node``` - la struttura è costituita da un campo puntatore al blocco ```struct Node``` successivo (in modo tale da creare una lista collegata) e da un campo ```unsigned int``` a 32 bit (1 bit di validità + 31 rappresentanti il numero di blocco in questione). In particolare, è stata implementata una lista collegata per tener conto ad ogni istante temporale dei soli blocchi validi, ovvero contenenti messaggi utente, ordinati secondo l'ordine temporale di scrittura.

*  ```struct bdev_metadata``` - questa struttura comprende due campi: un intero senza segno indicante quanti thread stanno correntemente utilizzando il file system e un campo ```struct block_device``` per recuperare le informazioni sul dispositivo e controllarne il montaggio. Se uno o più thread stanno utilizzando il file system (>0), allora questo può essere smontato dal sistema soltanto al termine delle operazioni di tutti i thread.

*  ```struct fs_metadata``` - questa struttura comprende due campi: un intero senza segno indicante se il file system è montato (1) o meno (0) e un vettore di caratteri indentificante il nome del dispositivo.

*  ```unsigned int kblock_md[NBLOCKS-2]``` - questo vettore ausiliario mantiene aggiornate in RAM ad ogni istante di tempo le informazioni sulla validità dei vari blocchi (esclusi il superblocco e l'inode).

  

## System call

Per sincronizzare i thread che operano sulle strutture dati precedenti è stato seguito l'approccio RCU. Secondo questo approccio di sincronizzazione i lettori non attendono le scritture, mentre le invalidazioni vengono sequenzializzate mediante un lock in scrittura. Per quanto riguarda gli inserimenti questi non fanno uso di lock e seguono invece un approccio all-or-nothing. Sempre in riferimento agli inserimenti, in caso di problemi di incosistenza dovuti alla concorrenza, viene ritornato all'utente l'errore -EAGAIN per comunicare al codice user che l'operazione non è andata a buon fine, ma che è possibile riprovare. Infine, le invalidazioni devono impedire il riutilizzo di blocchi invalidati fintantoché esistono lettori nel grace period. Per implementare il meccanismo RCU è stata utilizzata la seguente struttura:

*  ```struct counter```, contenente i campi: ```unsigned long standing[2]```, ```unsigned long epoch```, ```int next_epoch_index```, ```struct mutex write_lock```.

  

Per evitare attese attive a livello kernel da parte del thread che deve attendere il completamento delle letture da parte dei lettori presenti nel grace period, è stata utilizzata una wait queue: ```wait_queue_head_t readers_wq```. In questo modo si migliora l'efficienza e la gestione delle risorse del sistema.

  

### int put_data(char *source, size_t size)

1. Il contatore dei thread che stanno utilizzando il file system viene incrementato di 1 in modo atomico mediante una ```__sync_fetch_and_add()```.

2. Vengono effettuati dei sanity check sui valori dei parametri passati.

3. Se i controlli vanno a buon fine, il contenuto del buffer utente ```source``` viene riversato in un buffer di livello kernel mediante una ```copy_from_user()```.

4. Si cerca un blocco correntemente libero da poter sovrascrivere. Tale operazione si affida all'utilizzo di una ```__sync_val_compare_and_swap``` così da evitare scenari concorrenti nel caso in cui un altro writer selezioni lo stesso blocco libero. In particolare, se un altro writer ha selezionato lo stesso blocco libero, allora si annulla l'inserimento.

5. Il blocco selezionato viene aggiornato come valido e non più disponibile per sovrascritture.

6. Si prende il lock in scrittura per aggiornare il contenuto del blocco in cache e per aggiungere in coda alla lista dei messaggi validi il nuovo messaggio inserito dall'utente.

7. Infine, si rilascia il lock e si decrementa di 1 in modo atomico il contatore dei thread che stanno correntemente utilizzando il file system.

  

### int get_data(int offset, char *destination, size_t size)

1. Il contatore dei thread che stanno utilizzando il file system viene incrementato di 1 in modo atomico mediante una ```__sync_fetch_and_add()```.

2. Vengono effettuati dei sanity check sui valori dei parametri passati.

3. Si segnala la presenza del reader per evitare che uno scrittore riutilizzi lo stesso blocco mentre lo si sta leggendo.

4. Si controlla se il blocco identificato dal parametro ```offset``` è correntemente presente nella lista dei messaggi validi. Se il blocco non è presenta nella lista, la system call termina con l'errore ENODATA, mentre se il blocco è presente si prosegue con gli step successivi.

5. Si legge il blocco ad ```offset+2``` (per saltare il superblocco e l'inode del file) dalla cache e si restituisce il contenuto nel buffer ```destination``` utente mediante una ```copy_to_user()```.

6. Infine, si segnala che il reader non sta più leggendo e si decrementa di 1 in modo atomico il contatore dei thread che stanno correntemente utilizzando il file system.

  

### int invalidate_data(int offset)

1. Il contatore dei thread che stanno utilizzando il file system viene incrementato di 1 in modo atomico mediante una ```__sync_fetch_and_add()```.

2. Vengono effettuati dei sanity check sui valori dei parametri passati.

3. Si prende il lock in scrittura per evitare scenari concorrenti con altre ```put_data()``` o ```invalidate_data()```.

4. Si controlla se il blocco è già stato invalidato e, in caso affermativo, la system call termina con l'errore ENODATA, altrimenti si prosegue con i passi successivi.

5. Si rimuove il blocco identificato dal parametro ```offset``` dalla lista dei messaggi validi attendendo i lettori nel grace period.

6. Si aggiornano i metadati del blocco in cache.

7. Infine, si rilascia il lock in scrittura e si decrementa di 1 in modo atomico il contatore dei thread che stanno correntemente utilizzando il file system.

  

## File operation

### int onefilefs_open(struct inode *inode, struct file *file)
1. Viene controllato se il dispositivo è montato.
2. Viene controllato se il file è stato aperto in modalità read-only.
3. Il file viene effettivamente aperto.

### ssize_t onefilefs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)

1. Il contatore dei thread che stanno utilizzando il file system viene incrementato di 1 in modo atomico mediante una ```__sync_fetch_and_add()```.

2. Vengono effettuati dei sanity check sui valori dei parametri passati.

3. Si segnala la presenza del reader per evitare che uno scrittore riutilizzi lo stesso blocco mentre lo si sta leggendo.

4. Si scorre la lista dei messaggi validi e, per ogni blocco valido, si recupera il suo contenuto dalla cache e lo si concatena al buffer utente con una ```copy_to_user()```.

5. Infine, si segnala che il reader non sta più leggendo e si decrementa di 1 in modo atomico il contatore dei thread che stanno correntemente utilizzando il file system.

### int onefilefs_release(struct inode *inode, struct file *file)

1. Viene controllato se il dispositivo è montato.
2. Il file viene effettivamente chiuso.



## Concorrenza

E' opportuno analizzare i possibili scenari di concorrenza che si possono venire a presentare per evitare eventuali inconsistenze.

*  ```put_data()```: la concorrenza tra due o più ```put_data()``` viene gestita mediante una ```__sync_val_compare_and_swap()```. In particolare, soltanto un thread riuscirà a prendere un determinato blocco libero per l'inserimento del messaggio utente. La concorrenza con ```invalidate_data()``` viene invece gestita mediante il lock in scrittura per andare ad operare sulla lista dei soli blocchi validi.

*  ```get_data()```: in questo caso si utilizza soltanto il meccanismo RCU per segnalare la presenza del reader. Concorrentemente a un lettore possono accedere al dispositivo sia altri lettori che gli scrittori.

*  ```invalidate_data()```: la concorrenza con altre ```invalidate_data()``` o con la ```put_data()``` viene gestita mediante il lock in scrittura.

  

## Software utente

Per testare i servizi offerti dal modulo del kernel implementato nel presente progetto sono stati sviluppati due programmi di livello user, presenti nella sotto-cartella ```user/```, di cui uno interattivo (```user.c```) e uno no (```test.c```). Il software utente interattivo presenta un menu di scelta grazie al quale è possibile:

* montare il file system

* smontare il file system

* invocare la system call ```put_data()```

* invocare la system call ```get_data()```

* invocare la system call ```invalidate_data()```

  

Il software non interattivo genera invece un insieme di thread che invocano concorrentemente le tre system call testando così i diversi possibili scenari. Il risultato del test può essere analizzato aprendo un terminale ed utilizzando il comando ```dmesg``` con i privilegi di root (oltre che dai messaggi di output prodotti dal test stesso).

  

## Utilizzo

Il modulo per il kernel Linux è stato sviluppato sulla versione 6.2.0-26-generic. Per inserire correttamente il modulo all'interno del kernel è prima necessario inserire il modulo ```the_usctm.ko``` (necessario per la scoperta dell'indirizzo corrente della system call table) seguendo le istruzioni nel make file presente all'interno della sotto-cartella ```syscall-table/```. 

### Parametri configurabili prima della compilazione
Prima di effettuare la compilazione è possibile configurare i seguenti parametri:
1.	Nel Makefile nella directory principale bisogna configurare ```NBLOCKS```, che rappresenta il numero di blocchi di dati da inserire nell'immagine. Attenzione: ```NBLOCKS``` include anche il superblocco e l'inode (ad esempio, ```NBLOCKS=6``` indica che si stanno inserendo nell'immagine 4 blocchi dati).
2.	Nel file header ```common_header.h``` bisogna:
	* In ```NBLOCKS``` inserire lo stesso valore del punto precedente;
	* Cambiare ```IMAGE_PATH``` con il proprio percorso corretto del file immagine.
3. Commentare/decommentare la ```#define SYNC_WRITE_BACK``` nel file header ```utils_header.h``` se si vuole che la scrittura sul device avvenga in maniera asincrona/sincrona.

### Compilazione del modulo e montaggio del file system
Per compilare ed inserire correttamente il modulo è necessario eseguire i seguenti comandi (è richiesto essere utente root):
1. ```make all``` per compilare tutti i file necessari;
2. ```make create-fs``` per formattare il file immagine (block device logico);
3. ```make insmod``` per inserire il modulo e registrare le nuove system call, il device driver e il file system;
4. ```make mount-fs``` per montare il file system.

### Clean up
Per smontare il file system e rimuovere il modulo è necessario eseguire i seguenti comandi (è richiesto essere utente root):
1. ```make umount-fs``` per smontare il file system;
2. ```make rmmod``` per rimuovere il modulo;
3. ```make clean``` per rimuovere tutti i file generati dal comando ```make all```.
4. Spostarsi nella sotto-cartella ```syscall-table/``` ed eseguire il comando ```make rmmod``` per rimuovere il modulo per la scoperta dell'indirizzo corrente della system call table.
