#include "vfs.h"
#include "vnode.h"

/*
 * look up a name in a directory
 */
long namev_lookup(vnode_t *dir, const char *name, long namelen,
                  vnode_t **out) {
    if (dir == NULL || name == NULL || out == NULL ||
        namelen <= 0) {
        return -1;
    }
    *out = NULL;

    if (dir->vn_mode != DIRECTORY || dir->vn_ops == NULL || dir->vn_ops->lookup == NULL) {
        return -1;
    }

    return dir->vn_ops->lookup(dir, name, namelen, out);
}

/*
 * find the vnode for a path
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

    const char *p = path;

    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        // find beginning and length of next component (e.g. a folder or filename)

        const char *component = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }

        long len = p - component;
        vnode_t *next = NULL;
        long result;

        if (len == 1 && component[0] == '.') {
            continue;
        } else if (len == 2 &&
                   component[0] == '.' && component[1] == '.') {
            result = namev_get_parent(current, &next);
        } else {
            result = namev_get_child(current, component, len, &next);
        }

        if (result < 0 || next == NULL) {
            return -1;
        }
        current = next;
    }

    *res_vnode = current;
    return 0;
}

/*
 * get a directory's child by name
 */
long namev_get_child(vnode_t *base, const char *name, long namelen,
                     vnode_t **out) {
    return namev_lookup(base, name, namelen, out);
}

/*
 * get a directory's parent
 */
long namev_get_parent(vnode_t *base, vnode_t **out) {
    return namev_lookup(base, "..", 2, out);
}

/*
 * check whether a is inside b
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
