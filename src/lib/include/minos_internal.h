/**
 * @file minos_internal.h
 * @author Roberto Gioiosa (roberto.gioiosa@pnnl.gov)
 * @brief Header file for internal structs and API of the MCL library.
 * @version 0.5
 * @date 2022-05-23
 * 
 */
#ifndef MINOS_INTERNAL_H
#define MINOS_INTERNAL_H

#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#include <debug.h>
#include <uthash.h>

#include "mem_list.h"
#include "AvlTree.h"

#if __APPLE__
#include <OpenCL/cl.h>
#include <pbarrier.h>
#else
#include <CL/cl.h>
#endif

#define BILLION 1000000000LL
#define MEGA 10E6

#define MCL_MAX_DEPENDENCIES 63

#define MCL_SHM_NAME "mcl_shm"
#define MCL_SHM_SIZE 1UL << 22
#define MCL_SOCK_NAME "/tmp/mcl_sched_sock"
#define MCL_SOCK_CNAME "/tmp/mcl_client.%ld"
#define MCL_SND_BUF (1 << 22UL)
#define MCL_RCV_BUF MCL_SND_BUF
#define MCL_MSG_SIZE (9 * sizeof(uint64_t)) + ((MCL_MAX_DEPENDENCIES + 1) * sizeof(uint32_t))
#define MCL_RES_ARGS_MAX 16
#define MCL_MAX_MSG_SIZE (MCL_MSG_SIZE + (MCL_RES_ARGS_MAX * sizeof(uint64_t)))
#define MCL_MSG_MAX (MCL_RCV_BUF / MCL_MAX_MSG_SIZE)
#define MCL_NUM_DEV_TYPES 3

#define MSG_CMD_NEX 0x00
#define MSG_CMD_REG 0x01
#define MSG_CMD_END 0x02
#define MSG_CMD_ACK 0x03
#define MSG_CMD_ERR 0x04
#define MSG_CMD_NULL 0x05
#define MSG_CMD_EXE 0x06
#define MSG_CMD_DONE 0x07
#define MSG_CMD_FREE 0x08
#define MSG_CMD_TRAN 0x09

#define MSG_CMD_SIZE 0x04
#define MSG_TYPE_SIZE 0x10
#define MSG_RES1_SIZE 0x0c
#define MSG_RID_SIZE 0x20
#define MSG_PES_SIZE 0x20
#define MSG_MEM_SIZE 0x1a
#define MSG_FLAGS_SIZE 0x06
#define MSG_NRES_SIZE 0x06
#define MSG_TASKID_SIZE 0x20
#define MSG_MEMID_SIZE 0x20
#define MSG_PID_SIZE 0x1c
#define MSG_MEMFLAG_SIZE 0x04
#define MSG_OVERALL_SIZE 0x20
#define MSG_MEMSIZE_SIZE 0x10
#define MSG_OFFSET_SIZE 0x10

#define MSG_CMD_MASK 0xf000000000000000
#define MSG_TYPE_MASK 0x0ffff00000000000
#define MSG_FLAGS_MASK 0x0000003f00000000
#define MSG_RID_MASK 0x00000000ffffffff
#define MSG_PES_MASK 0xffffffff00000000
#define MSG_MEM_MASK 0x00000000ffffffc0
#define MSG_NRES_MASK 0x000000000000003f
#define MSG_TASKID_MASK 0x00000000ffffffff
#define MSG_MEMID_MASK 0xffffffff00000000
#define MSG_PID_MASK 0x00000000fffffff0
#define MSG_MEMFLAG_MASK 0x000000000000000f
#define MSG_OVERALL_MASK 0xffffffff00000000
#define MSG_MEMSIZE_MASK 0x00000000ffff0000
#define MSG_OFFSET_MASK 0x000000000000ffff

#define MSG_CMD_SHIFT 0x3c
#define MSG_TYPE_SHIFT 0x2c
#define MSG_FLAGS_SHIFT 0x20
#define MSG_RID_SHIFT 0x00
#define MSG_PES_SHIFT 0x20
#define MSG_MEM_SHIFT 0x06
#define MSG_NRES_SHIFT 0x00
#define MSG_TASKID_SHIFT 0x00
#define MSG_MEMID_SHIFT 0x20
#define MSG_PID_SHIFT 0x04
#define MSG_MEMFLAG_SHIFT 0x00
#define MSG_OVERALL_SHIFT 0x20
#define MSG_MEMSIZE_SHIFT 0x10
#define MSG_OFFSET_SHIFT 0x00

#define MSG_ARGFLAG_EXCLUSIVE 0x01
#define MSG_ARGFLAG_SHARED 0x02

