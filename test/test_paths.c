#include "svfs.h"
#include "slab_allocator.h"

int printf(const char *fmt, ...);

int main() {
    mem_init();
    slab_init();
    svfs_init();

    vnode_t *root = vfs_root_fs.fs_root;
    vnode_t *a, *b, *c, *found;

    // build /a/b/c.txt
    svfs_mkdir(root, "a", 1, &a);
    svfs_mkdir(a, "b", 1, &b);
    svfs_mknod(b, "c.txt", 5, &c);

    if (namev_open(root, "/a/b/c.txt", &found) != 0 || found != c) {
        printf("FAIL: /a/b/c.txt\n");
        return 1;
    }

    if (namev_open(root, "/a/./b/../b/c.txt", &found) != 0 || found != c) {
        printf("FAIL: . and ..\n");
        return 1;
    }

    if (namev_open(root, "/a/nope", &found) == 0) {
        printf("FAIL: found a file that doesn't exist\n");
        return 1;
    }

    if (namev_is_descendent(b, a) != 1 || namev_is_descendent(a, b) != 0) {
        printf("FAIL: is_descendent\n");
        return 1;
    }

    printf("PASS\n");
    return 0;
}
