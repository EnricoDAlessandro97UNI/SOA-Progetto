#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/apic.h>
#include <asm/io.h>
#include <linux/syscalls.h>
#include <linux/pid.h>
#include <linux/tty.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/delay.h>


// put_data syscall - insert size byte of the source in a free block
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char*, source, size_t, size) {
#else
asmlinkage int sys_put_data(char* source, size_t size) {
#endif

    int i, ret, len, block_to_write;

    char *klvl_buf;
    char end_str = '\0';

    unsigned int *sel_blk;
    unsigned int curr_blk, old_blk;

    struct buffer_head *bh = NULL;
    struct bdev_layout *bdev_blk;
    struct block_device *bdev_temp;

    /*
    int index;
    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;
    */

    printk("%s: put_data invocata\n", MODNAME);

    // sanity check
    if (source == NULL) return -EINVAL;
    len = strlen(source);
    if (len == 0) return -EINVAL;
    if (size >= DATA_SIZE) return -EINVAL;
    
    // allocazione di memoria dinamica per contenere il messaggio utente
    klvl_buf = kmalloc(size+1, GFP_KERNEL);
    if (!klvl_buf) {
        printk("%s: errore kmalloc, impossibilità di allocare memoria per la ricezione del buffer utente\n", MODNAME);
        return -ENOMEM;
    }

    // copia size bytes dal buffer utente al buffer kernel
    ret = copy_from_user(klvl_buf, source, size);
    len = strlen(klvl_buf);
    if (len < size) size = len;
    klvl_buf[size] = end_str;
    AUDIT printk(KERN_INFO "%s: il messaggio da inserire è %s (len=%lu)\n", MODNAME, klvl_buf, size+1); 

    // segnala la presenza del reader sulla variabile bdev (for the unmount check)
    __sync_fetch_and_add(&(bdev_md.usage),1);
    bdev_temp = bdev_md.bdev;
    if (bdev_temp == NULL) {
        printk("%s: nessun device montato", MODNAME);
        ret = -ENODEV;
        goto put_exit;
    }  

    // cerca un blocco valido da sovrascrivere
    for (i = 0; i < NBLOCKS-2; i++) {
        curr_blk = kblock_md[i];
        if (curr_blk == set_invalid(0)) {
            sel_blk = &kblock_md[i];
            break;
        }
    }
    
    // se il ciclo for è stato completato significa che non ci sono blocchi liberi
    if (i >= NBLOCKS-2) {
        printk("%s: nessun blocco disponibile per inserire il messaggio\n", MODNAME);
        ret = -ENOMEM;
        goto put_exit;
    }
    AUDIT printk(KERN_INFO "%s: il blocco invalido è il numero %d\n", MODNAME, i);

    // Validazione del blocco - un altro writer potrebbe aver selezionato lo stesso blocco
    // con cas (compare and swap) rilevo questo caso annullando l'inserimento
    old_blk = __sync_val_compare_and_swap(sel_blk, curr_blk, set_valid(0));
    if (old_blk == set_valid(0)) {
        printk("%s: cas put_data fallita\n", MODNAME);
        ret = -EAGAIN;
        goto put_exit;
    }
    AUDIT printk(KERN_INFO "%s: cas put_data ok, validità=%d\n", MODNAME, get_validity(kblock_md[i]));

    // popola la struct block_device_layout da scrivere poi in buffer_head
    bdev_blk = kmalloc(sizeof(struct bdev_layout), GFP_KERNEL);
    if (bdev_blk == NULL) {
        printk("%s: errore kmalloc, impossibilità di allocare memoria per bdev_blk\n", MODNAME);
        wake_up_interruptible(&unmount_wq);
        kfree(klvl_buf);
        return -ENOMEM;
    }
    bdev_blk->next_block = set_invalid(0);
    memcpy(bdev_blk->data, klvl_buf, size+1); // +1 per il terminatore di stringa

    // prendi il lock in scrittura per la concorrenza con invalidate_data
    mutex_lock(&(rcu.write_lock));
    
    // dati in cache
    block_to_write = blk_offset(i); // +2 per evitare il superblock e l'inode
    bh = (struct buffer_head *) sb_bread(bdev_temp->bd_super, block_to_write);
    if (!bh) {
        sel_blk = set_invalid(0);
        ret = -EIO;
        mutex_unlock(&(rcu.write_lock));
        goto put_exit;
    }

    // aggiunta concorrente sul blocco selezionato evitata grazie alla cas precedente
    if (bh->b_data != NULL) {
        memcpy(bh->b_data, (char *) bdev_blk, sizeof(struct bdev_layout));
        mark_buffer_dirty(bh);
    }

    // forza la scrittura in modo sincrono sul device
#ifdef SYNC_WRITE_BACK 
    if(sync_dirty_buffer(bh) == 0) {
        AUDIT printk(KERN_INFO "%s: scrittura sincrona avvenuta con successo", MODNAME);
    } else
        printk("%s: scrittura sincrona fallita", MODNAME);
#endif

    brelse(bh);
    ret = i;

    // aggiunta del blocco appena scritto alla rcu_list per renderlo visibile
    list_insert(&head, i);
    AUDIT printk(KERN_INFO "%s: blocco %d aggiunto alla lista dei messaggi validi\n", MODNAME, i);

    // Move to a new epoch
    /*
    updated_epoch = (rcu.next_epoch_index) ? MASK : 0;
    rcu.next_epoch_index += 1;
    rcu.next_epoch_index %= 2;

    last_epoch = __atomic_exchange_n(&(rcu.epoch), updated_epoch, __ATOMIC_SEQ_CST);
    index = (last_epoch & MASK) ? 1 : 0; 
	grace_period_threads = last_epoch & (~MASK); 

	AUDIT printk(KERN_INFO "%s: put_data (waiting %lu readers on index = %d)\n", MODNAME, grace_period_threads, index);

    wait_event_interruptible(readers_wq, rcu.standing[index] >= grace_period_threads);
    rcu.standing[index] = 0;
    */

    mutex_unlock(&(rcu.write_lock));

put_exit:
    __sync_fetch_and_sub(&(bdev_md.usage),1);
    wake_up_interruptible(&unmount_wq);
    kfree(klvl_buf);
    kfree(bdev_blk);
    return ret;
} 


// get_data syscall - get size bytes from the block at the specified offset
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char*, destination, size_t, size) {
#else
asmlinkage int sys_get_data(int offset, char* destination, size_t size) {
#endif

    int ret, index, len, return_val, block_to_read;
    char end_str = '\0';
    unsigned long my_epoch;
    struct buffer_head *bh = NULL;
    struct block_device *bdev_temp;
    struct bdev_layout *bdev_blk;

    printk("%s: get_data invocata", MODNAME);

    // sanity check
    if (destination == NULL) return -EINVAL;
    // if (size >= DATA_SIZE) size = DATA_SIZE; // se richiesta una dimensione superiore alla massima ritorna tutto il contenuto di default
    if (size < 0 || offset < 0 || offset >= NBLOCKS-2) return -EINVAL;
    
    // segnala la presenza del reader sulla variabile bdev
    __sync_fetch_and_add(&(bdev_md.usage),1);
    bdev_temp = bdev_md.bdev;
    if (bdev_temp == NULL) {
        printk("%s: nessun device montato", MODNAME);
        __sync_fetch_and_sub(&(bdev_md.usage),1);
        wake_up_interruptible(&unmount_wq);
        return -ENODEV;
    }

    // segnala la presenza del reader per evitare che uno scrittore riutilizzi lo stesso blocco mentre lo si sta leggendo
    my_epoch = __sync_fetch_and_add(&(rcu.epoch),1);

    // controlla se il blocco richiesto è valido
    if (list_search(head, offset) == 0) {
        printk("%s: il blocco %d richiesto non è valido\n", MODNAME, offset);
        return_val = -ENODATA;
        goto get_exit;
    }

    // dati in cache
    block_to_read = blk_offset(offset);
    bh = (struct buffer_head *) sb_bread(bdev_temp->bd_super, block_to_read);
    if (!bh) {
        return_val = -EIO;
        goto get_exit;
    }

    if (bh->b_data != NULL) {
        AUDIT printk(KERN_INFO "%s: [blocco %d]\n", MODNAME, block_to_read);
        bdev_blk = (struct bdev_layout *) bh->b_data;
        len = strlen(bdev_blk->data);
        if (size > len) { // richiesta una size maggiore del contenuto effettivo del blocco dati
            size = len; 
            ret = copy_to_user(destination, bdev_blk->data, size);
            return_val = size - ret;
            ret = copy_to_user(destination+return_val, &end_str, 1);
        } 
        else { // richiesta una size minore o uguale del contenuto effettivo del blocco dati
            ret = copy_to_user(destination, bdev_blk->data, size);
            return_val = size - ret;
            ret = copy_to_user(destination+return_val, &end_str, 1);
        }
        if (strlen(bdev_blk->data) < size) return_val = strlen(bdev_blk->data);
        AUDIT printk(KERN_INFO "%s: bytes caricati nell'area di destinazione %d\n", MODNAME, return_val);
    }
    else return_val = 0;

    brelse(bh);

get_exit:
    // the first bit in my_epoch is the index where we must release the counter
    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(rcu.standing[index]),1);
    __sync_fetch_and_sub(&(bdev_md.usage),1);
    wake_up_interruptible(&readers_wq);
    wake_up_interruptible(&unmount_wq);

    return return_val; // the amount of bytes actually loaded into the destination area
}