#define CLI_NONE 0x0
#define CLI_ACTIVE 0x1

#define MCL_NONE 0x00
#define MCL_STARTED 0x01
#define MCL_ACTIVE 0x02
#define MCL_DONE 0x04
#define MCL_BLOCK 0x08

#define MCL_DEV_MUL_CPU 1
#define MCL_DEV_MUL_GPU 512
#define MCL_DEV_MUL_FPGA 1

// These should be adjusted based on specific device but I am not sure how to get that now
#define MCL_DEV_MKERNELS_GPU 64 // CUDA compute capability 6.0 and 7.0
#define MCL_DEV_MKERNELS_CPU 4
#define MCL_DEV_MKERNELS_FPGA 4

#define MCL_DEV_MEM_SAFTEY_FACTOR 0.05
#define MCL_TASK_OVERHEAD 0.1

#define MCL_DEV_LSIZE_MIN 32
#define MCL_DEV_LSIZE_MAX 32

#define MCL_KER_NONE 0x00
#define MCL_KER_BUILT 0x01
#define MCL_KER_ERR 0x02
#define MCL_KER_COMPILING 0x03

#define MCL_PAGE_SIZE 4096.0
#define MCL_MEM_PAGE_SIZE 1024

#define MCL_TASK_FLAG_MASK 0xF00
#define MCL_TASK_FLAG_SHIFT 0x008

#define MCL_TASK_CHECK_ATTEMPTS 5
#define MCL_MAX_QUEUES_PER_DEVICE 16

extern uint64_t avail_dev_types;

typedef struct mcl_info_struct
{
    uint64_t status;
    uint64_t flags;

    uint64_t nclass;
    uint64_t nplts;
    uint64_t ndevs;

    uint64_t sndbuf;
    uint64_t rcvbuf;
} mcl_info_t;

typedef struct mcl_device_struct
{
    uint64_t id;

    cl_device_type type;
    cl_long mem_size;
    uint64_t pes;
    uint64_t max_kernels;
    cl_int punits;
    char name[CL_MAX_TEXT];
    double driver_version;
    cl_uint ndims;
    size_t wgsize;
    size_t *wisize;

    cl_platform_id cl_plt;
    cl_device_id cl_dev;
    cl_context cl_ctxt;

    uint64_t nqueues;
    cl_command_queue cl_queue[MCL_MAX_QUEUES_PER_DEVICE];

#ifdef _STATS
    uint64_t task_executed;
    uint64_t task_successful;
    uint64_t task_failed;
#endif
} mcl_device_t;

typedef struct mcl_platform_struct
{
    uint64_t id;

    cl_uint ndev;
    char name[CL_MAX_TEXT];
    char vendor[CL_MAX_TEXT];
    char version[CL_MAX_TEXT];

    cl_platform_id cl_plt;
    cl_device_id *cl_dev;
    mcl_device_t *devs;
} mcl_platform_t;

typedef struct mcl_class_struct
{
    char name[CL_MAX_TEXT];
    struct mcl_class_struct *next;
} mcl_class_t;

typedef struct mcl_resource_struct
{
    mcl_platform_t *plt;
    mcl_device_t *dev;
    union
    {
        uint64_t pes_used; // for scheduler
        uint64_t pes_req;  // for clients
    };
    union
    {
        uint64_t mem_avail;
        uint64_t mem_req;
    };
    uint64_t nkernels;
    uint64_t class;
    uint64_t status;
#ifdef _STATS
    struct timespec last_assigned;
    struct timespec last_released;
    struct timespec tused;
    struct timespec tidle;
#endif
} mcl_resource_t;

typedef struct mcl_sched_struct
{
    uint64_t nclients;
    uint64_t flags;
#ifdef _STATS
    uint64_t nreqs;
#endif
} mcl_sched_t;

typedef struct mcl_client_struct
{
    pid_t pid;
    uint64_t status;
    uint64_t flags;
    uint64_t start_cpu;
    uint64_t num_threads;
    struct sockaddr_un addr;
    struct mcl_client_struct *prev;
    struct mcl_client_struct *next;
} mcl_client_t;

typedef struct mcl_desc_struct
{
    uint64_t workers;
    uint64_t flags;
    pid_t pid;
    
    uint64_t start_cpu;

    int shm_fd;
    int sock_fd;
    struct sockaddr_un caddr;
    struct sockaddr_un saddr;
    uint64_t out_msg;

    mcl_info_t *info;
    mcl_device_t *devs;

    struct worker_struct *wids;
    pthread_t wcleanup;
    pthread_barrier_t wt_barrier;
    unsigned long num_reqs;

#ifdef _STATS
    unsigned long nreqs;
    unsigned long max_reqs;
#endif
} mcl_desc_t;

