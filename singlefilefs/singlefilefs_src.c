#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/types.h>
#include <linux/version.h>

#include "../utils_header.h"

static struct super_operations singlefilefs_super_ops = {
};

static struct dentry_operations singlefilefs_dentry_ops = {
};

struct filesystem_info fs_info = {0};   // extern in utils_header.h
struct super_block *global_sb;          // extern in utils_header.h

int singlefilefs_fill_super(struct super_block *sb, void *data, int silent) {

    struct inode *root_inode;
    struct buffer_head *bh;
    struct onefilefs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;
    
    // Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    // inizializzazione variabile superblocco globale
    global_sb = sb;

    // lettura del superblocco del file system
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
    
    long unsigned int mounted_curr;

    printk("%s: current usages: %d\n", MODNAME, fs_info.usage);

    if (atomic_read(&(fs_info.usage)) != 0) {
        printk("%s: smontaggio del file system impossibile, qualche thread sta eseguendo qualche operazione\n", MODNAME);
        return;
    }

    mounted_curr = __sync_val_compare_and_swap(&(fs_info.mounted), 1, 0);
    if (mounted_curr != 1) {
        printk("%s: il file system è già stato smontato\n", MODNAME);
        return;
    }

    cleanup_srcu_struct(&(fs_info.srcu)); // reset srcu_struct

    kill_block_super(s);
    printk("%s: singlefilefs smontato con successo\n", MODNAME);

    return;
}

struct dentry *singlefilefs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    
    struct dentry *d_ret;
    unsigned int mounted;
    int ret;

    mutex_init(&(fs_info.write_lock));

    ret = init_srcu_struct(&(fs_info.srcu));
    if (ret != 0) {
        printk("%s: errore durante il montaggio del filesystem", MODNAME);
        return ERR_PTR(-ENOMEM);
    }

    // controllo se il filesystem è già montato (utilizzo del compare and swap per far fronte a scenari concorrenti)
    mounted = __sync_val_compare_and_swap(&(fs_info.mounted), 0, 1);
    if (mounted != 0) {
        printk("%s: il device driver supporta un solo montaggio alla volta (mounted=%d)\n", MODNAME, fs_info.mounted);
        return ERR_PTR(-EBUSY);
    }

    d_ret = mount_bdev(fs_type, flags, dev_name, data, singlefilefs_fill_super);
    if (unlikely(IS_ERR(d_ret))) 
        printk("%s: errore durante il montaggio del filesystem", MODNAME);
    else 
        printk("%s: singlefilefs montato con successo dal dispositivo %s\n", MODNAME, dev_name);
    
    return d_ret;
}

// file system structure
static struct file_system_type onefilefs_type = {
    .owner = THIS_MODULE,
    .name = "singlefilefs",
    .mount = singlefilefs_mount,
    .kill_sb = singlefilefs_kill_superblock,
};
