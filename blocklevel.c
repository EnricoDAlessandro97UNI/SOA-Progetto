#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/kernel.h>
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
#include <linux/delay.h>
#include <linux/pid.h>
#include <linux/tty.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>

#include "lib/include/scth.h"
#include "common_header.h"
#include "utils_header.h"
#include "blocklevelsyscall.c"
#include "singlefilefs/singlefilefs_src.c"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Enrico D'Alessandro <enrico.dalessandro@alumni.uniroma2.eu>");
MODULE_DESCRIPTION("BLOCK-LEVEL DATA MANAGAMENT SERVICE");

unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);
unsigned long the_ni_syscall;
unsigned long new_sys_call_array[] = {0x0,0x0,0x0};
#define HACKED_ENTRIES (int)(sizeof(new_sys_call_array)/sizeof(unsigned long))
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};

// Blocks metadata
unsigned int kblock_md[NBLOCKS-2];
struct Node *head;
struct bdev_metadata bdev_md;
struct rcu_counter rcu;

int major;

DECLARE_WAIT_QUEUE_HEAD(readers_wq);    // queue used to wait readers (RCU)

int house_keeper(void *data);   // Kernel thread prototype

// Startup function
int init_module(void) {

    int i, ret;
    struct task_struct *the_rcu_kernel_daemon;
    char name[128] = "the_rcu_kernel_daemon";

    printk("%s: startup module", MODNAME);
    printk("%s: indirizzo sys_call_table ricevuto %p\n", MODNAME, (void *)the_syscall_table);
    printk("%s: initializing - hacked entries %d\n", MODNAME, HACKED_ENTRIES);

    // system call initialization
    new_sys_call_array[0] = (unsigned long) sys_put_data;
    new_sys_call_array[1] = (unsigned long) sys_get_data;
    new_sys_call_array[2] = (unsigned long) sys_invalidate_data;

    ret = get_entries(restore, HACKED_ENTRIES, (unsigned long *) the_syscall_table, &the_ni_syscall);
    if (ret != HACKED_ENTRIES) {
        printk("%s: could not hack %d entries (just %d)\n", MODNAME, HACKED_ENTRIES, ret);
        return -1;
    }

    unprotect_memory();
    for (i = 0; i < HACKED_ENTRIES; i++) {
        ((unsigned long *) the_syscall_table)[restore[i]] = (unsigned long) new_sys_call_array[i];
    }
    protect_memory();

    printk("%s: tutte le nuove system call sono state correttamente installate nella system call table\n", MODNAME);

    // device driver registration
    major = __register_chrdev(0, 0, 256, DEVICE_NAME, &onefilefs_file_operations);
    if (major < 0) {
        printk("%s: registrazione device driver fallita\n", MODNAME);
        return major;
    }
    printk("%s: device driver registrato, major number assegnato %d\n", MODNAME, major);

    // file system registration
    ret = register_filesystem(&onefilefs_type);
    if (likely(ret == 0))
        printk("%s: singlefilefs registrato correttamente\n", MODNAME);
    else
        printk("%s: registrazione singlefilefs fallito\n", MODNAME);

    // inizializzazione delle strutture livello kernel
    for (i = 0; i < NBLOCKS-2; i++) {
        kblock_md[i] = set_invalid(0); // inzializzazione del vettore di validità dei blocchi a invalidi
    } 

    head = NULL; // puntatore alla testa della lista dei blocchi attualmente validi

    // rcu counter init
    rcu.epoch = 0x0;
    rcu.standing[0] = 0x0;
    rcu.standing[1] = 0x0;
    rcu.next_epoch_index = 0x1;
    mutex_init(&(rcu.write_lock));

    printk("%s: metadati livello kernel correttamente inizializzati\n", MODNAME);

    the_rcu_kernel_daemon = kthread_create(house_keeper, NULL, name);
    if (the_rcu_kernel_daemon == NULL) {
        printk("%s: kernel daemon rcu errore inizializzazione\n", MODNAME);
        return -1;
    }
    else {
        wake_up_process(the_rcu_kernel_daemon);
        printk("%s: kernel daemon rcu inizializzato\n", MODNAME);
    }

    
    return 0;
}

// Shutdown function
void cleanup_module(void) {

    int i, ret;

    printk("%s: cleanup module\n", MODNAME);

    // system call table restore
    unprotect_memory();
    for (i = 0; i < HACKED_ENTRIES; i++) {
        ((unsigned long *) the_syscall_table)[restore[i]] = the_ni_syscall;
    }
    protect_memory();

    printk("%s: system call table ripristinata al suo contenuto originale\n", MODNAME);

    // unregister device driver
    unregister_chrdev(major, DEVICE_NAME);

    printk("%s: device driver de-registrato, major number a cui era assegnato %d\n", MODNAME, major);

    // unregister filesystem
    ret = unregister_filesystem(&onefilefs_type);
    if (likely(ret == 0)) 
        printk("%s: singlefilefs de-registrato\n", MODNAME);
    else
        printk("%s: de-registrazione singlefilefs fallita\n", MODNAME);

    return;
}

// Kernel thread to reset the epoch counter
int house_keeper(void* data) {

	unsigned long last_epoch;
	unsigned long updated_epoch;
	unsigned long grace_period_threads;
	int index;

redo:
    // "period" seconds sleep
	msleep(PERIOD*1000); 

	mutex_lock(&(rcu.write_lock));

    updated_epoch = (rcu.next_epoch_index) ? MASK : 0;
    rcu.next_epoch_index += 1;
	rcu.next_epoch_index %= 2;	

	last_epoch = __atomic_exchange_n (&(rcu.epoch), updated_epoch, __ATOMIC_SEQ_CST);
	index = (last_epoch & MASK) ? 1 : 0; 
	grace_period_threads = last_epoch & (~MASK); 

	AUDIT printk(KERN_INFO "%s: HOUSE KEEPING (waiting %lu readers on index = %d)\n", MODNAME, grace_period_threads, index);
	
    wait_event_interruptible(readers_wq, rcu.standing[index] >= grace_period_threads);
    rcu.standing[index] = 0;
	mutex_unlock(&(rcu.write_lock));
    AUDIT printk(KERN_INFO "%s: HOUSE KEEPING (stop waiting %lu readers on index = %d)\n", MODNAME, grace_period_threads, index);
	
    goto redo;
	return 0;
}