/**
 * @brief Structure of data for resident arguments in message
 *
 */
typedef struct msg_arg_struct
{
    /** Memory id currently is not the address so the whole address does not have to be sent in a message **/
    uint64_t mem_id;
    uint64_t pid;
    uint64_t overall_size;
    uint64_t mem_size;
    uint64_t mem_offset;
    uint64_t flags;
} msg_arg_t;

typedef struct msg_pes_struct
{
    uint64_t pes[MCL_DEV_DIMS];
    uint64_t lpes[MCL_DEV_DIMS];
} msg_pes_t;

/**
 * @brief Structure of message between client and scheduler
 * pid doesn't get sent but this struct store the src pid
 * from sockaddr_un once the message arrive at destination
 */
typedef struct mcl_msg_struct
{
    uint64_t cmd;
    uint64_t type;
    pid_t pid;
    union
    {
        uint64_t pes;
        uint64_t res;
        uint64_t threads;
    };
    uint64_t mem;
    uint64_t rid;
    uint64_t flags;
    uint32_t ndependencies;
    uint32_t dependencies[MCL_MAX_DEPENDENCIES];
    uint32_t taskid;
    msg_pes_t pesdata;
    uint64_t nres;
    msg_arg_t *resdata;
} mcl_msg;

typedef struct mcl_pobj_struct{
	unsigned long  status;
	unsigned char* binary;
	size_t         binary_len;
	cl_program     cl_prg;
} mcl_pobj;

typedef struct mcl_program_struct{
	char*           key;
	char*           path;
	char*           src;
	size_t          src_len;
	char*           opts;
        uint64_t        flags;
        uint64_t        targets;
        size_t          nkernels;
        size_t          knames_size;
        char*           knames;
	mcl_pobj*       objs;
	struct mcl_program_struct* next;
} mcl_program;

struct kernel_program{
        mcl_program*           p;
        struct kernel_program* next;
};

typedef struct mcl_kernel_struct{
        char*                     name;
        unsigned long             targets;
        struct kernel_program*    prg;
        pthread_rwlock_t          lock;
        struct mcl_kernel_struct* next;
} mcl_kernel;

typedef struct mcl_context{
	size_t*                 gsize;
	size_t*                 lsize;

	cl_program              prg;
	cl_kernel               kernel;
	cl_mem*                 buffers;
	cl_command_queue        queue;
	cl_event                event;
        cl_event                exec_event;
	
} mcl_context;

typedef struct
{
    unsigned long addr;
} mcl_rdata_key;

typedef struct
{
    uint64_t offset;
    uint64_t size;
    uint64_t refs;
    int64_t device;
    cl_mem clBuffer;
} mcl_subbuffer; // NOTE: Subbuffers are always assumed to be exclusive. They are only valid on one device

typedef struct mcl_rdata_struct
{
    mcl_rdata_key key;
    uint32_t id;
    unsigned long flags;
    size_t size;
    uint32_t refs;
    uint64_t host_is_valid;
    uint64_t devices;
    uint64_t evicted;
    cl_mem clBuffers[CL_MAX_DEVICES];
    uint64_t num_partitions;
    pthread_rwlock_t tree_lock;
    Tree children;
} mcl_rdata;

/**
 * @brief Argument data for the client
 * This is different than the data sent to the scheduler and the schedulers representation of the
 * argument.
 * TODO: each of the three need to keep track of different data, but having 3 different structures leads to replication
 * and confusion
 *
 */
typedef struct mcl_arg_struct
{
    void *addr;
    size_t size;
    off_t offset;
    uint64_t flags;
    int new_buffer;
    int moved_data;
    mcl_rdata *rdata_el;
} mcl_arg;

struct mcl_request_struct;

typedef struct mcl_task_struct
{
    mcl_kernel*  kernel;
	mcl_arg*     args;
	uint64_t     nargs;
	mcl_context  ctx;

    uint32_t ndependencies;
    struct mcl_request_struct *dependencies[MCL_MAX_DEPENDENCIES];
    uint64_t dependency_status;
    uint32_t ndependents;
    cl_event dependent_events[MCL_MAX_DEPENDENCIES];

    uint64_t tpes;
    uint64_t pes[MCL_DEV_DIMS];
    uint64_t lpes[MCL_DEV_DIMS];
    uint64_t offsets[MCL_DEV_DIMS];
    cl_uint dims;
    uint64_t mem;
} mcl_task;