// invalidate_data syscall - invalidate the block at specified offset
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset) {
#else
asmlinkage int sys_invalidate_data(int offset) {
#endif

    int ret = 0;
    int index;
    
    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;
    struct block_device *bdev_temp;
    struct Node *removed;

    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;

    printk("%s: invalidate_data invocata\n", MODNAME);

    if (offset < 0 || offset >= NBLOCKS-2) return -EINVAL;
    
    AUDIT printk(KERN_INFO "%s: il blocco da invalidare è %d\n", MODNAME, offset);

    // segnala la presenza del reader sulla variabile bdev
    __sync_fetch_and_add(&(bdev_md.usage),1);
    bdev_temp = bdev_md.bdev;
    if (bdev_temp == NULL) {
        printk("%s: nessun device montato", MODNAME);
        __sync_fetch_and_sub(&(bdev_md.usage),1);
        wake_up_interruptible(&unmount_wq);
        return -ENODEV;
    }

    // prendi il lock in scrittura per la concorrenza con put_data ed altre invalidate_data
    mutex_lock(&(rcu.write_lock));

    // controlla se il blocco è già stato invalidato
    if (list_search(head, offset) == 0) {
        printk("%s: il blocco %d è già invalidato, nulla da fare\n", MODNAME, offset);
        ret = -ENODATA;
        goto inv_exit;
    }

    // delete the element from valid list
    removed = list_remove(&head, offset);
    if(removed == NULL) {
        printk("%s: qualcosa è andato storto durante la list_remove (blocco=%d)", MODNAME, offset);
        ret = -ENODATA;
        goto inv_exit;
    } 
    kfree(removed);
    // a questo punto le nuove operazioni di read non troveranno il blocco invalidato nella lista    

    // move to a new epoch
    updated_epoch = (rcu.next_epoch_index) ? MASK : 0;
    rcu.next_epoch_index += 1;
	rcu.next_epoch_index %= 2;	

	last_epoch = __atomic_exchange_n (&(rcu.epoch), updated_epoch, __ATOMIC_SEQ_CST);
	index = (last_epoch & MASK) ? 1 : 0; 
	grace_period_threads = last_epoch & (~MASK); 

	AUDIT printk(KERN_INFO "%s: invalidate_data (waiting %lu readers on index = %d)\n", MODNAME, grace_period_threads, index);
	
    wait_event_interruptible(readers_wq, rcu.standing[index] >= grace_period_threads);
    rcu.standing[index] = 0;

    // aggiorna kblock_md - no concorrenza con put_data in quanto ho preso il lock
    kblock_md[offset] = set_invalid(0);

    // aggiorna i metadati del blocco
    bh = (struct buffer_head *) sb_bread(bdev_temp->bd_super, blk_offset(offset));
    if (!bh) {
        ret = -EIO;
        goto inv_exit;
    }

    if (bh->b_data != NULL) {
        bdev_blk = (struct bdev_layout *) bh->b_data;
        bdev_blk->next_block = set_invalid(bdev_blk->next_block);
        mark_buffer_dirty(bh);
    }

    // forza la scrittura in modo sincrono sul device
#ifdef SYNC_WRITE_BACK 
    if(sync_dirty_buffer(bh) == 0) {
        AUDIT printk(KERN_INFO "%s: scrittura sincrona avvenuta con successo", MODNAME);
    } else
        printk("%s: scrittura sincrona fallita", MODNAME);
#endif
    brelse(bh);

inv_exit:
    mutex_unlock(&(rcu.write_lock));
    __sync_fetch_and_sub(&(bdev_md.usage),1);
    wake_up_interruptible(&unmount_wq);
    return ret;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)       
unsigned long sys_put_data = (unsigned long) __x64_sys_put_data;
unsigned long sys_get_data = (unsigned long) __x64_sys_get_data;
unsigned long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#else
#endif
