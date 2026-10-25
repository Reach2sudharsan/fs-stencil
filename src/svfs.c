#include "svfs.h"

#include "dirent.h"
#include "slab_allocator.h"
#include "sys/disk/disk.h"
#include "util/string.h"

fs_ops_t svfs_fsops = { .fs_init = svfs_init,
                        .read_vnode = svfs_read_vnode};

static vnode_ops_t svfs_dir_vops = {.read = NULL,
                                    .write = NULL,
                                    .mknod = svfs_mknod,
                                    .mkdir = svfs_mkdir,
                                    .lookup = svfs_lookup,
                                    .readdir = svfs_readdir,
                                    .read_pframe = svfs_read_pframe,
                                    .write_pframe = svfs_write_pframe,
                                    .flush_pframe = NULL};

static vnode_ops_t svfs_file_vops = {.read = svfs_read,
                                     .write = svfs_write,
                                     .mknod = NULL,
                                     .mkdir = NULL,
                                     .lookup = NULL,
                                     .readdir = NULL,
                                     .read_pframe = svfs_read_pframe,
                                     .write_pframe = svfs_write_pframe,
                                     .flush_pframe = NULL};

/*
 * TODO: implement me!
 *
 * Hints:
 *   - what is the main file system reference? how should our svfs instance
 *     refer to this main system?
 *   - it is your job here to implement both the fields of the svfs_t struct
 *     and the fs_t struct!
 */

 // need to add the following to initialize the fields of svfs_t struct
static svfs_t svfs_instance;
static slab_allocator_t svfs_node_allocator;

void svfs_init() {
    fs_t *fs = &vfs_root_fs;

    fs->fs_ops = &svfs_fsops;
    list_init(&fs->vnode_list);
    spinlock_init(&fs->vnode_list_mutex);

    svfs_instance.svfs_fs = fs;
    spinlock_init(&svfs_instance.svfs_mutex);

    fs->vnode_allocator = &svfs_node_allocator;
    slab_allocator_init(&svfs_node_allocator, sizeof(svfs_node_t));

    char block[SV_BLOCK_SIZE];

    read_disk_block(SV_SUPER_BLOCK, block);
    memcpy(&svfs_instance.svfs_super, block, sizeof(svfs_super_t));

    svfs_node_t *root = slab_obj_alloc(svfs_node_allocator);

    if (root == NULL) {
        return;
    }

    root->vnode.parent = root;
    root->inode.svi_number = svfs_instance.svfs_super.svs_root_inode;

    svfs_read_vnode(fs, &root->vnode);

    fs->fs_root = &root->vnode;
    list_link_init(&root->vnode.vn_link, &root->vnode);
    list_insert_back(&fs->vnode_list, &root->vnode.vn_link);

}

/*
 * Initialize a svfs_node/vnode after reading a file from disk.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - how do we get data from disk?
 *   - what should the svfs_node/vnode fields be for a given file?
 *   - think about the distinction between normal file and directory!
 */
void svfs_read_vnode(fs_t *fs, vnode_t *vn) {
    if (fs == NULL || vn == NULL || vn->parent == NULL) {
        return;
    }

    svfs_node_t *node = (svfs_node_t *) vn->parent;
    long inode_number = node->inode.svi_number;

    if (inode_number < 1 ||
        inode_number > svfs_instance.svfs_super.svs_num_inodes) {
        return;
    }

    char block[SV_BLOCK_SIZE];
    read_disk_block(inode_number, block);
    memcpy(&node->inode, block, sizeof(node->inode));

    vn->vn_fs = fs;
    vn->vn_len = node->inode.svi_un.svi_size; // not sure about this because what if there is a free node

    // the uploaded svfs_inode_t has no persistent file-type field
    // this recognizes only the known root directory
    if (inode_number == svfs_instance.svfs_super.svs_root_inode) {
        vn->vn_mode = DIRECTORY;
        vn->vn_ops = &svfs_dir_vops;
    } else {
        // temporary assumption, cannot identify other directories
        vn->vn_mode = REGULAR;
        vn->vn_ops = &svfs_file_vops;
    }

    // vnode_mobj_ops is static inside vnode.c, so initialize with the exported wrapper functions instead
    mobj_ops_t ops = {
        .read_pframe = vnode_read_pframe,
        .write_pframe = vnode_write_pframe,
        .flush_pframe = vnode_flush_pframe
    };

    mobj_create(&vn->vn_mobj, &ops);
    vn->vn_mobj.parent = vn;
    node->dirtied_inode = 0;

}


/*
 * Check the super block. Return 0 on success, else -1
 *
 * TODO: implement me!
 */