// Forward decleration
struct worker_struct;

typedef struct mcl_request_struct
{
    uint32_t key;
    uint64_t res;
    mcl_handle *hdl;
    mcl_task *tsk;
    UT_hash_handle hh;
    struct worker_struct *worker;
} mcl_request;

typedef struct mcl_rlist_struct
{
    uint32_t rid;
    mcl_request *req;
    struct mcl_rlist_struct *next;
    struct mcl_rlist_struct *prev;
} mcl_rlist;

struct worker_struct
{
    pthread_t tid;
    uint64_t id;
    unsigned long ntasks;

#ifdef _STATS
    unsigned long max_tasks;
    uint64_t nreqs;
    uint64_t bytes_transfered;

    /** FIXME: this statistic should probably be somehow associated with each pair of devices? **/
    uint64_t n_transfers;
#endif
};

#define MCL_MAX_NAME_LEN 61
#define MCL_HDL_NAME_EXT_SIZE 4
#define MCL_HDL_NAME_EXT "_mcl"

#ifdef MCL_SHARED_MEM
typedef struct mcl_shm_struct
{
    pthread_mutex_t lock;
    uint32_t mem_id;
    uint32_t ref_counter;
    char name[MCL_MAX_NAME_LEN];
    pid_t original_pid;
    pid_t creator_pid[CL_MAX_DEVICES];
    uint64_t devs;
    size_t size;
    List subbuffers;
    char data[];
} mcl_shm_t;
#endif // MCL_SHARED_MEM

#define __get(field) mcl_desc->field
#define __get_plt(plt, field) mcl_desc->platformData[plt].field
#define __get_dev(plt, dev, field) mcl_desc->platformData[plt].deviceData[dev].field

#define __cget(field) mcl_cdesc.shm_ptr->field
#define __cget_plt(plt, field) mcl_cdesc.shm_ptr->platformData[plt].field
#define __cget_dev(plt, dev, field) mcl_cdesc.shm_ptr->platformData[plt].deviceData[dev].field

#define __get_time(val) clock_gettime(CLOCK_MONOTONIC, val)
#define __diff_time_ns(end, start) BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec
#define __nstos(val) ((float)val) / BILLION

#define p2order(val) (uint64_t) ceil(log2(val))
#define order2p(val) (uint64_t)(1UL << val)

#define req_getTask(r) r->tsk
#define req_getHdl(r) r->hdl
#define task_getArgAddr(t, i) &(t->args[i])
#define task_getCtxAddr(t) &(t->ctx)
#define task_getKernel(t) (t->kernel)
#define res_getClCtx(id) mcl_res[id].dev->cl_ctxt
#define res_getClDev(id) mcl_res[id].dev->cl_dev
#define res_getClQueue(id, idx) mcl_res[id].dev->cl_queue[idx]
#define res_getDev(id) mcl_res[id].dev
#define prg_getObj(p, id) &(p->objs[id])

extern mcl_desc_t mcl_desc;
extern mcl_resource_t *mcl_res;

int resource_discover(cl_platform_id **, mcl_platform_t **, mcl_class_t **, uint64_t *, uint64_t *);
int resource_map(mcl_platform_t *, uint64_t, mcl_class_t *, mcl_resource_t *);
int resource_create_ctxt(mcl_resource_t *, uint64_t);

int class_count(mcl_class_t *);
int cli_setup(void);
int cli_shutdown(void);

void *worker(void *);
void *check_pending(void *);

int __am_null(mcl_request *);
int __internal_wait(mcl_handle *, int, uint64_t);
int __am_exec(mcl_handle *, mcl_request *, uint64_t);
mcl_handle *__task_create(uint64_t);
mcl_handle *__task_init(char *, char *, uint64_t, char *, unsigned long);
int __task_cleanup(mcl_task *);
int __set_arg(mcl_handle *, uint64_t, void *, size_t, off_t, uint64_t);
int __set_prg(char*, char*, unsigned long);
int __set_kernel(mcl_handle*, char*, uint64_t);
void CL_CALLBACK __task_complete(cl_event e, cl_int status, void *v_request);

mcl_transfer *__transfer_create(uint64_t, uint64_t, uint64_t);
int __buffer_register(void *, size_t, uint64_t);
int __buffer_unregister(void *);
int __invalidate_buffer(void *);
cl_command_queue __get_queue(uint64_t);

int msg_setup(int);
int msg_init(mcl_msg *);
int msg_send(mcl_msg *, int, struct sockaddr_un *);
int msg_recv(mcl_msg *, int, struct sockaddr_un *);
void msg_free(mcl_msg *);

