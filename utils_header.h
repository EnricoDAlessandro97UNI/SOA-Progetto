#ifndef _UTILS_H
#define _UTILS_H

#include <linux/version.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>

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

// RCU STUFF
#define PERIOD 30
#define EPOCHS 2
#define MASK 0x8000000000000000

// MAJOR & MINOR UTILS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session) MAJOR(session->f_inode->i_rdev)
#define get_minor(session) MINOR(session->f_inode->i_rdev)
#else
#define get_major(session) MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session) MINOR(session->f_dentry->d_inode->i_rdev)
#endif

// KERNEL METADATA TO MANAGE MESSAGES
// Device's block layout
struct bdev_layout {
    unsigned int next_block; // 1 bit di validità + 31 bit per l'indice del blocco successivo
    char data[DATA_SIZE];
};

// Variable accessed by the mount thread, readers and writers
struct bdev_metadata {
    unsigned int usage;
    struct block_device *bdev;
};

// Variable accessed only by the mount thread
struct fs_metadata {
    unsigned int mounted;
    char block_device_name[20];
};

// RCU counter
struct rcu_counter {
    unsigned long standing[EPOCHS];	
    unsigned long epoch; 
    int next_epoch_index;
    struct mutex write_lock;
} __attribute__((packed));

// LIST STUFF
// Definition of the linked list node structure
struct Node {
    unsigned int block_num;
    struct Node *next;
};

int list_search(struct Node *head, unsigned int block_num);
struct Node *list_insert(struct Node **head, unsigned int block_num);
struct Node *list_remove(struct Node **head, unsigned int block_num);
unsigned int list_get_prev(struct Node *head, unsigned int block_num);
unsigned int list_get_next(struct Node *head, unsigned int block_num);
struct Node *list_get_node(struct Node *head, unsigned int block_num);
void list_clear(struct Node **head);

// Shared variables
extern unsigned int kblock_md[NBLOCKS-2];   // rappresentazione a livello kernel della validità dei blocchi
extern struct Node *head;                   // puntatore alla testa della lista dei blocchi attualmente validi 
extern struct bdev_metadata bdev_md;        // block device metadata
extern struct rcu_counter rcu;              // rcu struct  
extern wait_queue_head_t unmount_wq;        // wait queue per lo smontaggio del filesystem   
extern wait_queue_head_t readers_wq;        // wait queue per l'attesa dei reader (RCU)
extern char file_body[];

#endif