long svfs_check_super(svfs_super_t *super) {
    // validate the number of free blocks stored in the superblock
    if (super->svs_nfree < 0 ||
        super->svs_nfree > SV_NFBLKS_PER_NODE) {
        return -1;
    }

    // validate the root inode number
    if (super->svs_root_inode < 1 ||
        super->svs_root_inode > super->svs_num_inodes) {
        return -1;
    }

    // validate the free inode pointer (0 means none available)
    if (super->svs_free_inode < 0 ||
        super->svs_free_inode > super->svs_num_inodes) {
        return -1;
    }

    return 0;
}

/*
 * On file creation, this is called to attach an inode to the newly created
 * file. Choose an unused inode from the file system. Returns 0 on succes,
 * -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - what is the next available inode?
 *   - where do we find the next available inode after we allocate this inode?
 */
long svfs_alloc_inode(svfs_t *svfs) {
    if (svfs == NULL) {
        return -1;
    }

    spinlock_lock(&svfs->svfs_mutex);

    // get the first available inode
    long inode_num = svfs->svfs_super.svs_free_inode;

    if (inode_num <= 0 || inode_num > svfs->svfs_super.svs_num_inodes) {
        spinlock_unlock(&svfs->svfs_mutex);
        return -1;
    }

    // read the inode to find the next free inode
    svfs_inode_t inode;
    char block[SV_BLOCK_SIZE];

    read_disk_block(inode_num, block);
    memcpy(&inode, block, sizeof(svfs_inode_t));

    // advance the free-inode linked list.
    svfs->svfs_super.svs_free_inode = inode.svi_un.svi_next_free;

    spinlock_unlock(&svfs->svfs_mutex);

    return 0;

}

/*
 * Find a directory entry from a svfs_node with the input name/namelen and
 * pass the associated svfs_node/vnode to the ent pointer. Return 0 on success,
 * else -1.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - how should we read a directory entry?
 */
long svfs_find_dirent(svfs_node_t *dir, const char *name, long namelen,
                      svfs_node_t *ent) {
    if (dir == NULL || name == NULL || ent == NULL || namelen <= 0 || namelen > NAME_LEN) {
        return -1;
    }

    // search each directory entry
    for (long pos = 0; pos + sizeof(dirent_t) <= dir->vnode.vn_len; pos += sizeof(dirent_t)) {

        dirent_t entry;

        // read the directory entry from its data blocks
        if (mobj_read(&dir->vnode.vn_mobj, pos, sizeof(dirent_t), (char *)&entry) != sizeof(dirent_t)) {
            return -1;
        }

        // compare filename lengths and contents
        if (entry.d_ino != 0 && strlen(entry.d_name) == namelen && strncmp(entry.d_name, name, namelen) == 0) {

            // read the inode associated with the matching entry
            char block[SV_BLOCK_SIZE];
            read_disk_block(entry.d_ino, block);

            memcpy(&ent->inode, block, sizeof(svfs_inode_t));
            return 0;
        }
    }

    return -1; // entry not found


}

/*
 * Read len bytes from the file associated with the vnode_t from position pos
 * and save the result into the buffer. Return 0 on success, -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - where is the data?
 *   - how do we access data from a svfs_node/vnode?
 */
long svfs_read(vnode_t *vn, long pos, void *buf, long len) {
    if (vn == NULL || buf == NULL || pos < 0 || len < 0) {
        return -1;
    }

    // cannot read beyond the end of the file
    if (pos >= vn->vn_len) {
        return 0;
    }

    // limit reading to the remaining file size
    if (len > vn->vn_len - pos) {
        len = vn->vn_len - pos;
    }

    // read bytes through the vnode's memory object
    long bytes_read = mobj_read(&vn->vn_mobj, pos, len, buf);

    return (bytes_read == len) ? 0 : -1;
}

/*
 * Write len bytes from the buffer into the file at position pos. If the write
 * requires more bytes than are currently available, svfs should allocate
 * another data block to the file. Returns 0 on success, else -1.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - where do new data blocks come from?
 *   - where should we put the data relative to the vnode?
 */
