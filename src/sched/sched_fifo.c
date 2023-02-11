#include <pthread.h>
#include <stdio.h>

#include <atomics.h>
#include <minos.h>
#include <minos_internal.h>
#include <minos_sched_internal.h>
#include <utlist.h>

typedef struct fifo_request_struct {
    sched_req_t req;
    struct fifo_request_struct *next;
} fifo_req_t;

static fifo_req_t *plist;
static pthread_mutex_t fifo_plock;
static pthread_cond_t fifo_cond;

struct sched_class fifo_class; /* forward declaration */

static sched_req_t *fifo_alloc_request(void) {
    fifo_req_t *f = malloc(sizeof(fifo_req_t));

    if (!f)
        return NULL;

    f->next = NULL;

    return &f->req;
}

static void fifo_release_request(sched_req_t *r) {
    /* actually there is not strictly required to retreive fifo_req_t since
     * sched_request field is the first and its address is the same, we do it
     * just as an example of a correct free
     */
    fifo_req_t *f = container_of(r, fifo_req_t, req);

    free(f);
}

/*
 * Add a new element at the end of the pending task queue
 */
static int fifo_enqueue(sched_req_t *r) {
    fifo_req_t *el = container_of(r, fifo_req_t, req);

    if (!el) {
        eprintf("Invalid argument!");
        return -1;
    }

    Dprintf("Adding request (%d,%" PRIu64 ")", r->key.pid, r->key.rid);
    pthread_mutex_lock(&fifo_plock);
    LL_APPEND(plist, el);
    pthread_mutex_unlock(&fifo_plock);

    return 0;
}

static int fifo_dequeue(sched_req_t *r) {
    fifo_req_t *el = container_of(r, fifo_req_t, req);

    if (!el) {
        eprintf("Invalid argument!");
        return -1;
    }

    Dprintf("Removing request (%d,%" PRIu64 ")", r->key.pid, r->key.rid);
    pthread_mutex_lock(&fifo_plock);
    if (plist)
        LL_DELETE(plist, el);
    pthread_mutex_unlock(&fifo_plock);

    return 0;
}

static int fifo_qlength(void) {
    fifo_req_t *el;
    int n;

    pthread_mutex_lock(&fifo_plock);
    LL_COUNT(plist, el, n);
    pthread_mutex_unlock(&fifo_plock);

    return n;
}

static sched_req_t *fifo_next(void) {
    fifo_req_t *r;
    int dev = -1;

    /* fetch list head */
    pthread_mutex_lock(&fifo_plock);
    r = plist;
    pthread_mutex_unlock(&fifo_plock);

    if (!r)
        return NULL;

    pthread_mutex_lock(&fifo_plock);
    while ((dev = fifo_class.respol->find_resource(&r->req)) < 0) {
        if (dev == MCL_SCHED_BLOCK)
            pthread_cond_wait(&fifo_cond, &fifo_plock);
        else
            sched_yield();
    }
    LL_DELETE(plist, r);
    pthread_mutex_unlock(&fifo_plock);

    return &r->req;
}

static int fifo_complete(sched_req_t *r) {
    pthread_cond_broadcast(&fifo_cond);
    return 0;
}

static int fifo_init(void *args) {
    Dprintf("Initializing FIFO scheduler");

    plist = NULL;

    if (pthread_mutex_init(&fifo_plock, NULL)) {
        eprintf("Error initializing FIFO scheduler plock");
        goto err;
    }

    if (pthread_cond_init(&fifo_cond, NULL)) {
        eprintf("Error initializing FIFO scheduler condition variable");
        goto err;
    }

    return 0;

err:
    return -1;
}

static int fifo_finit(void) {
    Dprintf("Finalizing FIFO scheduler");

    return 0;
}

extern const struct sched_resource_policy ff_policy;
extern const struct sched_resource_policy rr_policy;
extern const struct sched_resource_policy delay_policy;

extern const struct sched_eviction_policy lru_eviction_policy;

struct sched_class fifo_class = {
    .respol = &ff_policy,
    .evictionpol = &lru_eviction_policy,
    .init = fifo_init,
    .finit = fifo_finit,
    .alloc_request = fifo_alloc_request,
    .release_request = fifo_release_request,
    .enqueue = fifo_enqueue,
    .dequeue = fifo_dequeue,
    .pick_next = fifo_next,
    .queue_len = fifo_qlength,
    .complete = fifo_complete,
};
