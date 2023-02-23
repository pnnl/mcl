#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomics.h>
#include <minos.h>
#include <minos_internal.h>
#include <stats.h>
#include <uthash.h>
#include <utlist.h>

mcl_desc_t mcl_desc;
uint32_t curr_h = 0;
uint32_t curr_mem_id = 0;
static uint64_t curr_q[CL_MAX_DEVICES];
volatile uint64_t status = MCL_NONE;

mcl_request *hash_reqs = NULL;
mcl_rlist *ptasks = NULL;
mcl_rlist *ftasks = NULL;
pthread_rwlock_t ptasks_lock;
pthread_rwlock_t ftasks_lock;

cl_platform_id *cl_plts = NULL;
mcl_platform_t *mcl_plts = NULL;
mcl_resource_t *mcl_res = NULL;
mcl_class_t *mcl_class = NULL;

char *shared_mem_name = NULL;

static inline int cli_msg_send(struct mcl_msg_struct *msg) {
    int ret;

    while (((ret = msg_send(msg, mcl_desc.sock_fd, &mcl_desc.saddr)) > 0))
        sched_yield();

    return ret;
}

static inline int cli_msg_recv(struct mcl_msg_struct *msg) {
    struct sockaddr_un src;

    return msg_recv(msg, mcl_desc.sock_fd, &src);
}

static inline uint32_t get_rid(void) {
    return ainc(&curr_h);
}

uint32_t get_mem_id(void) {
    return ainc(&curr_mem_id);
}

cl_command_queue __get_queue(uint64_t dev) {
    uint64_t q_idx = curr_q[dev];
    curr_q[dev] = (curr_q[dev] + 1) % res_getDev(dev)->nqueues;
    return res_getClQueue(dev, q_idx);
}

static inline mcl_handle *hdl_init(uint64_t cmd, uint32_t rid, uint64_t status, uint64_t flags) {
    mcl_handle *h = NULL;

    if (flags & MCL_HDL_SHARED) {
        h = mcl_allocate_shared_hdl();
    }
    else {
        h = (mcl_handle *)malloc(sizeof(mcl_handle));
    }
    if (!h)
        return NULL;

    h->cmd = cmd;
    h->rid = rid;
    h->status = status;
    h->ret = MCL_RET_UNDEFINED;

    return h;
}

/**
 * @brief Register a buffer for use for subbuffers later
 *
 * @param addr the base pointer
 * @param size the size of the memory region
 * @return int
 */
int __buffer_register(void *addr, size_t size, uint64_t flags) {
    mcl_rdata *el;
    uint32_t id = get_mem_id();
    if (!(el = rdata_add(addr, id, size, flags))) {
        return -1;
    }
    return 0;
}

uint64_t arg_flags_to_cl_flags(uint64_t arg_flags) {
    uint64_t flags;
    if (arg_flags & MCL_ARG_RDONLY) {
        flags = CL_MEM_READ_ONLY;
    }
    else if (arg_flags & MCL_ARG_WRONLY) {
        flags = CL_MEM_WRITE_ONLY;
    }
    else {
        flags = CL_MEM_READ_WRITE;
    }

    return flags;
}

/**
 * @brief Unregister a buffer
 *
 * @param addr base pointer of the memory region, address that was passed to buffer register
 * @return int 0 on success, TODO: error codes unimplemented
 */
int __buffer_unregister(void *addr) {
    return rdata_del(rdata_get(addr, 0));
}

int __invalidate_buffer(void *addr) {
    return rdata_invalidate_gpu_mem(rdata_get(addr, 0));
}

mcl_handle *__task_create(uint64_t flags) {
    mcl_handle *hdl = NULL;
    mcl_task *tsk = NULL;
    mcl_request *req = NULL;

    hdl = hdl_init(MSG_CMD_NEX, 0, MCL_REQ_ALLOCATED, flags);
    if (!hdl) {
        eprintf("Error allocating MCL handle.");
        goto err;
    }

    tsk = (mcl_task *)malloc(sizeof(mcl_task));
    if (!tsk) {
        eprintf("Error allocating MCL task");
        goto err_hdl;
    }

    req = (mcl_request *)malloc(sizeof(mcl_request));
    if (!req) {
        eprintf("Error allocating MCL request");
        goto err_tsk;
    }

    hdl->rid = get_rid();
    memset((void *)tsk, 0, sizeof(mcl_task));
    memset((void *)req, 0, sizeof(mcl_request));

    req->hdl = hdl;
    req->tsk = tsk;

    if (rlist_add(&ptasks, &ptasks_lock, req)) {
        eprintf("Error adding task %u to not actvie task list", hdl->rid);
        goto err_req;
    }

    // Dprintf("Task %u created (req=%p)", hdl->rid, req);

    hdl->flags = flags;
    return hdl;

err_req:
    free(req);
err_tsk:
    free(tsk);
err_hdl:
    free(hdl);
err:
    return NULL;
}

mcl_transfer *__transfer_create(uint64_t nargs, uint64_t ncopies, uint64_t flags) {
    mcl_transfer *t = (mcl_transfer *)malloc(sizeof(mcl_transfer));
    t->nargs = nargs;
    t->args = (void **)malloc(sizeof(void *) * nargs);
    t->sizes = (uint64_t *)malloc(sizeof(uint64_t) * nargs);
    t->flags = (uint64_t *)malloc(sizeof(uint64_t) * nargs);
    t->offsets = (uint64_t *)malloc(sizeof(uint64_t) * nargs);
    t->ncopies = ncopies;
    t->handles = (mcl_handle **)malloc(sizeof(mcl_handle *) * ncopies);

    for (uint64_t i = 0; i < ncopies; i++) {
        t->handles[i] = __task_create(flags);
        t->handles[i]->cmd = MSG_CMD_TRAN;

        mcl_request *rqst = NULL;
        mcl_task *task = NULL;

        rqst = rlist_search(&ptasks, &ptasks_lock, t->handles[i]->rid);
        if (!rqst) {
            goto err;
        }
        task = req_getTask(rqst);
        task->args = (mcl_arg *)malloc(nargs * sizeof(mcl_arg));
        if (!(task->args)) {
            eprintf("Error allocating host memory for transfer");
            goto err;
        }
        task->nargs = nargs;
    }
    return t;

err:
    free(t);
    return NULL;
}