long svfs_write(vnode_t *vn, long pos, const void *buf, long len) {
    if (vn == NULL || buf == NULL || pos < 0 || len < 0) {
        return -1;
    }

    if (pos + len > SV_NDIRECT_BLKS * SV_BLOCK_SIZE) {
        return -1;
    }

    svfs_node_t *node = (svfs_node_t *) vn->parent;
    svfs_super_t *superblock = &svfs_instance.svfs_super;

    // for every page we write, give it a block on disk
    for (long p = pos / SV_BLOCK_SIZE; p * SV_BLOCK_SIZE < pos + len; p++) {
        if (node->inode.svi_direct_blocks[p] == 0) {
            // slots [0, nfree - 2] are free blocks; the last slot is the overflow link
            if (superblock->svs_nfree <= 1) {
                return -1; 
            }
            node->inode.svi_direct_blocks[p] = superblock->svs_free_blocks[superblock->svs_nfree - 2];
            superblock->svs_nfree--;
        }
    }

    // put the bytes in the file's pages via mobj_write
    if (mobj_write(&vn->vn_mobj, pos, len, buf) != len) {
        return -1;
    }

    // update the length of the file 
    if (pos + len > vn->vn_len) {
        vn->vn_len = pos + len;
        node->inode.svi_un.svi_size = pos + len;
    }

    // save the inode and superblock, so that the file still exists after a restart
    char inode_block[SV_BLOCK_SIZE];
    memcpy(inode_block, &node->inode, sizeof(svfs_inode_t));
    write_disk_block(node->inode.svi_number, inode_block);

    char super_block[SV_BLOCK_SIZE];
    memcpy(super_block, superblock, sizeof(svfs_super_t));
    write_disk_block(SV_SUPER_BLOCK, super_block);

    return 0;
}

/*
 * Creates a new entry for a file (e.g., a call to "touch") for a file that
 * does not yet exist. Creates the file in the provided directory with the
 * the provided name and passes the newly created vnode to the out parameter.
 * Returns 0 on success, -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - don't forget to add the newly created vnode to any global tracking
 *     mechanisms!
 */
long svfs_mknod(vnode_t *dir, const char *name, long namelen, vnode_t **out) {
    if (dir == NULL || name == NULL || out == NULL || namelen <= 0 || namelen > NAME_LEN) {
        return -1;
    }

    long inode_num = svfs_alloc_inode(&svfs_instance);
    if (inode_num < 0) {
        return -1;
    }    

    // create and save new inode 
    svfs_node_t *node = slab_obj_alloc(svfs_node_allocator);
    node->inode.svi_un.svi_size = 0;
    node->inode.svi_number = inode_num;
    node->inode.svi_count = 1;
    for (long i = 0; i < SV_NDIRECT_BLKS; i++) {
        node->inode.svi_direct_blocks[i] = 0;
    }
    node->inode.svi_indirect_block = 0;
    node->inode.svi_mode = REGULAR;

    // save
    char inode_block[SV_BLOCK_SIZE];
    memcpy(inode_block, &node->inode, sizeof(svfs_inode_t));
    write_disk_block(inode_num, inode_block);

    node->vnode.parent = node;
    svfs_read_vnode(dir->vn_fs, &node->vnode);
    list_link_init(&node->vnode.vn_link, &node->vnode);
    list_insert_back(&dir->vn_fs->vnode_list, &node->vnode.vn_link);

    dirent_t entry;
    entry.d_ino = inode_num;
    entry.d_off = dir->vn_len + sizeof(dirent_t);
    strncpy(entry.d_name, name, namelen);
    entry.d_name[namelen] = '\0';

    if (svfs_write(dir, dir->vn_len, &entry, sizeof(dirent_t)) != 0) {
        return -1;
    }

    *out = &node->vnode;
    return 0;
}

/*
 * Creates a new entry for a directory for a file that does not yet exist.
 * Creates the file in the provided directory with theprovided name and passes
 * the newly created vnode to the out parameter.  Returns 0 on success, -1 on
 * failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - don't forget to add the newly created vnode to any global tracking
 *     mechanisms!
 */
long svfs_mkdir(vnode_t *dir, const char *name, long namelen, vnode_t **out) {
    if (dir == NULL || name == NULL || out == NULL || namelen <= 0 || namelen > NAME_LEN) {
        return -1;
    }

    long inode_num = svfs_alloc_inode(&svfs_instance);
    if (inode_num < 0) {
        return -1;
    }

    // create and save new inode
    svfs_node_t *node = slab_obj_alloc(svfs_node_allocator);
    node->inode.svi_un.svi_size = 0;
    node->inode.svi_number = inode_num;
    node->inode.svi_count = 1;
    for (long i = 0; i < SV_NDIRECT_BLKS; i++) {
        node->inode.svi_direct_blocks[i] = 0;
    }
    node->inode.svi_indirect_block = 0;
    node->inode.svi_mode = DIRECTORY; 

    // save
    char inode_block[SV_BLOCK_SIZE];
    memcpy(inode_block, &node->inode, sizeof(svfs_inode_t));
    write_disk_block(inode_num, inode_block);

    node->vnode.parent = node;
    svfs_read_vnode(dir->vn_fs, &node->vnode);
    list_link_init(&node->vnode.vn_link, &node->vnode);
    list_insert_back(&dir->vn_fs->vnode_list, &node->vnode.vn_link);

    dirent_t dots[2];
    dots[0].d_ino = inode_num;
    dots[0].d_name[0] = '.';
    dots[1].d_ino = ((svfs_node_t *) dir->parent)->inode.svi_number;
    dots[1].d_name[0] = '.';
    dots[1].d_name[1] = '.';

    if (svfs_write(&node->vnode, 0, dots, sizeof(dots)) != 0) {
        return -1;
    }

    dirent_t entry;
    entry.d_ino = inode_num;
    strncpy(entry.d_name, name, namelen);

    if (svfs_write(dir, dir->vn_len, &entry, sizeof(dirent_t)) != 0) {
        return -1;
    }

    *out = &node->vnode;
    return 0;
}


