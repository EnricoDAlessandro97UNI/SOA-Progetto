#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "utils_header.h"

// questa funzione restituisce il puntatore alla struttura dati che comprende le informazioni contenute nel superblocco del dispositivo
struct onefilefs_sb_info *get_sb_info(struct super_block *global_sb) {
    
    struct buffer_head *bh;
    struct onefilefs_sb_info *sb_disk;

    bh = sb_bread(global_sb, SB_BLOCK_NUMBER);
    if (!(global_sb && bh)) {
        return NULL;
    }

    sb_disk = (struct onefilefs_sb_info *) bh->b_data;
    brelse(bh);

    return sb_disk;
}   

// questa funzione restituisce il puntatore alla struttura dati che comprende i metadati + dati del blocco
struct bdev_layout* get_block(struct super_block *global_sb, unsigned int block_num) {

    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return NULL;
    }
    bdev_blk = (struct bdev_layout *) bh->b_data;
    brelse(bh);

    return bdev_blk;
}

// questa funzione scrive sul superblocco del dispositivo
int set_sb_info(struct super_block *global_sb, unsigned int new_first_valid, unsigned int new_last_valid) {

    struct buffer_head *bh;
    struct onefilefs_sb_info *sb_disk;

    bh = sb_bread(global_sb, SB_BLOCK_NUMBER);
    if (!(global_sb && bh)) {
        return -1;
    }
    sb_disk = (struct onefilefs_sb_info *) bh->b_data;
    sb_disk->first_valid = new_first_valid;
    sb_disk->last_valid = new_last_valid;

    mark_buffer_dirty(bh);

    // forza la scrittura in modo sincrono sul device
    #ifdef SYNC_WRITE_BACK 
    if(sync_dirty_buffer(bh) == 0) {
        AUDIT printk(KERN_INFO "%s: scrittura sincrona avvenuta con successo", MODNAME);
    }
    else {
        printk(KERN_CRIT "%s: scrittura sincrona fallita", MODNAME);
    }
    #endif

    brelse(bh);

    return 0;
}

// questa funzione scrive soltanto i metadati su uno specifico blocco all'interno del dispositivo
int set_block_metadata_valid(struct super_block *global_sb, unsigned int last_valid, unsigned int next_block_num) {

    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;

    bh = sb_bread(global_sb, last_valid);
    if (!(global_sb && bh)) {
        return -1;
    }
    bdev_blk = (struct bdev_layout *) bh->b_data;
    bdev_blk->next_block = set_valid(next_block_num);

    mark_buffer_dirty(bh);

    // forza la scrittura in modo sincrono sul device
    #ifdef SYNC_WRITE_BACK 
    if(sync_dirty_buffer(bh) == 0) {
        AUDIT printk(KERN_INFO "%s: scrittura sincrona avvenuta con successo", MODNAME);
    }
    else {
        printk(KERN_CRIT "%s: scrittura sincrona fallita", MODNAME);
    }
    #endif

    brelse(bh);

    return 0;
}

// questa funzione aggiorna i metadati di uno specifico blocco all'interno del dispositivo
int update_block_metadata(struct super_block *global_sb, unsigned int block_num, unsigned int next_block_num) {

    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return -1;
    }

    bdev_blk = (struct bdev_layout *) bh->b_data;
    bdev_blk->next_block = set_valid(next_block_num);  

    mark_buffer_dirty(bh);

    // forza la scrittura in modo sincrono sul device
    #ifdef SYNC_WRITE_BACK 
    if(sync_dirty_buffer(bh) == 0) {
        AUDIT printk(KERN_INFO "%s: scrittura sincrona avvenuta con successo", MODNAME);
    }
    else {
        printk(KERN_CRIT "%s: scrittura sincrona fallita", MODNAME);
    }
    #endif

    brelse(bh);

    return 0;
}

// questa funzione scrive soltanto
int set_block_data(struct super_block *global_sb, unsigned int block_num, char *source, size_t size) {

    int i;
    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return -1;
    }

    bdev_blk = (struct bdev_layout *) bh->b_data;
    bdev_blk->next_block = set_valid(-1);   // questo è l'ultimo blocco inserito e reso valido, non ha un successore

    for(i = 0; i < DATA_SIZE; i++) {
        if (i < size)
            bdev_blk->data[i] = source[i];
        else
            bdev_blk->data[i] = '\0';
    }

    mark_buffer_dirty(bh);

    // forza la scrittura in modo sincrono sul device
    #ifdef SYNC_WRITE_BACK 
    if(sync_dirty_buffer(bh) == 0) {
        AUDIT printk(KERN_INFO "%s: scrittura sincrona avvenuta con successo", MODNAME);
    }
    else {
        printk(KERN_CRIT "%s: scrittura sincrona fallita", MODNAME);
    }
    #endif

    brelse(bh);

    return 0;
}

// questa funzione invalida uno specifico blocco all'interno del dispositivo
int invalidate_block(struct super_block *global_sb, unsigned int block_num) {

    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;

    bh = sb_bread(global_sb, block_num);
    if (!(global_sb && bh)) {
        return -1;
    }
    bdev_blk = (struct bdev_layout *) bh->b_data;

    // rendo invalido il blocco target
    bdev_blk->next_block = set_invalid((unsigned int) -1);

    mark_buffer_dirty(bh);

    // forza la scrittura in modo sincrono sul device
    #ifdef SYNC_WRITE_BACK 
    if(sync_dirty_buffer(bh) == 0) {
        AUDIT printk(KERN_INFO "%s: scrittura sincrona avvenuta con successo", MODNAME);
    }
    else {
        printk(KERN_CRIT "%s: scrittura sincrona fallita", MODNAME);
    }
    #endif

    brelse(bh);

    return 0;
}

