#ifndef MINOS_SCHED_H
#define MINOS_SCHED_H

#include <minos.h>
#include <minos_internal.h>

#include <utlist.h>
#include <mem_list.h>

#define SCHED_REQ_NONE 0x00
#define SCHED_REQ_WAIT 0x01
// All dependencies are exec ready
#define SCHED_REQ_SCHED_READY 0x02
// All dependencies are finished
#define SCHED_REQ_EXEC_READY 0x03
#define SCHED_REQ_DONE 0x04

/** Return from scheduler resource policy. Indicates task was skipped and needs some event to trigger task **/
#define MCL_SCHED_BLOCK -2
/** Return from scheduler resource policy. Indicates task was skipped, but could be scheduled on another try **/
#define MCL_SCHED_AGAIN -1

#define ld_acq(addr) __atomic_load_n(addr, __ATOMIC_ACQUIRE)
#define add_fetch(addr, val) __atomic_add_fetch(addr, val, __ATOMIC_ACQ_REL)

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) ); })

struct eviction_node;

struct pid_node
{
    pid_t pid;
    struct pid_node *next;
    struct pid_node *prev;
};
typedef struct pid_node process_t;

int process_cmp(process_t *p1, process_t *p2);

/** Resident data struct for scheduler. Similar to the struct for the library, but only one struct per buffer **/
typedef struct sched_res_data
{
    union
    {
        struct
        {
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
    uint8_t valid;
    List subbuffers;
    struct eviction_node *enodes;
    process_t *processes;
    UT_hash_handle hh;

} sched_rdata;

typedef struct eviction_node
{
    sched_rdata *mem_data;
    int dev;
    uint64_t refs;
    void *pol_data;
} enode_t;

struct identifier
{
    union
    {
        struct
        {
            pid_t pid;
            uint64_t rid;
        };
        uint64_t key[2];
    };
};

typedef struct sched_request sched_req_t;

typedef struct dep_list
{
    sched_req_t *r;
    struct dep_list *next;
} dep_list;

struct sched_request
{
    struct identifier key;
    uint64_t type;
    uint64_t pes;
    uint64_t mem;
    uint64_t status;
    uint64_t flags;
    uint64_t dev;
    uint64_t num_attempts;
    uint32_t task_id;

    uint64_t dpes[MCL_DEV_DIMS];
    uint64_t lpes[MCL_DEV_DIMS];

    uint64_t nresident;
    sched_rdata **resdata;
    mcl_partition_t *regions;

    pthread_mutex_t dependent_lock;
    int dependencies_execing;
    int dependencies_waiting;
    dep_list *dependents;

    void *policy_data;
};

struct sched_resource_policy
{
    void (*init)(mcl_resource_t *, int n);
    int (*find_resource)(sched_req_t *);
    int (*assign_resource)(sched_req_t *);
    int (*put_resource)(sched_req_t *);
    int (*stats)();
};

struct sched_eviction_policy
{
    void (*init)(mcl_resource_t *, int);
    void (*init_node)(enode_t *);
    int (*used)(enode_t *);
    int (*released)(enode_t *);
    int (*removed)(enode_t *);
    enode_t *(*evict)(void);
    enode_t *(*evict_from_dev)(int);
    void (*destroy)(void);
};

struct sched_class
{
    const struct sched_resource_policy *respol;
    const struct sched_eviction_policy *evictionpol;
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
int default_put_resource(sched_req_t *);
int default_stats();
int scheduler_evict_mem(int dev);

int sched_rdata_init(void);
int sched_rdata_add_device(sched_rdata *el, int dev);
int sched_rdata_on_device(sched_rdata *el, int dev);
int sched_rdata_rm_device(sched_rdata *el, int dev);
int sched_rdata_rm_pid(pid_t pid, uint64_t *mem_freed, uint64_t ndevs);
int sched_rdata_add(sched_rdata *el);
sched_rdata *sched_rdata_rm(uint64_t mem_id, pid_t pid);
sched_rdata *sched_rdata_get(uint64_t mem_id, pid_t pid);
int sched_rdata_free(void);

static inline int sched_assign_resource(sched_req_t *r)
{
    return sched_curr->respol->assign_resource(r);
}

static inline int sched_put_resource(sched_req_t *r)
{
    return sched_curr->respol->put_resource(r);
}

static inline int sched_stats()
{
    return sched_curr->respol->stats();
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

static inline enode_t *eviction_policy_evict(void)
{
    return sched_curr->evictionpol->evict();
}

static inline enode_t *eviction_policy_evict_from_dev(int dev)
{
    return sched_curr->evictionpol->evict_from_dev(dev);
}

static inline int eviction_policy_used(enode_t *e)
{
    return sched_curr->evictionpol->used(e);
}

static inline int eviction_policy_released(enode_t *e)
{
    return sched_curr->evictionpol->released(e);
}

static inline void eviction_policy_init_node(enode_t *e)
{
    return sched_curr->evictionpol->init_node(e);
}

static inline int eviction_policy_removed(enode_t *e)
{
    return sched_curr->evictionpol->removed(e);
}
#endif
