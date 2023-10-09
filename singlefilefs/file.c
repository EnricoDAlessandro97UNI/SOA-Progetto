#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include "../utils_header.h"

int onefilefs_open(struct inode *, struct file *);
int onefilefs_release(struct inode *, struct file *);
ssize_t onefilefs_read(struct file *, char *, size_t, loff_t *);

struct dentry *onefilefs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {

    struct onefilefs_inode *FS_specific_inode;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh = NULL;
    struct inode *the_inode = NULL;

    //printk("%s: running the lookup inode-function for name %s",MODNAME,child_dentry->d_name.name);

    if (!strcmp(child_dentry->d_name.name, UNIQUE_FILE_NAME)) {
	
		// get a locked inode from the cache 
        the_inode = iget_locked(sb, 1);
        if (!the_inode)
       		 return ERR_PTR(-ENOMEM);

		// already cached inode - simply return successfully
		if (!(the_inode->i_state & I_NEW)) {
			return child_dentry;
		}

		// this work is done if the inode was not already cached
		inode_init_owner(&init_user_ns, the_inode, NULL, S_IFREG );
		the_inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

		the_inode->i_fop = &onefilefs_file_operations;
		the_inode->i_op = &onefilefs_inode_ops;

		// just one link for this file
		set_nlink(the_inode,1);

		// now we retrieve the file size via the FS specific inode, putting it into the generic inode
		bh = (struct buffer_head *)sb_bread(sb, SINGLEFILEFS_INODES_BLOCK_NUMBER );
		if (!bh) {
			iput(the_inode);
			return ERR_PTR(-EIO);
		}

		FS_specific_inode = (struct onefilefs_inode*)bh->b_data;
		the_inode->i_size = FS_specific_inode->file_size;
		brelse(bh);

		d_add(child_dentry, the_inode);
		dget(child_dentry);

		// unlock the inode to make it usable 
		unlock_new_inode(the_inode);

		return child_dentry;
    }

    return NULL;
}

// Open operation
int onefilefs_open(struct inode *inode, struct file *file) {
	
	// controlla se il filesystem è montato
	if(!fs_info.mounted){
		printk("%s: [onefilefs_open()] - il file system non è montato\n", MODNAME);
        return -ENODEV;
	}

	// nega aperture del file in scrittura (filesystem in sola lettura)
	if (file->f_mode & FMODE_WRITE) {
		printk("%s: [onefilefs_open()] - apertura in modalità scrittura non consentita\n", MODNAME);
		return -EROFS;
	}

	printk("%s: [onefilefs_open()] - device correttamente aperto\n", MODNAME);

	return 0;
}

// Read operation
ssize_t onefilefs_read(struct file *file, char __user *buf, size_t count, loff_t *pos) {

	int ret;
	int length;
	int copied;
	int srcu_idx;

	char *klvl_buf;
	char newline_str = '\n';
	char end_str = '\0';

	unsigned int curr_block_num;

	struct onefilefs_sb_info *sb_disk;
	struct bdev_layout *bdev_blk;
	
	if (*pos != 0) return 0;
	copied = 0;

	printk("%s: [onefilefs_read()] - operazione read invocata\n", MODNAME);

	// incremento del contatore atomico degli utilizzi del file system
    atomic_fetch_add(1, &(fs_info.usage));

	// sanity checks
    if (!fs_info.mounted) { // controlla se il file system è montato
        printk(KERN_INFO "%s: [onefilefs_read()] - il file system non è montato\n", MODNAME);
        atomic_fetch_add(-1, &(fs_info.usage));
        return -ENODEV;
    }  

	// acquisizione della sleepable RCU read lock
    srcu_idx = srcu_read_lock(&(fs_info.srcu));

	// recupero dei dati memorizzati nel superblocco
    sb_disk = get_sb_info(global_sb);
    if (sb_disk == NULL) {
        printk(KERN_CRIT "%s: [onefilefs_read()] - errore durante il recupero del superblocco\n", MODNAME);
        srcu_read_unlock(&(fs_info.srcu), srcu_idx);
        ret = -EIO;
        goto read_exit;
    }
	
	// controllo se attualmente ci sono blocchi validi
	if (sb_disk->first_valid == -1) {
		printk(KERN_INFO "%s: [onefilefs_read()] - nessun blocco valido\n", MODNAME);
        srcu_read_unlock(&(fs_info.srcu), srcu_idx);
		ret = 0;
		goto read_exit;
	}
	curr_block_num = sb_disk->first_valid;

	// leggo in ordine tutti i blocchi validi e restituisco il contenuto nel buffer utente
	while (curr_block_num != get_block_num(set_valid(-1))) {

		// recupero del blocco da leggere
		bdev_blk = get_block(global_sb, blk_offset(curr_block_num));
		if (bdev_blk == NULL) {
			printk(KERN_CRIT "%s: [onefilefs_read()] - errore durante il recupero del blocco %d\n", MODNAME, curr_block_num);
			ret = -EIO;
			goto read_exit;
		}

		length = strlen(bdev_blk->data);
		printk(KERN_INFO "%s: [onefilefs_read()] - blocco %u, %s, len=%d\n", MODNAME, curr_block_num, bdev_blk->data, length);

		klvl_buf = kmalloc(length + 1, GFP_KERNEL);
		if (!klvl_buf) {
			printk(KERN_CRIT "%s: [onefilefs_read()] - errore kmalloc, impossibile allocare memoria\n", MODNAME);
			ret = -1;
			goto read_exit;
		}

		memcpy(klvl_buf, bdev_blk->data, length);
		klvl_buf[length] = newline_str;
		ret = copy_to_user(buf + copied, klvl_buf, length + 1);
		copied = copied + length + 1 - ret; 
		printk(KERN_INFO "%s: [onefilefs_read()] - copiati %d bytes\n", MODNAME, copied);
		kfree(klvl_buf);
		curr_block_num = get_block_num(bdev_blk->next_block);
	}
	
	// rilascio della sleepable RCU read lock
    srcu_read_unlock(&(fs_info.srcu), srcu_idx);

	ret = copy_to_user(buf + copied, &end_str, 1);
	copied = copied + 1 - ret;
	ret = count;

read_exit:
	atomic_fetch_add(-1, &(fs_info.usage));

	*pos = *pos + copied;

	printk("%s: [onefilefs_read()] - read avvenuta con successo\n", MODNAME);

	return ret;
}

// Close operation
int onefilefs_release(struct inode *inode, struct file *file) {
	
	// controlla se il filesystem è montato
	if(!fs_info.mounted){
		printk("%s: [onefilefs_release()] - il file system non è montato\n", MODNAME);
        return -ENODEV;
	}

	printk("%s: [onefilefs_release()] - device correttamente rilasciato\n", MODNAME);

	return 0;
}

const struct inode_operations onefilefs_inode_ops = {
    .lookup = onefilefs_lookup,
};

const struct file_operations onefilefs_file_operations = {
  .read = onefilefs_read,
  .open = onefilefs_open,
  .release = onefilefs_release,
};