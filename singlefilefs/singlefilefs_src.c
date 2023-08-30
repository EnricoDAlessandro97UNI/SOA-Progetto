#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>

#include "singlefilefs.h"
#include "../common_header.h"
#include "../utils_header.h"

static struct super_operations singlefilefs_super_ops = {
};

static struct dentry_operations singlefilefs_dentry_ops = {
};

struct bdev_metadata bdev_md __attribute__((aligned(64))) = {0, NULL};  // extern in utils_header.h
struct fs_metadata fs_md __attribute__((aligned(64))) = {0, " "};       // extern in utils_header.h

DECLARE_WAIT_QUEUE_HEAD(unmount_wq); // utilizzato per attendere la terminazione delle altre operazioni prima dello smontaggio del filesystem

// prototypes
int restore_blocks(void);
int persistence(struct block_device *);

int singlefilefs_fill_super(struct super_block *sb, void *data, int silent) {

    struct inode *root_inode;
    struct buffer_head *bh;
    struct onefilefs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;
    
    // Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if (!sb) {
	    return -EIO;
    }
    sb_disk = (struct onefilefs_sb_info *)bh->b_data;
    magic = sb_disk->magic;
    brelse(bh);

    // check on the expected magic number
    if (magic != sb->s_magic) {
	    return -EBADF;
    }

    sb->s_fs_info = NULL;                               // FS specific data (the magic number) already reported into the generic superblock
    sb->s_op = &singlefilefs_super_ops;                 // set our own operations

    root_inode = iget_locked(sb, 0);                    // get a root inode indexed with 0 from cache
    if (!root_inode) {
        return -ENOMEM;
    }
    
    root_inode->i_ino = SINGLEFILEFS_ROOT_INODE_NUMBER; // this is actually 10
    inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR);// set the root user as owned of the FS root
    root_inode->i_sb = sb;
    root_inode->i_op = &onefilefs_inode_ops;            // set our inode operations
    root_inode->i_fop = &onefilefs_dir_operations;      // set our file operations
    
    // update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    // baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM;

    sb->s_root->d_op = &singlefilefs_dentry_ops;  // set our dentry operations

    // unlock the inode to make it usable
    unlock_new_inode(root_inode);

    return 0;
}

static void singlefilefs_kill_superblock(struct super_block *s) {
    
    struct block_device *bdev_curr;
    struct block_device *bdev_temp = bdev_md.bdev;

    strncpy(fs_md.block_device_name, " ", 1);
    fs_md.block_device_name[1] = '\0';

    bdev_curr = __sync_val_compare_and_swap(&(bdev_md.bdev), bdev_md.bdev, NULL);
    printk("%s: attesa dei thread pendenti (%d)...", MODNAME, bdev_md.usage);
    wait_event_interruptible(unmount_wq, bdev_md.usage == 0); 

    persistence(bdev_temp); // salvataggio dello stato dei blocchi (validi/invalidi)

    blkdev_put(bdev_temp, FMODE_READ);
    kill_block_super(s);
    fs_md.mounted = 0;

    printk("%s: singlefilefs smontato con successo\n", MODNAME);

    return;
}

struct dentry *singlefilefs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    
    struct dentry *ret;
    int len, mounted;

    // controllo se il filesystem è già montato (utilizzo del compare and swap per far fronte a scenari concorrenti)
    mounted = __sync_val_compare_and_swap(&(fs_md.mounted), 0, 1);
    if (mounted == 1) {
        printk("%s: il device driver supporta un solo montaggio alla volta (mounted=%d)\n", MODNAME, fs_md.mounted);
        return ERR_PTR(-EBUSY);
    }
    AUDIT printk("%s: mounted settato a %d\n", MODNAME, fs_md.mounted);

    ret = mount_bdev(fs_type, flags, dev_name, data, singlefilefs_fill_super);
    if (unlikely(IS_ERR(ret))) 
        printk("%s: errore durante il montaggio del filesystem", MODNAME);
    else {
        // ottieni il nome del loop device in modo tale da accedere i dati successivamente
        len = strlen(dev_name);
        strncpy(fs_md.block_device_name, dev_name, len);
        fs_md.block_device_name[len] = '\0';
        bdev_md.bdev = blkdev_get_by_path(fs_md.block_device_name, FMODE_READ|FMODE_WRITE, NULL);
        if (bdev_md.bdev == NULL) {
            printk("%s: impossibile recuperare la struct block_device associata a %s", MODNAME, fs_md.block_device_name);
            return ERR_PTR(-EINVAL);
        }

        restore_blocks(); // ripristino dello stato delle strutture del kernel dal dispositivo

        printk("%s: singlefilefs montato con successo dal dispositivo %s\n", MODNAME, dev_name);
    }

    return ret;
}

