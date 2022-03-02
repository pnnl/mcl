#ifndef MINOS_SCHED_H
#define MINOS_SCHED_H

#include <minos.h>
#include <minos_internal.h>

/** Return from scheduler resource policy. Indicates task was skipped and needs some event to trigger task **/
#define MCL_SCHED_BLOCK     -2
/** Return from scheduler resource policy. Indicates task was skipped, but could be scheduled on another try **/
#define MCL_SCHED_AGAIN     -1

#define ld_acq(addr) __atomic_load_n(addr, __ATOMIC_ACQUIRE)
#define add_fetch(addr, val) __atomic_add_fetch(addr, val, __ATOMIC_ACQ_REL)

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

/** Resident data struct for scheduler. Similar to the struct for the library, but only one struct per buffer **/
typedef struct sched_res_data {
    union {
		struct {
			pid_t pid;
			uint64_t mem_id;
		};
		uint64_t key[2];
	};
    uint64_t devs;
    uint64_t size;
    uint64_t refs;
    uint64_t ndevs;
    uint64_t flags;
    UT_hash_handle  hh;
} sched_rdata;

typedef struct sched_request {
	union {
		struct {
			pid_t pid;
			uint64_t rid;
		};
		uint64_t key[2];
	};
	uint64_t type;
	uint64_t pes;
	uint64_t mem;
	uint64_t status;
        uint64_t nresident;
        uint64_t flags;
	uint64_t dev;
        uint64_t num_attempts;
        uint64_t dpes[MCL_DEV_DIMS];
        uint64_t lpes[MCL_DEV_DIMS];
        sched_rdata** resdata;
} sched_req_t;

struct sched_resource_policy {
	void (*init)(mcl_resource_t *, int n);
	int (*find_resource)(sched_req_t *);
	int (*assign_resource)(sched_req_t *);
	int (*put_resource)(sched_req_t *);
};

struct sched_class {
	const struct sched_resource_policy *respol;
	int (*init)(void *args);
	int (*finit)(void);
	struct sched_request *(*alloc_request)(void);
	void (*release_request)(struct sched_request *);
	int (*enqueue)(struct sched_request *);
	int (*dequeue)(struct sched_request *);
	struct sched_request *(*pick_next)(void);
	int (*queue_len)(void);
	int (*complete)(struct sched_request *);
};

extern struct sched_class fifo_class;
extern struct sched_class fffs_class;
extern struct sched_class *sched_curr;

int default_assign_resource(sched_req_t *r);
int default_put_resource(sched_req_t*);

int             sched_rdata_init(void);
int             sched_rdata_add_device(sched_rdata* el, int dev);
int             sched_rdata_on_device(sched_rdata* el, int dev);
int             sched_rdata_rm_device(sched_rdata* el, int dev);
int             sched_rdata_rm_pid(pid_t pid, uint64_t* mem_freed, uint64_t ndevs);
int             sched_rdata_add(sched_rdata* el);
sched_rdata*    sched_rdata_rm(uint64_t mem_id, pid_t pid);
sched_rdata*    sched_rdata_get(uint64_t mem_id, pid_t pid);
int             sched_rdata_free(void);

static inline int sched_assign_resource(sched_req_t *r)
{
	return sched_curr->respol->assign_resource(r);
}

static inline int sched_put_resource(sched_req_t *r)
{
	return sched_curr->respol->put_resource(r);
}

static inline int default_complete(sched_req_t *r)
{
	return 0;
}

static inline int sched_init(void *args)
{
	return sched_curr->init(args);
}

static inline int sched_finit(void)
{
	return sched_curr->finit();
}

static inline sched_req_t *sched_alloc_request(void)
{
	sched_req_t *r = sched_curr->alloc_request();

	if (r)
		memset(r, 0, sizeof(*r));

	return r;
}

static inline void sched_release_request(sched_req_t *r)
{
    free(r->resdata);
	sched_curr->release_request(r);
    return;
}

static inline int sched_enqueue(sched_req_t *req)
{
	return sched_curr->enqueue(req);
}

static inline int sched_dequeue(sched_req_t *req)
{
	return sched_curr->dequeue(req);
}

static inline sched_req_t *sched_pick_next(void)
{
	return sched_curr->pick_next();
}

static inline int sched_queue_len(void)
{
	return sched_curr->queue_len();
}

static inline int sched_complete(sched_req_t *req)
{
	return sched_curr->complete(req);
}

#endif
