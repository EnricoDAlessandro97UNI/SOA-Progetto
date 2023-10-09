#ifndef _UTILS_H
#define _UTILS_H

#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/srcu.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0)
#include <linux/atomic.h>
#else
#include <linux/atomic_32.h>
#endif

#include "singlefilefs/singlefilefs.h"
#include "common_header.h"

#define SYNC_WRITE_BACK     // comment this line to disable synchronous writing

// BLOCK LEVEL DATA MANAGEMENT SERVICE STUFF
#define MODNAME "BLOCK-LEVEL-SERVICE"
#define AUDIT if(1)
#define DEVICE_NAME "blockleveldev"
#define DEV_NAME "./mount/the-file"
#define DEFAULT_BLOCK_SIZE 4096
#define METADATA_SIZE 4
#define DATA_SIZE (DEFAULT_BLOCK_SIZE - METADATA_SIZE)

// KERNEL METADATA TO MANAGE MESSAGES
// Device's block layout
struct bdev_layout {
    unsigned int next_block; // 1 bit di validità + 31 bit per l'indice del blocco successivo
    char data[DATA_SIZE];
};

// File system info
struct filesystem_info {
    unsigned int mounted;       // indica se il file system è montato o meno
    atomic_t usage;             // tiene traccia del numero di thread che stanno correntemente utilizzando il file system
    struct mutex write_lock;    // utilizzato per sincronizzare gli scrittori tra loro
    struct srcu_struct srcu;    // struttura dati a supporto delle sleepable RCU 
};

// Shared variables
extern struct super_block *global_sb;       // super block variable accessible by syscalls
extern struct filesystem_info fs_info;

// Prototypes
struct onefilefs_sb_info* get_sb_info(struct super_block *);
struct bdev_layout* get_block(struct super_block *, unsigned int);
int set_sb_info(struct super_block *, unsigned int, unsigned int);
int set_block_metadata_valid(struct super_block *, unsigned int, unsigned int);
int update_block_metadata(struct super_block *, unsigned int, unsigned int);
int set_block_data(struct super_block *, unsigned int, char *, size_t);
int invalidate_block(struct super_block *, unsigned int);
unsigned int get_previous_last_valid(struct super_block *, unsigned int, unsigned int);
int invalidate_one(struct super_block *, unsigned int, unsigned int, unsigned int);
int invalidate_first(struct super_block *, unsigned int, unsigned int, unsigned int);
int invalidate_middle(struct super_block *, unsigned int, unsigned int);
int invalidate_last(struct super_block *, unsigned int, unsigned int, unsigned int, unsigned int);
// for testing
void print_block_status(struct super_block *);

#endif