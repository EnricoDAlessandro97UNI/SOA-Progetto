#include <linux/buffer_head.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/version.h>

#include "lib/include/scth.h"
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

int major;

// Startup function
int init_module(void) {

    int i, ret;

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