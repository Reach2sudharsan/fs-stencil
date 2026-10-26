/*
 * how to run: 
 * ./bin/test_persist write: create /persist.txt, write to it, flush
 * ./bin/test_persist: read it back and check it 
 */
#include "svfs.h"
#include "slab_allocator.h"

int printf(const char *fmt, ...);

int main(int argc, char **argv) {
    mem_init();
    slab_init();
    svfs_init();

    vnode_t *root = vfs_root_fs.fs_root;
    char msg[] = "Hello world!";
    vnode_t *file;

    if (argc > 1) {
        svfs_mknod(root, "persist.txt", 11, &file);
        svfs_write(file, 0, msg, sizeof(msg));
        mobj_flush(&file->vn_mobj); // the file's data
        mobj_flush(&root->vn_mobj); // root's new entry for "persist.txt"
        return 0;
    }

    // otherwise: this is the second run; find file in disk
    if (svfs_lookup(root, "persist.txt", 11, &file) != 0) {
        printf("FAIL: persist.txt not found\n");
        return 1;
    }

    char result[sizeof(msg)];
    svfs_read(file, 0, result, sizeof(msg));
    for (long i = 0; i < sizeof(msg); i++) {
        if (result[i] != msg[i]) {
            printf("FAIL!");
            return 1;
        }
    }
    printf("PASS!");
    return 0;
}
