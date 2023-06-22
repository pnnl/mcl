#include <pthread.h>
#include <stdio.h>

#include <atomics.h>
#include <minos.h>
#include <minos_internal.h>
#include <minos_sched_internal.h>
#include <utlist.h>

typedef struct fffs_request_struct {
    sched_req_t req;
    struct fffs_request_struct *next;
} fffs_req_t;

static fffs_req_t *plist;
static pthread_mutex_t fffs_plock;
static pthread_cond_t fffs_cond;

struct sched_class fffs_class; /* forward declaration */

static sched_req_t *fffs_alloc_request(void) {
    fffs_req_t *f = malloc(sizeof(fffs_req_t));

    if (!f)
        return NULL;

    f->next = NULL;

    return &f->req;
}

static void fffs_release_request(sched_req_t *r) {
    /* actually there is not strictly required to retreive fffs_req_t since
     * sched_request field is the first and its address is the same, we do it
     * just as an example of a correct free
     */
    fffs_req_t *f = container_of(r, fffs_req_t, req);

    free(f);
}

/*
 * Add a new element at the end of the pending task queue
 */
static int fffs_enqueue(sched_req_t *r) {
    fffs_req_t *el = container_of(r, fffs_req_t, req);

    if (!el) {
        eprintf("Invalid argument!");
        return -1;
    }

    Dprintf("Adding request (%d,%" PRIu64 ")", r->key.pid, r->key.rid);
    pthread_mutex_lock(&fffs_plock);
    LL_APPEND(plist, el);
    pthread_cond_signal(&fffs_cond);
    pthread_mutex_unlock(&fffs_plock);

    return 0;
}

static int fffs_dequeue(sched_req_t *r) {
    fffs_req_t *el = container_of(r, fffs_req_t, req);

    if (!el) {
        eprintf("Invalid argument!");
        return -1;
    }

    Dprintf("Removing request (%d,%" PRIu64 ")", r->key.pid, r->key.rid);
    pthread_mutex_lock(&fffs_plock);
    if (plist)
        LL_DELETE(plist, el);
    pthread_mutex_unlock(&fffs_plock);

    return 0;
}

static int fffs_qlength(void) {
    fffs_req_t *el;
    int n;

    pthread_mutex_lock(&fffs_plock);
    LL_COUNT(plist, el, n);
    pthread_mutex_unlock(&fffs_plock);

    return n;
}

static sched_req_t *fffs_next(void) {
    fffs_req_t *r = NULL;
    int dev = MCL_SCHED_BLOCK;
    int block = 1;

    pthread_mutex_lock(&fffs_plock);
    while (dev < 0) {
        LL_FOREACH(plist, r) {
            dev = fffs_class.respol->find_resource(&r->req);
            block = dev == MCL_SCHED_AGAIN ? 0 : block;

            if (dev >= 0) {
                LL_DELETE(plist, r);
                break;
            }
        }
        if (dev >= 0) {
            break;
        }

        if (block) {
            pthread_cond_wait(&fffs_cond, &fffs_plock);
        }
    }
    pthread_mutex_unlock(&fffs_plock);

    return &r->req;
}

static int fffs_complete(sched_req_t *r) {
    pthread_cond_broadcast(&fffs_cond);
    return 0;
}

static int fffs_init(void *args) {
    Dprintf("Initializing FFFS (First-Feseable First-Served) scheduler");

    plist = NULL;

    if (pthread_mutex_init(&fffs_plock, NULL)) {
        eprintf("Error initializing FFFS scheduler plock");
        goto err;
    }
    if (pthread_cond_init(&fffs_cond, NULL)) {
        eprintf("Error initializing FFFS scheduler plock");
        goto err;
    }

    return 0;

err:
    return -1;
}

static int fffs_finit(void) {
    Dprintf("Finalizing FFFS scheduler");

    return 0;
}

extern const struct sched_resource_policy ff_policy;
extern const struct sched_eviction_policy lru_eviction_policy;

struct sched_class fffs_class = {
    .respol = &ff_policy,
    .evictionpol = &lru_eviction_policy,
    .init = fffs_init,
    .finit = fffs_finit,
    .alloc_request = fffs_alloc_request,
    .release_request = fffs_release_request,
    .enqueue = fffs_enqueue,
    .dequeue = fffs_dequeue,
    .pick_next = fffs_next,
    .queue_len = fffs_qlength,
    .complete = fffs_complete,
};
