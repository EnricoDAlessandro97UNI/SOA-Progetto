#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/srcu.h>
#include <linux/syscalls.h>
#include <linux/types.h>

#include "utils_header.h"

// put_data syscall - insert size byte of the source in a free block
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char*, source, size_t, size) {
#else
asmlinkage int sys_put_data(char* source, size_t size) {
#endif

    int i;
    int ret;
    int len;
    int new_first_valid;
    char *klvl_buf;
    struct bdev_layout *bdev_blk;
    struct onefilefs_sb_info *sb_disk;

    printk("%s: [put_data()] - invocata\n", MODNAME);

    // incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(fs_info.usage));

    // sanity checks
    if (!fs_info.mounted) { // controlla se il file system è montato
        printk(KERN_INFO "%s: [put_data()] - il file system non è montato\n", MODNAME);
        atomic_fetch_add(-1, &(fs_info.usage));
        return -ENODEV;
    }  
    if (source == NULL) {
        printk(KERN_INFO "%s: [put_data()] - source null\n", MODNAME);
        atomic_fetch_add(-1, &(fs_info.usage));
        return -EINVAL;
    }
    len = strlen(source);
    if (len == 0) {
        printk(KERN_INFO "%s: [put_data()] - non vi sono dati da scrivere\n", MODNAME);
        atomic_fetch_add(-1, &(fs_info.usage));
        return -EINVAL;
    }
    if (size >= DATA_SIZE) {
        printk(KERN_INFO "%s: [put_data()] - dimensione dei dati da scrivere maggiore del limite massimo memorizzabile in un blocco\n", MODNAME);
        atomic_fetch_add(-1, &(fs_info.usage));
        return -EINVAL;
    }
    
    // allocazione di memoria dinamica per contenere il messaggio utente
    klvl_buf = kmalloc(size+1, GFP_KERNEL);
    if (!klvl_buf) {
        printk(KERN_CRIT "%s: [put_data()] - impossibile allocare memoria per la ricezione del buffer utente\n", MODNAME);
        atomic_fetch_add(-1, &(fs_info.usage));
        return -ENOMEM;
    }

    // copia size bytes dal buffer utente al buffer kernel
    ret = copy_from_user(klvl_buf, source, size);
    len = strlen(klvl_buf);
    if (len < size) size = len;
    printk(KERN_INFO "%s: [put_data()] - messaggio da inserire: %s (len=%lu)\n", MODNAME, klvl_buf, size+1); 

    // prendo il lock per sincronizzare gli scrittori (no concorrenza su tutte le operazioni di scrittura fino al rilascio del lock)
    mutex_lock(&(fs_info.write_lock));

    // recupero del superblocco
    sb_disk = get_sb_info(global_sb);
    if (sb_disk == NULL) {
        printk(KERN_CRIT "%s: [put_data()] - errore durante il recupero del superblocco\n", MODNAME);
        ret = -EIO;
        goto put_exit;
    }
    
    // ricerca di un blocco libero
    for (i = 0; i < NBLOCKS-2; i++) {
        bdev_blk = get_block(global_sb, blk_offset(i));
        if (bdev_blk == NULL) {
            printk(KERN_CRIT "%s: [put_data()] - errore durante il recupero del blocco %d\n", MODNAME, i);
            ret = -EIO;
            goto put_exit;
        }

        if (!get_validity(bdev_blk->next_block)) // cerco un blocco libero (bit di validità = 0)
            break;
    }
    
    // se il ciclo for è stato completato significa che non ci sono blocchi liberi
    if (i >= NBLOCKS-2) {
        printk(KERN_INFO "%s: [put_data()] - nessun blocco disponibile per inserire il messaggio\n", MODNAME);
        ret = -ENOMEM;
        goto put_exit;
    }

    printk(KERN_INFO "%s: [put_data()] - blocco libero: %d\n", MODNAME, i);

    // attesa della fine del grace period
    synchronize_srcu(&(fs_info.srcu));

    // aggiorna il campo next_block del vecchio ultimo blocco valido (se presente)
    if (sb_disk->last_valid != -1) {
        // aggiorna il blocco successivo a cui punta il last_valid corrente
        ret = set_block_metadata_valid(global_sb, blk_offset(sb_disk->last_valid), i);
        if (ret < 0) {
            printk(KERN_CRIT "%s: [put_data()] - errore durante la scrittura dei metadati sul blocco %d\n", MODNAME, sb_disk->last_valid);
            ret = -EIO;
            goto put_exit;
        }
    }

    // scrivi i dati sul blocco specifico
    ret = set_block_data(global_sb, blk_offset(i), klvl_buf, size);
    if (ret < 0) {
        printk(KERN_CRIT "%s: [put_data()] - errore durante la scrittura dei dati sul blocco %d\n", MODNAME, i);
        ret = -EIO;
        goto put_exit;
    }

    // se necessario aggiorno il primo blocco valido
    if (sb_disk->first_valid == -1)
        new_first_valid = i;
    else
        new_first_valid = sb_disk->first_valid;

    ret = set_sb_info(global_sb, new_first_valid, i);
    if (ret < 0) {
        printk(KERN_CRIT "%s: [put_data()] - errore durante la scrittura dei dati sul superblocco\n", MODNAME);
        ret = -EIO;
        goto put_exit;
    }

    print_block_status(global_sb);
    ret = i;

put_exit:
    kfree(klvl_buf);
    mutex_unlock(&(fs_info.write_lock));
    atomic_fetch_add(-1, &(fs_info.usage));
    printk("%s: [put_data()] - scrittura sul blocco %d completata\n", MODNAME, i);
    return ret;
} 


// get_data syscall - get size bytes from the block at the specified offset
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char*, destination, size_t, size) {
#else
asmlinkage int sys_get_data(int offset, char* destination, size_t size) {
#endif

    int ret;
    int return_val;
    int len;
    int srcu_idx;
    char end_str = '\0';
    // struct onefilefs_sb_info *sb_disk;
    struct bdev_layout *bdev_blk;

    printk("%s: [get_data()] - invocata\n", MODNAME);

    // incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(fs_info.usage));

    // sanity checks
    if (!fs_info.mounted) { // controlla se il file system è montato
        printk(KERN_INFO "%s: [get_data()] - il file system non è montato\n", MODNAME);
        return_val = -ENODEV;
        goto get_exit;
    } 
    if (destination == NULL) {
        printk(KERN_INFO "%s: [get_data()] - destination null\n", MODNAME);
        return_val = -EINVAL;
        goto get_exit;
    } 
    // if (size >= DATA_SIZE) size = DATA_SIZE; // se richiesta una dimensione superiore alla massima ritorna tutto il contenuto di default
    if (size < 0 || offset < 0 || offset >= NBLOCKS-2) {
        printk(KERN_INFO "%s: [get_data()] - parametri non validi\n", MODNAME);
        return_val = -EINVAL;
        goto get_exit;
    }
    
    // acquisizione della sleepable RCU read lock
    srcu_idx = srcu_read_lock(&(fs_info.srcu));
    
    // recupero del blocco da leggere
    bdev_blk = get_block(global_sb, blk_offset(offset));
    if (bdev_blk == NULL) {
        printk(KERN_CRIT "%s: [get_data()] - errore durante il recupero del blocco %d\n", MODNAME, offset);
        srcu_read_unlock(&(fs_info.srcu), srcu_idx);
        return_val = -EIO;
        goto get_exit;
    }

    // rilascio della sleepable RCU read lock
    srcu_read_unlock(&(fs_info.srcu), srcu_idx);

    // controllo validità del blocco target
    if (!get_validity(bdev_blk->next_block)) {
        printk(KERN_INFO "%s: [get_data()] - il blocco %d non è valido\n", MODNAME, offset);
        return_val = -ENODATA;
        goto get_exit;
    }

    // consegna dei dati all'utente
    len = strlen(bdev_blk->data);
    if (size > len) // richiesta una size maggiore del contenuto effettivo del blocco dati
        size = len; 
    
    ret = copy_to_user(destination, bdev_blk->data, size);
    return_val = size - ret;
    ret = copy_to_user(destination+return_val, &end_str, 1);

get_exit:
    atomic_fetch_add(-1, &(fs_info.usage));
    printk("%s: [get_data()] - lettura del blocco %d completata\n", MODNAME, offset);
    return return_val; // the amount of bytes actually loaded into the destination area
}


// invalidate_data syscall - invalidate the block at specified offset
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset) {
#else
asmlinkage int sys_invalidate_data(int offset) {
#endif

    int ret;
    unsigned int new_first_valid;
    unsigned int new_last_valid;
    struct bdev_layout *bdev_blk;
    struct onefilefs_sb_info *sb_disk;

    new_first_valid = -1;
    new_last_valid = -1;

    printk("%s: [invalidate_data()] - invocata\n", MODNAME);

    // incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(fs_info.usage));

    // sanity checks
    if (!fs_info.mounted) { // controlla se il file system è montato
        printk(KERN_INFO "%s: [invalidate_data()] - il file system non è montato\n", MODNAME);
        atomic_fetch_add(-1, &(fs_info.usage));
        return -ENODEV;
    } 
    if (offset < 0 || offset >= NBLOCKS-2) {
        printk(KERN_INFO "%s: [invalidate_data()] - parametri non validi\n", MODNAME);
        atomic_fetch_add(-1, &(fs_info.usage));
        return -EINVAL;
    }

    // prendo il lock per sincronizzare gli scrittori (no concorrenza su tutte le operazioni di scrittura fino al rilascio del lock)
    mutex_lock(&(fs_info.write_lock));

    // recupero dei dati memorizzati nel superblocco
    sb_disk = get_sb_info(global_sb);
    if (sb_disk == NULL) {
        printk(KERN_CRIT "%s: [invalidate_data()] - errore durante il recupero del superblocco\n", MODNAME);
        ret = -EIO;
        goto inv_exit;
    }

    // recupero del blocco da invalidare
    bdev_blk = get_block(global_sb, blk_offset(offset));
    if (bdev_blk == NULL) {
        printk(KERN_CRIT "%s: [invalidate_data()] - errore durante il recupero del blocco %d\n", MODNAME, offset);
        ret = -EIO;
        goto inv_exit;
    }

    // controllo se il blocco è già stato invalidato
    if (!(get_validity(bdev_blk->next_block))) {
        printk(KERN_INFO "%s: [invalidate_data()] - il blocco %d è già stato invalidato\n", MODNAME, offset);
        ret = -ENODATA;
        goto inv_exit;
    }

    // attesa della fine del grace period
    synchronize_srcu(&(fs_info.srcu));

    // il blocco da invalidare è l'unico blocco valido
    if ((sb_disk->first_valid == offset) && (sb_disk->last_valid == offset)) {
        ret = invalidate_one(global_sb, offset, new_first_valid, new_last_valid);
        if (ret < 0) {
            printk(KERN_CRIT "%s: [invalidate_data()] - errore durante l'invalidazione dell'unico blocco valido %d\n", MODNAME, offset);
            ret = -EIO;
            goto inv_exit;
        }
    }
    // il blocco da invalidare è il primo blocco valido, ma non l'ultimo
    else if ((sb_disk->first_valid == offset) && (sb_disk->last_valid != offset)) {
        // aggiorno i dati che andranno nel superblocco
        new_first_valid = get_block_num(bdev_blk->next_block);
        new_last_valid = sb_disk->last_valid;

        ret = invalidate_first(global_sb, offset, new_first_valid, new_last_valid);
        if (ret < 0) {
            printk(KERN_CRIT "%s: [invalidate_data()] - errore durante l'invalidazione del blocco in testa %d\n", MODNAME, offset);
            ret = -EIO;
            goto inv_exit;
        }
    }
    // il blocco da invalidare è l'ultimo blocco valido, ma non il primo
    else if ((sb_disk->first_valid != offset) && (sb_disk->last_valid == offset)) {
        ret = invalidate_last(global_sb, offset, sb_disk->first_valid, sb_disk->last_valid, get_block_num(bdev_blk->next_block));
        if (ret < 0) {
            printk(KERN_CRIT "%s: [invalidate_data()] - errore durante l'invalidazione dell'ultimo blocco %d\n", MODNAME, offset);
            ret = -EIO;
            goto inv_exit;
        }
    }
    // il blocco da invalidare non è né il primo né l'ultimo
    else {
        ret = invalidate_middle(global_sb, sb_disk->first_valid, offset);
        if (ret < 0) {
            printk(KERN_CRIT "%s: [invalidate_data()] - errore durante l'invalidazione di un blocco nel mezzo\n", MODNAME);
            ret = -EIO;
            goto inv_exit;
        }
    }

    printk(KERN_INFO "%s: [invalidate_data()] - new_first_valid: %d | new_last_valid: %d\n", MODNAME, sb_disk->first_valid, sb_disk->last_valid);
    print_block_status(global_sb);
    ret = 0;

inv_exit:
    mutex_unlock(&(fs_info.write_lock));
    atomic_fetch_add(-1, &(fs_info.usage));
    printk("%s: [invalidate_data()] - invalidazione del blocco %d completata\n", MODNAME, offset);
    return ret;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)       
unsigned long sys_put_data = (unsigned long) __x64_sys_put_data;
unsigned long sys_get_data = (unsigned long) __x64_sys_get_data;
unsigned long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#else
#endif