// Funzione per recuperare lo stato dei blocchi in seguito al montaggio del filesystem
int restore_blocks(void) {

    // inizializza le strutture del kernel con il contenuto del dispositivo
    int i, block_to_read;

    unsigned int first_blk = -1;
    unsigned int temp_blk;
    unsigned int successors[NBLOCKS-2] = {[0 ... NBLOCKS-3] = -2};
    unsigned int predecessors[NBLOCKS-2] = {[0 ... NBLOCKS-3] = -2};

    struct buffer_head *bh = NULL;
    struct bdev_layout *bdev_blk = NULL;

    for (i = 0; i < NBLOCKS-2; i++) {
        block_to_read = blk_offset(i); // +2 per saltare il superblocco e l'inode
        bh = (struct buffer_head *) sb_bread((bdev_md.bdev)->bd_super, block_to_read);
        if (!bh)
            return -EIO;
        if (bh->b_data != NULL) {
            AUDIT printk(KERN_INFO "%s: [blocco %d]\n", MODNAME, block_to_read);
            bdev_blk = (struct bdev_layout *) bh->b_data;
            if (get_validity(bdev_blk->next_block) == 1) { // blocco valido
                kblock_md[i] = set_valid(0);
                if (get_block_num(bdev_blk->next_block) != get_block_num((unsigned int) -1)) { // se il blocco ha un successore
                    successors[i] = get_block_num(bdev_blk->next_block); // aggiorna successore del blocco i
                    predecessors[get_block_num(bdev_blk->next_block)] = i; // aggiorna predecessore next_block
                }
            }
            else { // blocco invalido
                kblock_md[i] = set_invalid(0);
            }
            printk(KERN_INFO "%s: [blocco %d] validità: %d\n", MODNAME, i, get_validity(kblock_md[i]));
            brelse(bh);
        }
    }

    // cerca l'indice del primo blocco valido della lista
    for (i = 0; i < NBLOCKS-2; i++) {
        if ((get_validity(kblock_md[i]) == 1) && (predecessors[i] == -2)) { // blocco valido e senza predecessore
            first_blk = i;
            break;
        }   
    }

    if (first_blk != -1) { // primo blocco trovato
        // inizializzazione lista messaggi validi
        temp_blk = first_blk;
        printk(KERN_INFO "%s: il primo blocco è %d", MODNAME, first_blk);
        do {
            list_insert(&head, temp_blk);
            printk(KERN_INFO "%s: inserito nella lista il blocco %d\n", MODNAME, temp_blk);
            temp_blk = successors[temp_blk];
        } while(temp_blk != -2);
    }

    printk("%s: ripristino contenuto dispositivo avvenuto con successo\n", MODNAME);

    return 0;
}

// Funzione per trasferire lo stato corrente dei blocchi validi sul dispositivo prima dello smontaggio del filesystem
int persistence(struct block_device *bdev_temp) {

    // aggiornamento del contenuto del dispositivo con le strutture dati del kernel
    // no concorrenza in quanto il thread di smontaggio attende il termine delle altre operazioni già avviate 
    int i, block_to_write;
    unsigned int next_block_num;
    struct buffer_head *bh = NULL;

    if (head != NULL) {
        // aggiornamento dei blocchi del dispositivo con l'ultimo stato della lista dei messaggi validi
        for (i = 0; i < NBLOCKS-2; i++) {
            block_to_write = blk_offset(i); // +2 per saltare il superblocco e l'inode
            bh = (struct buffer_head *) sb_bread(bdev_temp->bd_super, block_to_write);
            if (!bh) 
                return -EIO;
            if (bh->b_data != NULL) {
                AUDIT printk(KERN_INFO "%s: [blocco %d]\n", MODNAME, block_to_write);
                if (list_search(head, i)) { // blocco presente aggiorna metadati (validità + blocco successivo)
                    next_block_num = list_get_next(head, i);
                    if (next_block_num == -1) // il nodo non ha un successore
                        next_block_num = (unsigned int) -1;
                    else // il nodo possiede un successore
                        next_block_num = set_valid(next_block_num);
                }
                else { // blocco non presente aggiorna solo validità (ad invalido)
                    next_block_num = set_invalid(0);
                }

                // scrittura in cache e flush verso il dispositivo
                memcpy(&(((struct bdev_layout *) bh->b_data)->next_block), &next_block_num, sizeof(unsigned int));               
                mark_buffer_dirty(bh);
                if (sync_dirty_buffer(bh) == 0) {
                    AUDIT printk(KERN_INFO "%s: scrittura sincrona avvenuta con successo\n", MODNAME);
                } 
                else printk(KERN_INFO "%s: scrittura sincrona fallita\n", MODNAME);

                brelse(bh);

                kblock_md[i] = set_invalid(0); // reset della struttura dei blocchi validi/invalidi
            }
        }

        list_clear(&head); // reset della lista dei messaggi validi
    }

    printk("%s: lista dei messaggi validi svuotata con successo\n", MODNAME);

    return 0;
}

// file system structure
static struct file_system_type onefilefs_type = {
    .owner = THIS_MODULE,
    .name = "singlefilefs",
    .mount = singlefilefs_mount,
    .kill_sb = singlefilefs_kill_superblock,
};