// questa funzione restituisce il numero di blocco che punta all'ultimo blocco valido
unsigned int get_previous_last_valid(struct super_block *global_sb, unsigned int first_valid, unsigned int last_valid) {
    
    unsigned int block_num = -1;
    unsigned int curr_block_num = first_valid;
    int cycle = 0;
    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;

    while (cycle < NBLOCKS-2) {
        bh = sb_bread(global_sb, blk_offset(curr_block_num));
        if (!(global_sb && bh)) {
            return -1;
        }
        bdev_blk = (struct bdev_layout *) bh->b_data;
        if (get_block_num(bdev_blk->next_block) == last_valid) {
            if (cycle == 0) {
                block_num = first_valid; 
                brelse(bh);
                break;
            }
            else {
                block_num = curr_block_num;
                brelse(bh);
                break;
            }
        }
        curr_block_num = get_block_num(bdev_blk->next_block);
        brelse(bh);
        cycle++;
    }

    return block_num;
}

// questa funzione aggiorna tutti i metadati dei blocchi coinvolti nell'invalidazione dell'unico blocco valido
int invalidate_one(struct super_block *global_sb, unsigned int offset, unsigned int new_first_valid, unsigned int new_last_valid) {
    
    int ret;

    // aggiorno il superblocco
    ret = set_sb_info(global_sb, new_first_valid, new_last_valid);
    if (ret < 0) {
        return -1;
    }

    // invalidazione del blocco (aggiornamento dei suoi metadati)
    ret = invalidate_block(global_sb, blk_offset(offset));
    if (ret < 0) {
        return -1;
    }

    return 0;
}

// questa funzione aggiorna tutti i metadati dei blocchi coinvolti nell'invalidazione del blocco in testa
int invalidate_first(struct super_block *global_sb, unsigned int offset, unsigned int new_first_valid, unsigned int new_last_valid) {
    
    int ret;

    // aggiorno il superblocco
    ret = set_sb_info(global_sb, new_first_valid, new_last_valid);
    if (ret < 0) {
        return -1;
    }

    // invalidazione del blocco (aggiornamento dei suoi metadati)
    ret = invalidate_block(global_sb, blk_offset(offset));
    if (ret < 0) {
        return -1;
    }

    return 0;
}

// questa funzione aggiorna tutti i metadati dei blocchi coinvolti nell'invalidazione di un blocco nel mezzo
int invalidate_middle(struct super_block *global_sb, unsigned int first_valid, unsigned block_to_invalidate) {

    unsigned int curr_block_num = first_valid;
    unsigned int prev_block_num = -1;
    unsigned int next_block_num = -1;

    int cycle = 0;
    int ret;

    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;

    while (cycle < NBLOCKS-2) {
        bdev_blk = get_block(global_sb, blk_offset(curr_block_num));
        if (bdev_blk == NULL) {
            return -1;
        }
        if (prev_block_num != -1) {
            next_block_num = get_block_num(bdev_blk->next_block);
            brelse(bh);
            break;
        }
        if (get_block_num(bdev_blk->next_block) == block_to_invalidate) {
            prev_block_num = curr_block_num;
        }
        curr_block_num = get_block_num(bdev_blk->next_block);
        brelse(bh);
        cycle++;
    }

    if (update_block_metadata(global_sb, blk_offset(prev_block_num), next_block_num) < 0) {
        return -1;
    }

    // invalidazione del blocco (aggiornamento dei suoi metadati)
    ret = invalidate_block(global_sb, blk_offset(block_to_invalidate));
    if (ret < 0) {
        return -1;
    }

    return 0; 
}

// questa funzione aggiorna tutti i metadati dei blocchi coinvolti nell'invalidazione di un blocco alla fine
int invalidate_last(struct super_block *global_sb, unsigned int offset, unsigned int first_valid, unsigned last_valid, unsigned int next_block_num) {

    int ret;
    unsigned int new_last_valid;

    // aggiorno i dati che andranno nel superblocco
    new_last_valid = get_previous_last_valid(global_sb, first_valid, last_valid);
    if (new_last_valid < 0) {
        return -1;
    }

    // aggiorno il blocco precedente a quello da eliminare in modo tale da farlo puntare al blocco successivo corretto
    if (update_block_metadata(global_sb, blk_offset(new_last_valid), next_block_num) < 0) {
        return -1;
    }

    // aggiorno il superblocco
    ret = set_sb_info(global_sb, first_valid, new_last_valid);
    if (ret < 0) {
        return -1;
    }

    // invalidazione del blocco (aggiornamento dei suoi metadati)
    ret = invalidate_block(global_sb, blk_offset(offset));
    if (ret < 0) {
        return -1;
    }

    return 0;
}

// for testing
void print_block_status(struct super_block *global_sb) {

    int cycle = 0;
    struct buffer_head *bh;
    struct bdev_layout *bdev_blk;

    while (cycle < NBLOCKS-2) {
        bh = sb_bread(global_sb, blk_offset(cycle));
        bdev_blk = (struct bdev_layout *) bh->b_data;
        printk(KERN_INFO "%s: %d -> %d | v: %d\n", MODNAME, cycle, get_block_num(bdev_blk->next_block), get_validity(bdev_blk->next_block));
        brelse(bh);
        cycle++;
    } 
}