/*
 * Search for a given entry within a directory. If found, entry is passed back
 * via the out pointer. Returns 0 on success, -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - recall that the data in a directory is a series of dirents (defined in
 *     dirent.h)
 *   - what happens if we can't find the directory?
 */
long svfs_lookup(vnode_t *dir, const char *name, long namelen, vnode_t **out) {
    if (dir == NULL || name == NULL || out == NULL || dir->vn_mode != DIRECTORY) {
        return -1;
    }

    // obtain the inode number from the name 
    svfs_node_t found;
    if (svfs_find_dirent((svfs_node_t *) dir->parent, name, namelen, &found) != 0) {
        return -1; // no such name in this directory
    }
    long inode_num = found.inode.svi_number;

    // check if the file is already in memory 
    for (list_link_t *link = dir->vn_fs->vnode_list.head; link != NULL; link = link->next) {
        vnode_t *vn = (vnode_t *) link->parent;
        if (((svfs_node_t *) vn->parent)->inode.svi_number == inode_num) {
            *out = vn;
            return 0;
        }
    }

    svfs_node_t *node = slab_obj_alloc(svfs_node_allocator);
    node->vnode.parent = node;
    node->inode.svi_number = inode_num;
    svfs_read_vnode(dir->vn_fs, &node->vnode);

    list_link_init(&node->vnode.vn_link, &node->vnode);
    list_insert_back(&dir->vn_fs->vnode_list, &node->vnode.vn_link);

    *out = &node->vnode;
    return 0;
}

/*
 * Retrieve the directory entry at the location specified by pos in the
 * provided directory. Return 0 on success, -1 on failure.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - index the data associated with the directory
 *   - what happens if pos is not aligned to the dirent?
 */
long svfs_readdir(vnode_t *vnode, long pos, dirent_t **d) {
    if (vnode == NULL || d == NULL || vnode->vn_mode != DIRECTORY) {
        return -1;
    }

    // pos must be the start of an entry
    if (pos < 0 || pos % sizeof(dirent_t) != 0) {
        return -1;
    }

    // pos must not be past the last entry
    if (pos + sizeof(dirent_t) > vnode->vn_len) {
        return -1;
    }

    slab_allocator_t dirent_allocator;
    slab_allocator_init(&dirent_allocator, sizeof(dirent_t));
    dirent_t *entry = slab_obj_alloc(dirent_allocator);

    if (mobj_read(&vnode->vn_mobj, pos, sizeof(dirent_t), (char *) entry) != sizeof(dirent_t)) {
        return -1;
    }
    entry->d_off = pos + sizeof(dirent_t); 

    *d = entry;
    return 0;
}

/*
 * Read the data associated with a file from disk. Return 0 on success, -1 on
 * error.
 *
 * TODO: implement me!
 *
 * Hints:
 *   - are there helper functions that can help us out?
 *   - how do we access these helpers?
 */
long svfs_read_pframe(vnode_t *vnode, long pagenum, pframe_t **pfp) {
    if (vnode == NULL || pfp == NULL || pagenum < 0 || pagenum >= SV_NDIRECT_BLKS) {
        return -1;
    }

    svfs_node_t *node = (svfs_node_t *) vnode->parent;

    // find the disk block that holds this page
    long block = node->inode.svi_direct_blocks[pagenum];
    if (block == 0) {
        return -1; // this page has no block on disk
    }

    slab_allocator_t pframe_allocator;
    slab_allocator_init(&pframe_allocator, sizeof(pframe_t));
    pframe_t *pf = slab_obj_alloc(svfs_pframe_allocator);
    pf->pf_pagenum = pagenum;
    read_disk_block(block, pf->bytes);

    *pfp = pf;
    return 0;
}

/*
 * Write pframe to disk. Return 0 on success, -1 on error.
 *
 * TODO: implement me!
 */
long svfs_write_pframe(vnode_t *vnode, pframe_t *pf) {
    if (vnode == NULL || pf == NULL) {
        return -1;
    }

    svfs_node_t *node = (svfs_node_t *) vnode->parent;

    long block = node->inode.svi_direct_blocks[pf->pf_pagenum];
    if (block == 0) {
        return -1;
    }

    write_disk_block(block, pf->bytes);
    return 0;
}
