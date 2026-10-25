#include "mobj.h"

#include "pframe.h"
#include "util/string.h"

/*
 * TODO: implement me!
 *
 * Hints:
 *   - when we write the ops, we want to make sure that we copy the value
 *     of the parameters into the space that we've been provided
 */
void mobj_create(mobj_t *o, mobj_ops_t *ops) {
    o->mo_ops = *ops;
    list_init(&o->mo_pframes);
    spinlock_init(&o->mo_mutex);
}

/*
 * TODO: implement me!
 *
 * Hints:
 *   - in this function, where do we search for frame?
 *   - if we don't find the frame, set the pfp to NULL
 */
void mobj_find_pframe(mobj_t *o, long pagenum, pframe_t **pfp) {
    *pfp = NULL;

    spinlock_lock(&o->mo_mutex);
    for (list_link_t *link = o->mo_pframes.head; link != NULL; link = link->next) {
        pframe_t *frame = (pframe_t *)(link->parent);
        if (pagenum == frame->pf_pagenum) {
            *pfp = frame;
            break;
        }
    }
    spinlock_unlock(&o->mo_mutex);

}

/*
 * TODO: implement me!
 *
 * Hints:
 *   - the frame might be in our list already...
 *   - we have access to a function to read the pframe from disk!
 *   - check how we get data from disk in sys/disk/disk.{h,c}...
 */
void mobj_get_pframe(mobj_t *o, long pagenum, pframe_t **pfp) {
    // is it already in memory?
    mobj_find_pframe(o, pagenum, pfp);
    if (*pfp != NULL) {
        return;
    }

    // if not, retireve it from disk
    pframe_t *pf = NULL;
    if (o->mo_ops.read_pframe(o, pagenum, &pf) < 0 || pf == NULL) {
        *pfp = NULL;
        return;
    }
    pf->pf_pagenum = pagenum;
    list_link_init(&pf->pf_link, pf);

    // insert so the list stays sored by page number
    spinlock_lock(&o->mo_mutex);
    list_link_t *after = o->mo_pframes.head;

    while (after != NULL && ((pframe_t *) after->parent)->pf_pagenum < pagenum) {
        after = after->next;
    }

    if (after == NULL) {
        list_insert_back(&o->mo_pframes, &pf->pf_link);
    } else if (after == o->mo_pframes.head) {
        list_insert_front(&o->mo_pframes, &pf->pf_link);
    } else {
        pf->pf_link.prev = after->prev;
        pf->pf_link.next = after;
        after->prev->next = &pf->pf_link;
        after->prev = &pf->pf_link;
        o->mo_pframes.size++;
    }

    spinlock_unlock(&o->mo_mutex);

    *pfp = pf;

}

/*
 * TODO: implement me!
 *
 * Hints:
 *   - pos describes where to read from in terms of bytes
 *   - len is the maximum number of bytes to read... when can we not read all
 *     of the way up to len?
 *   - save what we read to buf
 */
long mobj_read(mobj_t *o, long pos, long len, char *buf) {
    if (pos <0 || len < 0 || buf == NULL) {
        return -1;
    }

    long done = 0;
    while (done < len) {
        long cur = pos + done;
        pframe_t *pf;
        mobj_get_pframe(o, cur / FRAME_LEN, &pf);
        if (pf == NULL) {
            break;
        }

        long off = cur % FRAME_LEN;
        long n = FRAME_LEN - off;
        if (n > len - done) {
            n = len - done;
        }
        memcpy(buf + done, pf->bytes + off, n);
        done += n;
    }
    return done;

}

/*
 * TODO: implement me!
 *
 * Hints:
 *   - pos describes where to write to in terms of bytes
 *   - len is the maximum number of bytes to write... when can we not read all
 *     of the way up to len?
 *   - save what we read to buf
 */
long mobj_write(mobj_t *o, long pos, long len, const char *buf) {
        if (pos <0 || len < 0 || buf == NULL) {
        return -1;
    }

    long done = 0;
    while (done < len) {
        long cur = pos + done;
        pframe_t *pf;
        mobj_find_pframe(o, cur / FRAME_LEN, &pf);
        if (pf == NULL) {
            break;
        }

        long off = cur % FRAME_LEN;
        long n = FRAME_LEN - off;
        if (n > len - done) {
            n = len - done;
        }
        memcpy(pf->bytes + off, buf + done, n);
        done += n;
    }
    return done;
}

/*
 * TODO: implement me!
 *
 * Hints:
 *   - make sure that disk is up to date in the places that we care about!
 */
void mobj_flush(mobj_t *o) {
    // write_pframe, not flush_pframe: svfs sets flush_pframe to NULL
    spinlock_lock(&o->mo_mutex);
    for (list_link_t *link = o->mo_pframes.head; link != NULL;
                                                  link = link->next) {
        o->mo_ops.write_pframe(o, (pframe_t *) link->parent);
    }
    spinlock_unlock(&o->mo_mutex);
}
