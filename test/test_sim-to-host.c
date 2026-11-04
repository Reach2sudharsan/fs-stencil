
#include <stdio.h>
#include "svfs.h"
#include "vfs.h"
#include "vnode.h"
#include "slab_allocator.h"
#include "sys/disk/disk.h"

#include "dirent.h"

#include "sys/mem/mem.h"


#define CHUNK_SIZE 1024

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <host-output-file>\n", argv[0]);
        return 1;
    }

    // initialize simulated filesystem
    mem_init();
    slab_init();

    if (disk_init() != 0) {
        fprintf(stderr, "Disk initialization failed\n");
        return 1;
    }

    svfs_init();

    // find the file previously imported in test 1
    vnode_t *file = NULL;

    // if (namev_open(vfs_root_fs.fs_root, "/imported.txt", &file) != 0 ||
    //     file == NULL) {
    //     fprintf(stderr, "Failed to find /imported.txt\n");
    //     return 1;
    // }


    vnode_t *root = vfs_root_fs.fs_root;
    if (root == NULL) {
        fprintf(stderr, "root vnode initialization failed\n");
        return 1;
    }
    printf("root length=%ld, dirent size=%zu\n",
        root->vn_len, sizeof(dirent_t));

    for (long pos = 0; pos + sizeof(dirent_t) <= root->vn_len;
        pos += sizeof(dirent_t)) {
        dirent_t entry = {0};
        long got = mobj_read(&root->vn_mobj, pos, sizeof(entry),
                            (char *)&entry);
        printf("pos=%ld read=%ld inode=%ld name='%.*s'\n",
            pos, got, entry.d_ino, NAME_LEN + 1, entry.d_name);
    }

    if (svfs_lookup(vfs_root_fs.fs_root, "imported.txt", 12, &file) != 0 ||
        file == NULL) {
        fprintf(stderr, "Direct lookup failed\n");
        return 1;
    }

    if (file->vn_mode != REGULAR ||
        file->vn_ops == NULL ||
        file->vn_ops->read == NULL) {
        fprintf(stderr, "Not a readable regular file\n");
        return 1;
    }

    // create an output file on host machine
    FILE *host = fopen(argv[1], "wb");
    if (host == NULL) {
        perror("fopen");
        return 1;
    }

    unsigned char buffer[CHUNK_SIZE];
    long offset = 0;

    // copy the simulated file's contents to host
    while (offset < file->vn_len) {
        long count = file->vn_len - offset;

        if (count > CHUNK_SIZE) {
            count = CHUNK_SIZE;
        }

        if (file->vn_ops->read(file, offset, buffer, count) != 0) {
            fprintf(stderr, "Simulated read failed at offset %ld\n",
                    offset);
            fclose(host);
            return 1;
        }

        if (fwrite(buffer, 1, (size_t)count, host) != (size_t)count) {
            perror("fwrite");
            fclose(host);
            return 1;
        }

        offset += count;
    }

    fclose(host);

    printf("Exported %ld bytes from /imported.txt to %s\n",
           offset, argv[1]);

    return 0;
}
