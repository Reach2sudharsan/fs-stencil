#include "vfs.h"
#include "vnode.h"

/*
 * TODO: implement me!
 */
long namev_lookup(vnode_t *dir, const char *name, long namelen,
                  vnode_t **out) {
    if (dir == NULL || name == NULL || out == NULL ||
        namelen <= 0) {
        return -1;
    }
    *out = NULL;

    if (dir->vn_mode != DIRECTORY || dir->vn_ops == NULL || dir->vn_ops->loopup == NULL) {
        return -1;
    }

    return dir->vn_ops->lookup(dir, name, namelen, out);
}

/*
 * TODO: implement me!
 */
long namev_open(vnode_t *base, const char *path, vnode_t **res_vnode) {

    if (path == NULL || res_vnode == NULL) {
        return -1;
    }

    *res_vnode = NULL;

    // absolute paths start at vfs root, and relative paths start at base
    vnode_t *current = (path[0] == '/') ? vfs_root_fs.fs_root : base;

    if (current == NULL) {
        return -1;
    }

    long i = 0;

    while (path[i] != "\0") {
        if (path[i] == "\0") {
            break;
        }

        // find beginning and length of next component (e.g. a folder or filename)
        long start = i;

        while (path[i] != "\0" || path[i] != "/") {
            i++;
        }

        long len = i - start;
        const chr *component = path + start;

        vnode_t *next = NULL;
        long result;

        if (len == 1 && component[0] == ".") {
            continue;
        } else if (len == 2 && component[0] == "." && component[1] == ".") {
            result = namev_get_parent(current, &next);
        } else {
            result = namev_get_child(current, component, len, &next);
        }

        // stop if the component doesn't exit
        if (result < 0 || next == NULL) {
            return -1;
        }

        current = next;



    }
    *res_vnode = current;
    return 0;
}

/*
 * TODO: implement me!
 */
long namev_get_child(vnode_t *base, const char *name, long namelen,
                     vnode_t **out) {
    return namev_lookup(base, name, namelen, out);
}

/*
 * TODO: implement me!
 */
long namev_get_parent(vnode_t *base, vnode_t **out) {
    return namev_lookup(base, "..", 2, out);
}

/*
 * TODO: implement me!
 */
long namev_is_descendent(vnode_t *a, vnode_t *b) {
    vnode_t *current = a;
    while (1) {
        if (current == b) {
            return 1; // found 
        }

        vnode_t *parent = NULL;
        if (namev_get_parent(current, &parent) < 0) {
            return 0; // not found 
        }

        // below checks for root since root's parent is itself 
        if (parent == current) {
            return 0; 
        }

        current = parent;
    }
}