int req_init(void);
int req_add(mcl_request **, uint32_t, mcl_request *);
mcl_request *req_del(mcl_request **, uint32_t);
mcl_request *req_search(mcl_request **, uint32_t);
uint32_t req_count(mcl_request **);
int req_clear(mcl_request **);
void req_print(mcl_request **);

int rlist_init(pthread_rwlock_t *);
int rlist_add(mcl_rlist **, pthread_rwlock_t *, mcl_request *);
mcl_request *rlist_remove(mcl_rlist **, pthread_rwlock_t *, uint32_t);
mcl_request *rlist_search(mcl_rlist **, pthread_rwlock_t *, uint32_t);
int rlist_count(mcl_rlist **, pthread_rwlock_t *);
mcl_rlist *rlist_pop(mcl_rlist **, pthread_rwlock_t *);
int rlist_append(mcl_rlist **, pthread_rwlock_t *, mcl_rlist *);

int cli_remove(struct mcl_client_struct **, pid_t);
int cli_add(struct mcl_client_struct **, struct mcl_client_struct *);
struct mcl_client_struct *cli_search(struct mcl_client_struct **, pid_t);
pid_t cli_get_pid(const char *);

int          prgMap_init(void);
mcl_program* prgMap_add(char*, char*, uint64_t, uint64_t);
mcl_program* prgMap_search(char*);
int          prgMap_remove(mcl_program*);
void         prgMap_finit(void);

int          kerMap_init(void);
mcl_kernel*  kerMap_add(char*);
int          kerMap_add_prg(mcl_kernel*, mcl_program*);
mcl_kernel*  kerMap_search(char*);
int          kerMap_remove(mcl_kernel*);
void         kerMap_finit(void);
mcl_program* kerMap_get_prg(mcl_kernel*, uint64_t);

uint64_t arg_flags_to_cl_flags(uint64_t);
int rdata_init(void);
int rdata_free(void);
mcl_rdata *rdata_add(void *addr, uint32_t id, size_t size, uint64_t flags);
mcl_rdata *rdata_get(void *addr, int hold);
cl_mem rdata_get_mem(mcl_rdata *rdata, uint64_t device, size_t size, off_t offset, uint64_t flags, cl_command_queue queue, cl_int *err);
void rdata_put(mcl_rdata *el);
int rdata_put_mem(mcl_rdata *rdata, off_t offset);
int rdata_remove_subbuffers(mcl_rdata *rdata, size_t size, off_t offset);
int rdata_del(mcl_rdata *rdata);
int rdata_release_mem(mcl_rdata *rdata, uint64_t dev);
int rdata_release_mem_by_id(uint32_t memid, uint64_t dev);
uint32_t get_mem_id();
int rdata_invalidate_gpu_mem(mcl_rdata *rdata);

#ifdef MCL_SHARED_MEM
int mcl_shm_init();
void *mcl_request_shared_mem(const char *, size_t, uint64_t);
int mcl_update_shared_mem(mcl_rdata *rdata, uint64_t new_dev, uint64_t size, uint64_t offset, int direction);
int mcl_delete_shared_mem_subbuffer(void *addr, uint64_t size, uint64_t offset);
cl_mem mcl_get_shared_mem(mcl_rdata *rdata, uint64_t dev, uint64_t flags);
int mcl_release_shared_mem(void *buffer);
int mcl_release_device_shared_mem(void *buffer, uint64_t dev);
pid_t mcl_get_shared_mem_pid(void *buffer);
int mcl_is_shared_mem_owner(void *buffer, uint64_t dev);

uint32_t mcl_get_shared_hdl_id(mcl_handle *m_hdl);
mcl_handle *mcl_get_shared_hdl(pid_t pid, uint32_t sid);
void mcl_free_shared_hdl(mcl_handle *);
mcl_handle *mcl_allocate_shared_hdl(void);

int mcl_shm_free(void);
#else
#define mcl_shm_init(args...)
#define mcl_request_shared_mem(args...) 0
#define mcl_get_shared_mem_pid(args...) -1
#define mcl_get_shared_mem(args...) NULL
#define mcl_update_shared_mem(args...)
#define mcl_delete_shared_mem_subbuffer(args...)
#define mcl_release_device_shared_mem(args...)
#define mcl_release_shared_mem(args...)
#define mcl_is_shared_mem_owner(args...) 0

#define mcl_get_shared_hdl_id(args...) 0
#define mcl_get_shared_hdl(args...) NULL
#define mcl_free_shared_hdl(args...)
#define mcl_allocate_shared_hdl(args...) NULL

#define mcl_shm_free(args...) NULL
#endif

#endif
