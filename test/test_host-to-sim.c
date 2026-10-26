
#include <stdio.h>
#include <string.h>

#include "svfs.h"
#include "vfs.h"
#include "vnode.h"
#include "slab_allocator.h"
#include "sys/disk/disk.h"

#define CHUNK_SIZE 1024

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <host-file>\n", argv[0]);
        return 1;
    }

    // open a file from host
    FILE *host = fopen(argv[1], "rb");
    if (host == NULL) {
        perror("fopen");
        return 1;
    }

    // initialize simulated filesystem
    slab_init();

    if (disk_init() != 0) {
        fprintf(stderr, "Disk initialization failed\n");
        fclose(host);
        return 1;
    }

    svfs_init();

    vnode_t *root = vfs_root_fs.fs_root;
    if (root == NULL || root->vn_ops == NULL ||
        root->vn_ops->mknod == NULL) {
        fprintf(stderr, "Root vnode initialization failed\n");
        fclose(host);
        return 1;
    }

    // create regular file inside simulated root directory
    const char *filename = "imported.txt";
    vnode_t *file = NULL;

    if (root->vn_ops->mknod(root, filename, strlen(filename),
                           &file) != 0 || file == NULL) {
        fprintf(stderr, "Failed to create simulated file\n");
        fclose(host);
        return 1;
    }

    // copy the host file into simulated filesystem
    unsigned char buffer[CHUNK_SIZE];
    size_t n;
    long offset = 0;

    while ((n = fread(buffer, 1, sizeof(buffer), host)) > 0) {
        if (file->vn_ops == NULL || file->vn_ops->write == NULL ||
            file->vn_ops->write(file, offset, buffer, (long)n) != 0) {
            fprintf(stderr, "Simulated write failed at offset %ld\n",
                    offset);
            fclose(host);
            return 1;
        }

        offset += (long)n;
    }

    if (ferror(host)) {
        perror("fread");
        fclose(host);
        return 1;
    }

    printf("Copied %ld bytes into /%s\n", offset, filename);

    // flush mobj memory to the simulated disk
    mobj_flush(&file->vn_mobj);

    fclose(host);
    return 0;
}