static inline mcl_pobj *__build_program(mcl_program *p, unsigned int n) {
    mcl_pobj *obj = prg_getObj(p, n);
    cl_context clctx = res_getClCtx(n);
    cl_device_id dev = res_getClDev(n);
    cl_int ret = 0;
#ifdef _DEBUG
    struct timespec start, end;
#endif

    Dprintf("Building program %s for resource %u (flags = %lu, obj %p)...", p->path, n, p->flags, obj);
#ifdef _DEBUG
    __get_time(&start);
#endif
    if (cas(&(obj->status), MCL_KER_NONE, MCL_KER_COMPILING)) {
        Dprintf("\t Building program %s for device %u (%lu bytes)...", p->key, n, p->src_len);
#ifdef _DEBUG
        if (p->flags & MCL_PRG_SRC)
            Dprintf("\t Program source: \n %s", p->src);
#endif
        switch (p->flags & MCL_PRG_MASK) {
        case MCL_PRG_SRC:
            obj->cl_prg = clCreateProgramWithSource(clctx, 1, (const char **)&(p->src),
                                                    (const size_t *)&(p->src_len), &ret);
            break;
#ifdef OPENCL2
        case MCL_PRG_IR:
            obj->cl_prg = clCreateProgramWithIL(clctx, (const void *)p->src, p->src_len, &ret);
            break;
#endif
        case MCL_PRG_BIN:
            obj->cl_prg = clCreateProgramWithBinary(clctx, 1, &dev, (const size_t *)&(p->src_len),
                                                    (const unsigned char **)&(p->src), NULL, &ret);
            break;
        case MCL_PRG_GRAPH:
            obj->cl_prg = clCreateProgramWithBuiltInKernels(clctx, 1, &dev, "dataflow", &ret);
            break;
        default:
            eprintf("Unsupported program type! (flags=0x%lx)", p->flags);
            goto err;
        }

        if (ret != CL_SUCCESS) {
            eprintf("Error loading CL program source (ret = %d)", ret);
            goto err;
        }

        Dprintf("\t Compiling program for resources %u (options %s)...", n, p->opts);

        ret = clBuildProgram(obj->cl_prg, 1, &dev, p->opts, NULL, NULL);
        if (ret != CL_SUCCESS) {
            eprintf("Error compiling CL program for resources %u (ret = %d). \n", n, ret);
            size_t log_size;
            clGetProgramBuildInfo(obj->cl_prg, dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
            char *log = (char *)malloc(log_size);
            clGetProgramBuildInfo(obj->cl_prg, dev, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
            eprintf("%s\n", log);
            goto err_prg;
        }

        cas(&(obj->status), MCL_KER_COMPILING, MCL_KER_BUILT);
    }
    else {
        while (obj->status == MCL_KER_COMPILING)
            sched_yield();

        assert(obj->status == MCL_KER_BUILT);
        Dprintf("\t Program %s has been already built for device %u.",
                p->key, n);
    }

#ifdef _DEBUG
    __get_time(&end);
#endif
    Dprintf("\t Compile time: %lld (obj %p)", __diff_time_ns(end, start), obj);

    return obj;

err_prg:
    clReleaseProgram(obj->cl_prg);
err:
    obj->status = MCL_KER_ERR;
    return NULL;
}

int __set_prg(char *path, char *copts, unsigned long flags) {
    int ret = 0;
    uint64_t archs = 0x0;
    unsigned int i;
    int target = -1;
    mcl_program *p;
    mcl_pobj *obj;

    /*
     * We need to compile the program for at least one resource to extract number
     * and names of the kernels contained in the program.
     */
    switch (flags) {
    case MCL_PRG_SRC: {
        archs = MCL_TASK_CPU | MCL_TASK_GPU;
        break;
    }
    case MCL_PRG_BIN: {
        archs = MCL_TASK_FPGA;
        break;
    }
    case MCL_PRG_GRAPH: {
        archs = MCL_TASK_DF;
        char* targ = "-DMCL_CONFIG="; //we pass along the path of the config so that pocl can access during the "build/compile" of the "ocl" program
        copts = (char*) malloc(sizeof(char) * strlen(targ)+strlen(path)+1);
        strcpy(copts,targ);
        strcpy(copts+strlen(targ),path);
        copts[ strlen(targ)+strlen(path)]='\0';
        break;
    }
    default: {
        eprintf("Support for type of program 0x%lx missing.", flags);
        goto err;
    }
    }

    Dprintf("\t Looking for a suitable device for program type 0x%lx...", flags);
    for (i = 0; i < mcl_desc.info->ndevs; i++)
        if (mcl_res[i].dev->type & archs) {
            target = i;
            break;
        }

    if (target == -1) {
        eprintf("No suitable device for program type 0x%lx found!", flags);
        ret = MCL_ERR_INVPRG;
        goto err;
    }

    p = prgMap_add(path, copts, flags, archs);
    if (!p) {
        eprintf("Error linking program %s.", path);
        ret = MCL_ERR_INVPRG;
        goto err;
    }

    obj = __build_program(p, target);
    if (!obj) {
        eprintf("Error building program %s for resource %d.", path, target);
        ret = MCL_ERR_INVPRG;
        goto err;
    }

    ret = clGetProgramInfo(obj->cl_prg, CL_PROGRAM_NUM_KERNELS, sizeof(size_t), &(p->nkernels), NULL);
    if (ret != CL_SUCCESS) {
        eprintf("Error quering number of kernels in program (%d)", ret);
        ret = MCL_ERR_INVPRG;
        goto err;
    }

    ret = clGetProgramInfo(obj->cl_prg, CL_PROGRAM_KERNEL_NAMES, 0, NULL, &(p->knames_size));
    if (ret != CL_SUCCESS) {
        eprintf("Error quering kernel names' length (%d)", ret);
        ret = MCL_ERR_INVPRG;
        goto err;
    }

    p->knames = (char *)malloc(p->knames_size);
    if (!(p->knames)) {
        eprintf("Error allocating memory to store kernel names");
        ret = MCL_ERR_INVPRG;
        goto err;
    }

    ret = clGetProgramInfo(obj->cl_prg, CL_PROGRAM_KERNEL_NAMES, p->knames_size, p->knames, NULL);
    if (ret != CL_SUCCESS) {
        eprintf("Error quering kernel names' length (%d)", ret);
        ret = MCL_ERR_INVPRG;
        goto err;
    }
    Dprintf("\t Found %lu kernels in program: %s (size %lu)", p->nkernels, p->knames, p->knames_size);

    char *ptr = strtok(p->knames, ";");
    while (ptr != NULL) {
        mcl_kernel *k;
        k = kerMap_add(ptr);
        if (k)
            kerMap_add_prg(k, p);
        else {
            ret = MCL_ERR_INVPRG;
            goto err;
        }
        ptr = strtok(NULL, ";");
    }

err:
    return ret;
}

int __set_kernel(mcl_handle *h, char *name, uint64_t nargs) {
    mcl_request *r = NULL;
    mcl_task *t = NULL;
    unsigned int ret = 0;

    r = rlist_search(&ptasks, &ptasks_lock, h->rid);
    if (!r) {
        ret = MCL_ERR_INVREQ;
        goto err;
    }

    t = req_getTask(r);

    t->kernel = kerMap_search(name);
    if (t->kernel == NULL) {
        eprintf("Kernel %s not found", name);
        ret = MCL_ERR_INVARG;
        goto err;
    }

    if (nargs) {
        t->args = (mcl_arg *)malloc(nargs * sizeof(mcl_arg));
        if (!(t->args)) {
            ret = MCL_ERR_MEMALLOC;
            goto err;
        }
    }
    t->nargs = nargs;
    memset((void *)(t->args), 0, nargs * sizeof(mcl_arg));

    return 0;

err:
    return -ret;
}

mcl_handle *__task_init(char *path, char *name, uint64_t nargs, char *opts, unsigned long flags) {
    mcl_handle *h = __task_create(0);

    if (!h)
        return NULL;

    if (__set_prg(path, opts, flags))
        return NULL;
    return h;
}

int __set_arg(mcl_handle *h, uint64_t id, void *addr, size_t size, off_t offset, uint64_t flags) {
    mcl_request *r = NULL;
    mcl_task *tsk = NULL;
    mcl_arg *a = NULL;

    r = rlist_search(&ptasks, &ptasks_lock, h->rid);
    if (!r)
        return -MCL_ERR_INVREQ;

    tsk = req_getTask(r);
    a = task_getArgAddr(tsk, id);

    if (flags & MCL_ARG_SCALAR) {
        a->addr = (void *)malloc(size);
        if (!(a->addr))
            return -MCL_ERR_MEMALLOC;

        memcpy(a->addr, addr, size);
    }
    else
        a->addr = addr;

    a->size = size;
    a->offset = offset;
    a->flags = flags;

    if (a->flags & MCL_ARG_SHARED) {
        a->flags |= MCL_ARG_BUFFER | MCL_ARG_RESIDENT;
    }

    tsk->mem += size;
    // Dprintf("  Added argument %"PRIu64" to task %"PRIu32" (mem=%"PRIu64")", id, h->rid, tsk->mem);

    return 0;
}

int __am_exec(mcl_handle *hdl, mcl_request *req, uint64_t flags) {
    mcl_task *t = req_getTask(req);
    struct mcl_msg_struct msg;
    int retcode = 0;
    mcl_arg *a;

    assert(hdl->status == MCL_REQ_ALLOCATED);
    msg_init(&msg);

    msg.cmd = MSG_CMD_EXE;
    msg.rid = hdl->rid;
    msg.pes = t->tpes;
    msg.type = flags & MCL_TASK_TYPE_MASK;
    msg.flags = (flags & MCL_TASK_FLAG_MASK) >> MCL_TASK_FLAG_SHIFT;
    msg.nres = 0;

    for (int i = 0; i < MCL_DEV_DIMS; i++) {
        msg.pesdata.pes[i] = t->pes[i];
        msg.pesdata.lpes[i] = t->lpes[i];
    }

    /*
     * It is most time efficient to allocate the greatest memory that could be needed, but likely it
     * is an overshoot
     */
    msg.resdata = malloc(t->nargs * sizeof(msg_arg_t));
    memset(msg.resdata, 0, t->nargs * sizeof(msg_arg_t));

    mcl_rdata *el;
    uint8_t swap_success;

    for (uint64_t i = 0; i < t->nargs; i++) {
        a = &(t->args[i]);
        if ((a->flags & MCL_ARG_BUFFER) && (a->flags & MCL_ARG_INVALID) && (el = rdata_get(a->addr, 0))) {
            mcl_msg free_msg;
            msg_init(&free_msg);
            free_msg.cmd = MSG_CMD_FREE;
            free_msg.nres = 1;
            free_msg.resdata = (msg_arg_t *)malloc(sizeof(msg_arg_t));
            memset(free_msg.resdata, 0, sizeof(msg_arg_t));
            free_msg.resdata[0].mem_id = el->id;
            free_msg.resdata[0].mem_size = el->size;
            if (cli_msg_send(&free_msg)) {
                eprintf("Error sending msg 0x%" PRIx64 ".", free_msg.cmd);
                msg_free(&msg);
                goto err;
            }
            msg_free(&free_msg);
            Dprintf("\t Argument %" PRIu64 " free message sent", i);

            rdata_del(el);
        }

        if ((a->flags & MCL_ARG_BUFFER) && (a->flags & MCL_ARG_RESIDENT)) {
            el = rdata_get(a->addr, 1);
            if (el) {
                msg.resdata[msg.nres].mem_id = el->id;
                msg.resdata[msg.nres].overall_size = (uint64_t)((el->size / MCL_MEM_PAGE_SIZE) + .5);
            }
            else {
                msg.resdata[msg.nres].mem_id = get_mem_id();
                el = rdata_add(a->addr, msg.resdata[msg.nres].mem_id, a->size, a->flags);
                msg.resdata[msg.nres].overall_size = (uint64_t)((el->size / MCL_MEM_PAGE_SIZE) + .5);
            }
            a->rdata_el = el;

            if (a->flags & MCL_ARG_DYNAMIC) {
                msg.resdata[msg.nres].flags = MSG_ARGFLAG_EXCLUSIVE;
            }

            if (a->flags & MCL_ARG_SHARED) {
                msg.resdata[msg.nres].flags |= MSG_ARGFLAG_SHARED;
                msg.resdata[msg.nres].pid = mcl_get_shared_mem_pid(a->addr);
            }
            msg.resdata[msg.nres].mem_size = (uint64_t)((a->size - 1) / MCL_MEM_PAGE_SIZE + 1.0);
            msg.resdata[msg.nres].mem_offset = a->offset / (uint64_t)MCL_MEM_PAGE_SIZE;
            msg.nres += 1;

            // The entire buffer has to be allocated at once to the task memory needs to
            // be adjusted for subbuffers
            t->mem += (el->size - a->size);
        }
    }
    msg.mem = ceil((t->mem * (1 + MCL_TASK_OVERHEAD)) / MCL_PAGE_SIZE);

    msg.ndependencies = t->ndependencies;
    for (uint64_t i = 0; i < t->ndependencies; i++) {
        msg.dependencies[i] = t->dependencies[i]->key;
    }

    // Dprintf("Sending EXE AM RID: %"PRIu64" PEs: %"PRIu64" MEM: %"PRIu64, msg.rid, msg.pes,
    //     msg.mem);

    if (!cas(&(hdl->status), MCL_REQ_ALLOCATED, MCL_REQ_PENDING)) {
        eprintf("Task %" PRIu32 " failed to execute with incorrect status: %" PRIu64 ".", hdl->rid, hdl->status);
        retcode = MCL_ERR_INVTSK;
        goto err;
    };
    stats_timestamp(hdl->stat_submit);

    while (__atomic_load_n(&(mcl_desc.out_msg), __ATOMIC_RELAXED) >= MCL_MSG_MAX)
        sched_yield();

    ainc(&(mcl_desc.out_msg));

    if (cli_msg_send(&msg)) {
        eprintf("Error sending msg 0x%" PRIx64, msg.cmd);
        retcode = MCL_ERR_SRVCOMM;
        goto err;
    }
    msg_free(&msg);

    return 0;

err:
    msg_free(&msg);
    swap_success = cas(&(hdl->status), MCL_REQ_ALLOCATED, MCL_REQ_COMPLETED);
    assert(swap_success);
    hdl->ret = MCL_RET_ERROR;
    return -retcode;
}

int __am_null(mcl_request *req) {
    struct mcl_msg_struct msg;
    mcl_handle *hdl = req_getHdl(req);

    assert(hdl->status == MCL_REQ_ALLOCATED);
    msg_init(&msg);

    msg.cmd = MSG_CMD_NULL;
    msg.rid = hdl->rid;
    msg.pes = 0x0;
    msg.type = 0x0;
    msg.mem = 0x0;
    msg.flags = 0x0;

    while (__atomic_load_n(&(mcl_desc.out_msg), __ATOMIC_RELAXED) >= MCL_MSG_MAX)
        sched_yield();

    ainc(&(mcl_desc.out_msg));

    if (cli_msg_send(&msg)) {
        eprintf("Error sending msg 0x%" PRIx64, msg.cmd);
        goto err;
    }

    // Doesn't really do anything, nor wait for the scheduler...

    uint8_t swap_success = cas(&(hdl->status), MCL_REQ_ALLOCATED, MCL_REQ_COMPLETED);
    assert(swap_success);
    hdl->ret = MCL_RET_SUCCESS;

    adec(&(mcl_desc.out_msg));

    adec(&mcl_desc.num_reqs);
    msg_free(&msg);

    return 0;

err:
    adec(&mcl_desc.num_reqs);
    free(req);
    msg_free(&msg);

    return -MCL_ERR_MEMALLOC;
}

int __internal_wait(mcl_handle *h, int blocking, uint64_t timeout) {
    struct timespec start, now;
    uint64_t tsk_status;

    /* This is s shortcut for the common case */
    if ((tsk_status = __atomic_load_n(&(h->status), __ATOMIC_SEQ_CST)) == MCL_REQ_COMPLETED)
        return 0;

    if (!blocking)
        return tsk_status;

    __get_time(&start);
    __get_time(&now);

    while ((tsk_status = __atomic_load_n(&(h->status), __ATOMIC_SEQ_CST)) != MCL_REQ_COMPLETED) {
        sched_yield();
        __get_time(&now);
    }

    if (tsk_status == MCL_REQ_COMPLETED) {
        Dprintf("Request %u completed.", h->rid);
        return 0;
    }

    eprintf("Timeout occurred, blocking for more than %f s!", __nstos(MCL_TIMEOUT));

    return -1;
}

int cli_register(void) {
    struct mcl_msg_struct msg;
    int ret;

    msg_init(&msg);
    msg.cmd = MSG_CMD_REG;
    msg.rid = get_rid();
    msg.threads = (2 + mcl_desc.workers);

    if (cli_msg_send(&msg)) {
        eprintf("Error sending msg 0x%" PRIx64, msg.cmd);
        goto err;
    }
    msg_free(&msg);

    Dprintf("Waiting for scheduler to accept registration request...");

    while ((ret = cli_msg_recv(&msg)) >= 0) {
        if (ret == 0)
            break;
        sched_yield();
    }

    if (msg.cmd == MSG_CMD_ERR) {
        eprintf("Client registration failed!");
        goto err;
    }

    mcl_desc.start_cpu = msg.res;
    Dprintf("Client registration confirmed.");
    msg_free(&msg);
    return 0;

err:
    msg_free(&msg);
    return -1;
}

int cli_deregister(void) {
    struct mcl_msg_struct msg;

    msg_init(&msg);
    msg.cmd = MSG_CMD_END;
    msg.rid = get_rid();

    if (cli_msg_send(&msg)) {
        eprintf("Error sending msg 0x%" PRIx64 ".", msg.cmd);
        goto err;
    }
    msg_free(&msg);

    // Wait for request to be accepted...

    return 0;

err:
    msg_free(&msg);
    return -1;
}

int cli_setup(void) {
    if ((shared_mem_name = getenv("MCL_SHM_NAME")) == NULL)
        shared_mem_name = MCL_SHM_NAME;

    Dprintf("Opening shared memory object (%s)...", shared_mem_name);
    mcl_desc.shm_fd = shm_open(shared_mem_name, O_RDONLY, 0);
    if (mcl_desc.shm_fd < 0) {
        eprintf("Error opening shared memory object %s.", shared_mem_name);
        perror("shm_open");
        goto err;
    }

    mcl_desc.info = (mcl_info_t *)mmap(NULL, MCL_SHM_SIZE, PROT_READ, MAP_SHARED,
                                       mcl_desc.shm_fd, 0);
    if (!mcl_desc.info) {
        eprintf("Error mapping shared memory object.");
        perror("mmap");
        close(mcl_desc.shm_fd);
        goto err;
    }

    Dprintf("Shared memory ojbect mapped at address %p", mcl_desc.info);
    close(mcl_desc.shm_fd);

    const char *socket_name;
    if ((socket_name = getenv("MCL_SOCK_NAME")) == NULL)
        socket_name = MCL_SOCK_NAME;

    memset(&mcl_desc.saddr, 0, sizeof(struct sockaddr_un));
    mcl_desc.saddr.sun_family = PF_UNIX;
    strncpy(mcl_desc.saddr.sun_path, socket_name, sizeof(mcl_desc.saddr.sun_path) - 1);

    mcl_desc.sock_fd = socket(PF_LOCAL, SOCK_DGRAM, 0);
    if (mcl_desc.sock_fd < 0) {
        eprintf("Error creating communication socket");
        perror("socket");
        goto err_shm_fd;
    }

    if (fcntl(mcl_desc.sock_fd, F_SETFL, O_NONBLOCK)) {
        eprintf("Error setting scheduler socket flags");
        perror("fcntl");
        goto err_shm_fd;
    }

    if (setsockopt(mcl_desc.sock_fd, SOL_SOCKET, SO_SNDBUF, &(mcl_desc.info->sndbuf),
                   sizeof(uint64_t))) {
        eprintf("Error setting scheduler sending buffer");
        perror("setsockopt");
        goto err_shm_fd;
    }

    if (setsockopt(mcl_desc.sock_fd, SOL_SOCKET, SO_RCVBUF, &(mcl_desc.info->rcvbuf),
                   sizeof(uint64_t))) {
        eprintf("Error setting scheduler receiving buffer");
        perror("setsockopt");
        goto err_shm_fd;
    }

#if _DEBUG
    uint64_t r = 0, s = 0;
    socklen_t len = sizeof(uint64_t);

    if (getsockopt(mcl_desc.sock_fd, SOL_SOCKET, SO_SNDBUF, &s, &len)) {
        eprintf("Error getting scheduler sended buffer");
        perror("getsockopt");
    }

    len = sizeof(uint64_t);

    if (getsockopt(mcl_desc.sock_fd, SOL_SOCKET, SO_RCVBUF, &r, &len)) {
        eprintf("Error getting scheduler receiving buffer");
        perror("getsockopt");
    }

    Dprintf("Sending buffer set to %" PRIu64 " - Receiving buffer set to %" PRIu64 " ", s, r);
#endif

    const char *client_format;
    if ((client_format = getenv("MCL_SOCK_CNAME")) == NULL)
        client_format = MCL_SOCK_CNAME;

    memset(&mcl_desc.caddr, 0, sizeof(struct sockaddr_un));
    mcl_desc.caddr.sun_family = PF_UNIX;
    snprintf(mcl_desc.caddr.sun_path, sizeof(mcl_desc.caddr.sun_path),
             client_format, (long)mcl_desc.pid);

    Dprintf("Client communication socket %s created", mcl_desc.caddr.sun_path);

    if (bind(mcl_desc.sock_fd, (struct sockaddr *)&mcl_desc.caddr,
             sizeof(struct sockaddr_un)) < 0) {
        eprintf("Error binding client communication socket %s.", mcl_desc.caddr.sun_path);
        perror("bind");
        goto err_socket;
    }
    Dprintf("Client communication socket bound to %s", mcl_desc.caddr.sun_path);

    if (resource_discover(&cl_plts, &mcl_plts, &mcl_class, NULL, NULL)) {
        eprintf("Error discovering computing elements.");
        goto err_socket;
    }

    mcl_res = (mcl_resource_t *)malloc(mcl_desc.info->ndevs * sizeof(mcl_resource_t));
    if (!mcl_res) {
        eprintf("Error allocating memory to map MCL resources.");
        goto err_socket;
    }

    if (resource_map(mcl_plts, mcl_desc.info->nplts, mcl_class, mcl_res)) {
        eprintf("Error mapping devices to MCL resources.");
        goto err_res;
    }

    if (resource_create_ctxt(mcl_res, mcl_desc.info->ndevs)) {
        eprintf("Error creating resource contexts.");
        goto err_res;
    }

    Dprintf("Discovered %" PRIu64 " platforms and a total of %" PRIu64 " devices",
            mcl_desc.info->nplts, mcl_desc.info->ndevs);

    if (req_init()) {
        eprintf("Error initializing request hash table");
        goto err_res;
    }

    if (rlist_init(&ptasks_lock)) {
        eprintf("Error initializing not active task list lock");
        goto err_res;
    }

    if (rlist_init(&ftasks_lock)) {
        eprintf("Error initializing pending task list lock");
        goto err_res;
    }

    if (rdata_init()) {
        eprintf("Error initializing resident data hash table");
        goto err_res;
    }

    mcl_shm_init();

    if (msg_setup(mcl_desc.info->ndevs)) {
        eprintf("Error setting up messages");
        goto err_rdata;
    }

    if (cli_register()) {
        eprintf("Error registering process %d.", mcl_desc.pid);
        goto err_rdata;
    }

    return 0;

err_rdata:
    mcl_shm_free();
    rdata_free();
err_res:
    free(mcl_res);
err_socket:
    close(mcl_desc.sock_fd);
    unlink(mcl_desc.caddr.sun_path);
err_shm_fd:
    munmap(mcl_desc.info, MCL_SHM_SIZE);
err:
    return -1;
}

static inline int cli_shutdown_dev(void) {
#ifdef _STATS
    mcl_device_t *dev;
    int i;
#endif
    Dprintf("Shutting down devices...");
#ifdef _STATS
    for (i = 0; i < mcl_desc.info->ndevs; i++) {
        dev = mcl_res[i].dev;
        stprintf("Resource[%d] Task Executed=%" PRIu64 " Successful=%" PRIu64 " Failed=%" PRIu64 "",
                 i, dev->task_executed, dev->task_successful, dev->task_failed);
    }
#endif
    return 0;
}

int cli_shutdown(void) {
    Dprintf("MCL shutting down.");
    mcl_shm_free();
    rdata_free();

    if (cli_shutdown_dev()) {
        eprintf("Error shutting down devices...");
        goto err;
    }

    if (cli_deregister())
        eprintf("Error de-registering process %d", mcl_desc.pid);

    close(mcl_desc.sock_fd);
    unlink(mcl_desc.caddr.sun_path);

    for (uint64_t i = 0; i < mcl_desc.info->nplts; i++) {
        for (uint64_t j = 0; j < mcl_plts[i].ndev; j++) {
            clReleaseDevice(mcl_plts[i].cl_dev[j]);
            clReleaseContext(mcl_plts[i].devs[j].cl_ctxt);
            for (uint64_t k = 0; k < mcl_plts[i].devs[j].nqueues; k++) {
                clReleaseCommandQueue(mcl_plts[i].devs[j].cl_queue[k]);
            }
            free(mcl_plts[i].devs[j].wisize);
        }
        free(mcl_plts[i].cl_dev);
        free(mcl_plts[i].devs);
    }

    if (munmap(mcl_desc.info, MCL_SHM_SIZE)) {
        eprintf("Error unmapping Minos scheduler shared memory.");
        goto err;
    }
    Dprintf("Shared memory object unmapped.");

    if (req_count(&hash_reqs))
        iprintf("There are still pending requests in the request hash table");

    prgMap_finit();
    req_clear(&hash_reqs);

    if (mcl_desc.out_msg)
        iprintf("There are still pending messages");

    free(mcl_class);
    free(mcl_res);
    free(mcl_plts);
    free(cl_plts);
    return 0;

err:
    return -1;
}

static inline int __task_setup_buffers(mcl_request *r) {
    mcl_arg *a;
    size_t arg_size;
    void *arg_value;
    mcl_task *t = req_getTask(r);
    mcl_context *context = task_getCtxAddr(t);
    cl_context ctxt = res_getClCtx(r->res);
    cl_command_queue queue = context->queue;
    uint64_t flags = 0u;
    int i, ret, retcode = 0;

#ifdef _DEBUG
    struct timespec start, end;
#endif

    stats_timestamp(r->hdl->stat_input);
    /*
     * Allocate buffers for input + output
     */
    context->buffers = (cl_mem *)malloc(sizeof(cl_mem) * t->nargs);
    memset(context->buffers, 0, sizeof(cl_mem) * t->nargs);

    if (!context->buffers) {
        eprintf("Error allocating OpenCL object buffers.");
        return -MCL_ERR_MEMALLOC;
    }

    for (i = 0; i < t->nargs; i++) {
        a = &(t->args[i]);
        a->new_buffer = 1;
        a->moved_data = 0;

        Dprintf("  Setting up arg %d addr %p size %lu flags 0x%" PRIx64 "", i, a->addr, a->size, a->flags);

        if ((a->flags & MCL_ARG_BUFFER) && !(a->flags & MCL_ARG_LOCAL)) {

            if (a->flags & MCL_ARG_RESIDENT) {
                /* we might need to check that flags are the same... */
                context->buffers[i] = rdata_get_mem(a->rdata_el, r->res, a->size, a->offset, a->flags, queue, &ret);

                if (!context->buffers[i]) {
                    eprintf("Error with resident memory at task: %" PRIu32 ", argument %d, address %p", r->key, i, a->addr);
                    goto err;
                }
            }
            else {
                Dprintf("\t Creating buffer...");
#ifdef _DEBUG
                __get_time(&start);
#endif
                flags = arg_flags_to_cl_flags(a->flags);
                context->buffers[i] = clCreateBuffer(ctxt, flags, a->size, NULL, &ret);
                if (ret != CL_SUCCESS) {
                    eprintf("Error creating input OpenCL buffer %d (%d).", i, ret);
                    retcode = MCL_ERR_MEMALLOC;
                    goto err;
                }
                if ((a->flags & MCL_ARG_INPUT)) {
                    VDprintf("    Writing data to buffer...");
                    ret = clEnqueueWriteBuffer(queue, context->buffers[i], CL_FALSE, 0,
                                               a->size, a->addr + a->offset, 0, NULL, 0);
                    stats_add(r->worker->bytes_transfered, (a->size));
                    stats_inc(r->worker->n_transfers);
                }
                else if ((a->flags & MCL_ARG_OUTPUT)) {
                    /* Output only area: memset to 0
                     * NOTE: this could probably be done faster by using
                     *       pattern_size greater than 1, it would require proper
                     *       computations to handle sizes that are not multiple of
                     *       pattern size.
                     */
                    uint32_t __zero_pattern = 0u;
                    Dprintf("\t\t Zeroing the buffer...");
                    ret = clEnqueueFillBuffer(queue, context->buffers[i], &__zero_pattern,
                                              1, 0, a->size, 0, NULL, NULL);
                }

                if (ret != CL_SUCCESS) {
                    eprintf("Error for arg %d, OpenCL device %d.", i, ret);
                    retcode = MCL_ERR_MEMCOPY;
                    goto err1;
                }
#ifdef _DEBUG
                __get_time(&end);
#endif
                Dprintf("  Time to create buffer: %lld", __diff_time_ns(end, start));
            }
            arg_size = sizeof(cl_mem);
            arg_value = (void *)&(context->buffers[i]);
        }
        else if (a->flags & MCL_ARG_LOCAL) {
            if (a->addr != NULL) {
                eprintf("Error, argument address must be NULL for local memory");
                retcode = MCL_ERR_INVARG;
                goto err1;
            }
            context->buffers[i] = NULL;
            arg_size = a->size;
            arg_value = (void *)NULL;
        }
        else if (a->flags & MCL_ARG_SCALAR) {
            context->buffers[i] = NULL;
            arg_size = a->size;
            arg_value = (void *)a->addr;
        }
        else {
            context->buffers[i] = NULL;
            eprintf("Type of argument not recognized (flags = 0x%" PRIx64 ")!",
                    a->flags);
            retcode = MCL_ERR_INVARG;
            goto err1;
        }

        if (r->hdl->cmd == MSG_CMD_TRAN)
            continue;

        Dprintf("\t Setting up kernel argument...");
        Dprintf("\t i: %d, arg_size: %lu, arg_value: %p", i, arg_size, arg_value);
        ret = clSetKernelArg(context->kernel, i, arg_size, arg_value);

        if (ret != CL_SUCCESS) {
            eprintf("Error setting OpenCL argument %d (%d).", i, ret);
            retcode = MCL_ERR_INVARG;
            goto err;
        }
    }
    return 0;
err1:
    /** Rollback buffer allocations, FIXME: this needs to check for resident data **/
    while (i)
        clReleaseMemObject(context->buffers[i--]); /** TODO: Fix for shared buffer **/
err:
    free(context->buffers);
    context->buffers = NULL;
    return -retcode;
}

static inline int __save_program(mcl_pobj *obj, cl_program prg) {
    cl_int err;

    Dprintf("  Saving program...");

    err = clGetProgramInfo(prg, CL_PROGRAM_BINARY_SIZES,
                           sizeof(size_t),
                           &(obj->binary_len), NULL);

    if (err != CL_SUCCESS) {
        eprintf("Error quiering program binary size (%d)", err);
        return -1;
    }

    Dprintf("    Binary size %lu", obj->binary_len);
    obj->binary = (unsigned char *)malloc(sizeof(unsigned char) * (obj->binary_len));
    err = clGetProgramInfo(prg, CL_PROGRAM_BINARIES, sizeof(char *),
                           &(obj->binary), NULL);

    if (err != CL_SUCCESS) {
        eprintf("Error quiering program binary (%d)", err);
        return -1;
    }

    obj->status = MCL_KER_BUILT;

    return 0;
}

static inline int __task_setup(mcl_request *r) {
    cl_int ret;
    mcl_task *t = req_getTask(r);
    mcl_kernel *k = task_getKernel(t);
    mcl_program *p;
    mcl_context *ctx = task_getCtxAddr(t);
    int retcode = 0;
    size_t kwgroup;
    mcl_pobj *obj;

    stats_timestamp(r->hdl->stat_setup);

    if (r->hdl->cmd != MSG_CMD_TRAN) {
        p = kerMap_get_prg(k, r->res);
        if (!p) {
            retcode = MCL_ERR_INVPRG;
            goto err;
        }

        obj = __build_program(p, r->res);
        if (!obj) {
            retcode = MCL_ERR_INVPRG;
            goto err;
        }
        ctx->prg = obj->cl_prg;

        Dprintf("\t Creating kernel %s...", t->kernel->name);
        ctx->kernel = clCreateKernel(ctx->prg, (const char *)(t->kernel->name), &ret);
        if (ret != CL_SUCCESS) {
            eprintf("Error creating kernel %s (ret = %d)",
                    t->kernel->name, ret);
            goto err;
        }

        /*
         * Determine workgroup size automatically if the user hasn't explicitely set
         * them (lpes[0] = lpes[1] = lpes[2] = 0
         */
        ret = clGetKernelWorkGroupInfo(ctx->kernel, mcl_res[r->res].dev->cl_dev,
                                       CL_KERNEL_WORK_GROUP_SIZE,
                                       sizeof(size_t), &kwgroup, NULL);
        if (ret != CL_SUCCESS) {
            eprintf("Error querying the kernel work group size! (%d)\n", ret);
            retcode = MCL_ERR_INVTSK;
            goto err;
        }
        if (t->lpes[0] * t->lpes[1] * t->lpes[2] > kwgroup) {
            eprintf("Provided local size too large for device %" PRIu64 " [%" PRIu64 ",%" PRIu64 ",%" PRIu64 "] > %lu",
                    r->res, t->lpes[0], t->lpes[1], t->lpes[2], kwgroup);
            retcode = MCL_ERR_INVTSK;
            goto err;
        }
    }

    Dprintf("Preparting input and output arguments (%" PRIu64
            " arguments)...",
            t->nargs);

    retcode = __task_setup_buffers(r);
    if (retcode < 0) {
        retcode = -retcode;
        goto err;
    }

    return 0;

err:
    return -retcode;
}

static inline int __task_release(mcl_request *r) {
    mcl_task *t = req_getTask(r);
    mcl_context *ctx = task_getCtxAddr(t);
    int i;
    int ret = 0;
    Dprintf("Task %u Removing %" PRIu64 " arguemnts", req_getHdl(r)->rid, t->nargs);

    for (i = 0; i < t->nargs; i++) {
        Dprintf("\t Removing argument %d: ADDR: %p SIZE: %lu FLAGS: 0x%" PRIx64 " ", i,
                t->args[i].addr, t->args[i].size, t->args[i].flags);

        if (t->args[i].flags & MCL_ARG_SCALAR) {
            free(t->args[i].addr);
            Dprintf("\t\t Scalar arguement removed");
        }
        else if ((t->args[i].flags & MCL_ARG_RESIDENT)) {
            rdata_put(t->args[i].rdata_el);
            rdata_put_mem(t->args[i].rdata_el, t->args[i].offset);
            if (t->args[i].flags & MCL_ARG_OUTPUT) {
                rdata_remove_subbuffers(t->args[i].rdata_el, t->args[i].size, t->args[i].offset);
                if (t->args[i].flags & MCL_ARG_SHARED) {
                    msync(t->args[i].addr, t->args[i].size, MS_INVALIDATE);
                }
            }

            if (t->args[i].flags & MCL_ARG_DONE) {
                mcl_msg free_msg;
                msg_init(&free_msg);
                free_msg.cmd = MSG_CMD_FREE;
                free_msg.nres = 1;
                free_msg.resdata = (msg_arg_t *)malloc(sizeof(msg_arg_t));
                memset(free_msg.resdata, 0, sizeof(msg_arg_t));
                free_msg.resdata[0].mem_id = t->args[i].rdata_el->id;
                free_msg.resdata[0].mem_size = t->args[i].size;
                if (cli_msg_send(&free_msg)) {
                    eprintf("Error sending msg 0x%" PRIx64 ".", free_msg.cmd);
                    msg_free(&free_msg);
                    ret = MCL_ERR_SRVCOMM;
                    continue;
                }
                msg_free(&free_msg);

                Dprintf("\t Argument %d free message sent", i);
                rdata_del(t->args[i].rdata_el);
            }
        }
        else {
            clReleaseMemObject(ctx->buffers[i]);
        }
    }

    if (r->hdl->cmd != MSG_CMD_TRAN)
        clReleaseKernel(ctx->kernel);

    // clReleaseEvent(ctx->event);
    free(t->args);
    // free(t);

    return ret;
}

static inline int cli_evict_am(struct worker_struct *desc, mcl_msg *msg) {
    uint32_t mem_id;
    int ret;

    mem_id = msg->resdata[0].mem_id;
    ret = rdata_release_mem_by_id(mem_id, msg->res);

    /** TODO: Program scheduler to check result of this command **/
    if (ret) {
        eprintf("Error executing evict command.");
        return -1;
    }

    return 0;
}

static inline int create_waitlist(mcl_task *t, uint64_t res, int *nwait, cl_event *waitlist) {
    // cl_context ctx = res_getClCtx(res);
    // cl_int err;
    uint64_t waitlist_idx = 0;
    for (uint32_t i = 0; i < t->ndependencies; i++) {
        mcl_request *r = t->dependencies[i];
        mcl_task *dep_task = t->dependencies[i]->tsk;
        if (__atomic_load_n(&(r->hdl->status), __ATOMIC_SEQ_CST) == MCL_REQ_EXECUTING && r->res == res) {
            waitlist[waitlist_idx++] = dep_task->ctx.event;
        }
        else {
            uint32_t dep_idx = ainc(&dep_task->ndependents);

            // Temporary solution to pause thread
            // eprintf("Trying to enqueue dependency from another device for handle %s depending on %s, res: %"PRIu64", dep res: %"PRIu64"", t->kname, dep_task->kname, res, r->res);
            while (__atomic_load_n(&(dep_task->dependent_events[dep_idx]), __ATOMIC_SEQ_CST) == NULL) {
                sched_yield();
            }

            // waitlist[waitlist_idx] = clCreateUserEvent(ctx, &err);
            // if (cas(&(dep_task->dependent_events[dep_idx]), NULL, waitlist[waitlist_idx]))
            //{
            //     waitlist_idx += 1;
            // }
            // else
            //{
            //     // The task must have finished, so we don't need to wait anymore
            //     clReleaseEvent(waitlist[waitlist_idx]);
            // }
        }
    }
    *nwait = waitlist_idx;
    return 0;
}

static inline int cli_exec_am(struct worker_struct *desc, struct mcl_msg_struct *msg) {
    mcl_request *r = NULL;
    mcl_handle *h = NULL;
    mcl_task *t = NULL;
    mcl_context *ctx;
    mcl_msg ack;
    cl_int ret = CL_SUCCESS;
    int i;
    cl_command_queue queue;
    uint8_t swap_success = 0;
    int retcode = 0;

    adec(&(mcl_desc.out_msg));
    msg_init(&ack);

    r = req_search(&hash_reqs, msg->rid);
    if (!r) {
        retcode = MCL_ERR_INVREQ;
        goto err;
    }

    h = req_getHdl(r);
    t = req_getTask(r);
    ctx = task_getCtxAddr(t);
    r->res = msg->res;
    ctx->queue = __get_queue(r->res);
    queue = ctx->queue;
    r->worker = desc;

    Dprintf("Worker %" PRIu64 " executing request %u", desc->id, h->rid);

#ifdef _DEBUG
    for (i = 0; i < t->nargs; i++)
        Dprintf("  Argument addr %p size %lu flags 0x%" PRIx64 "",
                t->args[i].addr, t->args[i].size, t->args[i].flags);
#endif

    swap_success = cas(&(h->status), MCL_REQ_PENDING, MCL_REQ_INPROGRESS);
    if (!swap_success) {
        eprintf("Request (RID=%u), status incorrect. Status: %" PRIu64 "!", h->rid, h->status);
        goto err_req;
    }

    if (__task_setup(r)) {
        eprintf("Error setting up task for execution (RID=%u)!", h->rid);
        retcode = MCL_ERR_INVTSK;
        goto err_req;
    }

    if (h->cmd != MSG_CMD_TRAN) {
        Dprintf("Executing Task RID=%u RES=%" PRIu64 " DIMS=%u "
                "PEs=[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "] "
                "LPEs=[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]",
                h->rid, r->res, t->dims,
                t->pes[0], t->pes[1], t->pes[2],
                t->lpes[0], t->lpes[1], t->lpes[2]);
    }

    int nwait = 0;
    cl_event waitlist[MCL_MAX_DEPENDENCIES];
    create_waitlist(t, r->res, &nwait, waitlist);

    stats_timestamp(h->stat_exec_start);

    cl_event kernel_event;
    if (h->cmd != MSG_CMD_TRAN) {
        ret = clEnqueueNDRangeKernel(queue, ctx->kernel, t->dims, t->offsets,
                                     (size_t *)t->pes, t->lpes[0] == 0 ? NULL : (size_t *)t->lpes,
                                     nwait, nwait ? waitlist : NULL, &kernel_event);
    }
    else {
        // Wait for all enqueued events (write events)
        ret = clEnqueueMarkerWithWaitList(queue, 0, NULL, &kernel_event);
    }

    if (ret != CL_SUCCESS) {
        retcode = MCL_ERR_EXEC;
        eprintf("Error executing task %u (%d)", h->rid, ret);
        if (h->cmd == MSG_CMD_TRAN)
            goto err_setup;
        goto err_kernel;
    }

    int buffer_num = 0;
    cl_event *read_events = malloc(sizeof(cl_event) * t->nargs);
    for (i = 0; i < t->nargs; i++) {
#ifdef MCL_USE_POCL_SHARED_MEM
        if (t->args[i].flags & MCL_ARG_OUTPUT)
#else
        if ((t->args[i].flags & MCL_ARG_OUTPUT) ||
            ((t->args[i].flags & MCL_ARG_SHARED) && (t->args[i].flags & MCL_ARG_DYNAMIC)))
#endif
        {
            Dprintf("\tEnqueueing read buffer for argument %d.", i);
            // POCL 1.8 wont let us do t->args[i].addr + t->args[i].offset to point to the proper location in host memory,
            // so unforunately we need to copy the current data int->args[i].addr[0..t->args[i].size] to a temp buffer,
            // do the cl read, copy that new data to the appropriate offset, then copy the original data back.
            uint8_t *temp = NULL;
            if (t->args[i].offset != 0) {
                temp = malloc(t->args[i].size);
                if (temp == NULL){
                    retcode = MCL_ERR_MEMCOPY;
                    goto err_kernel;
                }
                memcpy(temp,t->args[i].addr,t->args[i].size);
            }
            ret = clEnqueueReadBuffer(queue, ctx->buffers[i],
                                      CL_FALSE, 0, t->args[i].size,
                                      t->args[i].addr + t->args[i].offset, 0,
                                      NULL, &read_events[buffer_num++]);
            if (ret != CL_SUCCESS) {
                retcode = MCL_ERR_MEMCOPY;
                if (h->cmd == MSG_CMD_TRAN)
                    goto err_setup;
                goto err_kernel;
            }
            if (t->args[i].offset != 0) {
                memcpy(t->args[i].addr + t->args[i].offset,t->args[i].addr,t->args[i].size);
                memcpy(t->args[i].addr, temp,t->args[i].size);
                if (temp!=NULL){
                 free(temp);
                }
            }
            stats_add(r->worker->bytes_transfered, (t->args[i].size));
            stats_inc(r->worker->n_transfers);
        }
    }
    if (buffer_num >= 1) {
        ctx->event = read_events[buffer_num - 1];
    }
    else {
        ctx->event = kernel_event;
    }

    if (!cas(&(h->status), MCL_REQ_INPROGRESS, MCL_REQ_EXECUTING)) {
        eprintf("Incorrect status for request %" PRIu32 ": %" PRIu64 "", h->rid, h->status);
        assert(0);
    }

    ret = clSetEventCallback(ctx->event, CL_COMPLETE, __task_complete, r);
    if (ret != CL_SUCCESS) {
        retcode = MCL_ERR_EXEC;
        if (h->cmd == MSG_CMD_TRAN)
            goto err_setup;
        goto err_kernel;
    }

    free(read_events);
    assert(rlist_add(&ftasks, &ftasks_lock, r) == 0);
    ainc(&(desc->ntasks));
    Dprintf("Added task %u to queue (total: %ld, ret: %d)", h->rid, desc->ntasks, ret);

#ifdef _STATS
    if (desc->ntasks > desc->max_tasks)
        desc->max_tasks = desc->ntasks;
#endif

    return 0;

err_kernel:
    clReleaseKernel(ctx->kernel);
    clReleaseProgram(ctx->prg);

err_setup: /** TODO: check this **/
    for (i = 0; i < (t->nargs); i++)
        if (ctx->buffers[i] != NULL)
            clReleaseMemObject(ctx->buffers[i]);

err_req:
    adec(&mcl_desc.num_reqs);
    cas(&(h->status), MCL_REQ_INPROGRESS, MCL_REQ_COMPLETED);
    h->ret = MCL_RET_ERROR;
    stats_inc(mcl_res[r->res].dev->task_failed);
    free(t->args);
err:
    ack.cmd = MSG_CMD_ERR;
    ack.rid = msg->rid;
    cli_msg_send(&ack);
    msg_free(&ack);

    return -retcode;
}

void CL_CALLBACK __task_complete(cl_event e, cl_int s, void *v_request) {
    mcl_request *r = (mcl_request *)v_request;
    mcl_task *tsk = r->tsk;

    // Notify waiting kernels on other devices
    cl_event dep_event, sentinel = (cl_event)(-1);
    for (uint32_t i = 0; i < MCL_MAX_DEPENDENCIES; i++) {
        __atomic_exchange(&(tsk->dependent_events[i]), &sentinel, &dep_event, __ATOMIC_SEQ_CST);
        // if (dep_event)
        //     clSetUserEventStatus(dep_event, CL_COMPLETE);
    }
}

static inline int __check_task(mcl_request *r) {
    mcl_handle *h = req_getHdl(r);
    mcl_task *t = req_getTask(r);
    mcl_context *ctx = task_getCtxAddr(t);
    cl_command_queue queue = ctx->queue;
    mcl_msg ack;
    int retcode = 0;
    cl_int eventStatus = CL_SUBMITTED;
    int attempts = MCL_TASK_CHECK_ATTEMPTS;

    while (eventStatus != CL_COMPLETE && attempts) {
        sched_yield();

        clFlush(queue);
        retcode = clGetEventInfo(ctx->event, CL_EVENT_COMMAND_EXECUTION_STATUS,
                                 sizeof(cl_int), &eventStatus, NULL);

        if (retcode != CL_SUCCESS) {
            retcode = MCL_ERR_EXEC;
            return retcode;
        }
        attempts--;
    }

    if (eventStatus != CL_COMPLETE)
        return 1;

    uint8_t swap_success = cas(&(h->status), MCL_REQ_EXECUTING, MCL_REQ_FINISHING);
    assert(swap_success);

#if defined(_STATS) && defined(_STATS_DETAILS)
    /** FIXME: Does not give accurate timings (maybe should be just be removed for the release) **/
    /*mcl_task*        t       = req_getTask(r);
      mcl_context*     ctx     = task_getCtxAddr(t);
      cl_ulong q_time, e_time;
      clGetEventProfilingInfo(ctx->exec_event, CL_PROFILING_COMMAND_QUEUED, sizeof(cl_ulong), &q_time, NULL);
      clGetEventProfilingInfo(ctx->exec_event, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &e_time, NULL);
      r->hdl->stat_true_runtime = e_time - q_time;*/
#endif

    stats_inc(res_getDev(r->res)->task_executed);
    __task_release(r);

    msg_init(&ack);
    ack.cmd = MSG_CMD_DONE;
    ack.rid = h->rid;
    if (cli_msg_send(&ack))
        retcode = MCL_ERR_SRVCOMM;
    msg_free(&ack);

    h->ret = retcode ? MCL_RET_ERROR : MCL_RET_SUCCESS;

    stats_timestamp(h->stat_end);

#if defined(_STATS) && defined(_STATS_DETAILS)

    stprintf("Request %" PRIu32 " %lld.%.9ld %lld.%.9ld %lld.%.9ld %lld.%.9ld %lld.%.9ld %lld.%.9ld %lld",
             h->rid,
             (long long)h->stat_submit.tv_sec, h->stat_submit.tv_nsec,
             (long long)h->stat_setup.tv_sec, h->stat_setup.tv_nsec,
             (long long)h->stat_input.tv_sec, h->stat_input.tv_nsec,
             (long long)h->stat_exec_start.tv_sec, h->stat_exec_start.tv_nsec,
             (long long)h->stat_exec_end.tv_sec, h->stat_exec_end.tv_nsec,
             (long long)h->stat_end.tv_sec, h->stat_end.tv_nsec,
             (long long)h->stat_true_runtime);
#endif

    if (!retcode)
        stats_inc((res_getDev(r->res)->task_successful));

    adec(&r->worker->ntasks);
    swap_success = cas(&(h->status), MCL_REQ_FINISHING, MCL_REQ_COMPLETED);
    assert(swap_success);
    adec(&mcl_desc.num_reqs);

    return -retcode;
}

void *check_pending(void *data) {
    mcl_rlist *li;
    mcl_request *r;

    int ret;

#ifndef __APPLE__
    if (mcl_desc.flags & MCL_SET_BIND_WORKERS) {
        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        int core_id = (1 + mcl_desc.start_cpu) % num_cores;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        Dprintf("\t Binding clean up thread to CPU: %d", core_id);

        pthread_t current_thread = pthread_self();
        pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    }
#endif

    Dprintf("\t Cleanup thread started...");
    pthread_barrier_wait(&mcl_desc.wt_barrier);
    while (__atomic_load_n(&status, __ATOMIC_SEQ_CST) != MCL_DONE || rlist_count(&ftasks, &ftasks_lock) > 0) {
        li = NULL;
        r = NULL;
        li = rlist_pop(&ftasks, &ftasks_lock);

        if (li == NULL)
            continue;

        r = li->req;
        ret = __check_task(r);
        if (ret > 0) {
            rlist_append(&ftasks, &ftasks_lock, li);
        }
        else if (ret < 0) {
            eprintf("Error checking task %u status.", r->key);
            free(li);
        }
        else {
            free(li);
        }
    }
    Dprintf("Cleanup thread terminated.");

    pthread_exit(NULL);
}
/*
 * Woker threads process incoming messages and pending tasks. Processing incoming
 * messages has higher priority becuase each message may spawn new tasks. If there
 * are no incoming messages, workers check for all pending tasks. Since the list
 * of pending tasks may be large, we want to make sure that there are no incoming
 * messages waiting to be processed.
 */
void *worker(void *data) {
    struct worker_struct *desc = (struct worker_struct *)data;
    int ret;
    struct mcl_msg_struct msg;

    Dprintf("\t Starting worker thread %" PRIu64 " (ntasks=%lu)",
            desc->id, desc->ntasks);
    desc->ntasks = 0;

#ifndef __APPLE__
    if (mcl_desc.flags & MCL_SET_BIND_WORKERS) {
        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        int core_id = (desc->id + 2 + mcl_desc.start_cpu) % num_cores;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        Dprintf("\t Binding worker thread to CPU: %d", core_id);

        pthread_t current_thread = pthread_self();
        pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    }
#endif

#ifdef _STATS
    desc->max_tasks = 0;
    desc->n_transfers = 0;
    desc->bytes_transfered = 0;
#endif
    pthread_barrier_wait(&mcl_desc.wt_barrier);

    while (__atomic_load_n(&(status), __ATOMIC_RELAXED) != MCL_DONE) {
        sched_yield();
        ret = cli_msg_recv(&msg);

        if (ret == -1) {
            eprintf("Worker %" PRIu64 ": Error receiving message.",
                    desc->id);
            pthread_exit(NULL);
        }

        if (ret == 0) {
            Dprintf("\t Worker %" PRIu64 " received message 0x%" PRIx64
                    " for request %" PRIu64,
                    desc->id, msg.cmd,
                    msg.rid);
#ifdef _STATS
            desc->nreqs++;
            stats_inc(mcl_desc.nreqs);
#endif
            switch (msg.cmd) {
            case MSG_CMD_FREE:
                ret = cli_evict_am(desc, &msg);
                break;
            case MSG_CMD_ACK:
                ret = cli_exec_am(desc, &msg);
                if (ret)
                    eprintf("Error executing AM %" PRIu64 " (%d).", msg.cmd, ret);
                break;
            default:
                break;
            }
            msg_free(&msg);
        }
    }

    Dprintf("\t Worker thread %" PRIu64 " terminated", desc->id);
#ifdef _STATS
    stprintf("W[%" PRIu64 "] NREQS: %" PRIu64 " Pending Tasks: %lu, Max Tasks: %lu",
             desc->id, desc->nreqs, desc->ntasks, desc->max_tasks);
    stprintf("    transfers: %" PRIu64 " bytes transfered: %" PRIu64 "",
             desc->n_transfers, desc->bytes_transfered);
#endif
    pthread_exit(NULL);
}
