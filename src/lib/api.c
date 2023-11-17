#define _GNU_SOURCE
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/mman.h>

#include <minos.h>
#include <minos_internal.h>
#include <stats.h>

extern volatile uint64_t status;
extern mcl_request *hash_reqs;
extern mcl_desc_t mcl_desc;
extern mcl_resource_t *mcl_res;
extern mcl_rlist *ptasks;
extern pthread_rwlock_t ptasks_lock;

int mcl_test(mcl_handle *h)
{
    if (!h)
    {
        eprintf("Invalid argument");
        return -1;
    }
    return __internal_wait(h, 0, MCL_TIMEOUT);
}

int mcl_wait(mcl_handle *h)
{
    if (!h)
    {
        eprintf("Invalid argument");
        return -1;
    }

    // Dprintf("Waiting for request %u to complete...", h->rid);

    return __internal_wait(h, 1, MCL_TIMEOUT);
}

int mcl_wait_all(void)
{
    while (__atomic_load_n(&(mcl_desc.num_reqs), __ATOMIC_RELAXED))
        sched_yield();

    return 0;
}

int mcl_task_set_arg(mcl_handle *h, uint64_t id, void *addr, size_t size, uint64_t flags)
{
    if (!h || !size || !flags)
    {
        eprintf("Invalid argument");
        return -MCL_ERR_INVARG;
    }

    /*Dprintf("Setting argument of task %u (addr: %p size: %lu flags: 0x%"PRIx64")",
        h->rid, addr, size, flags);*/

    return __set_arg(h, id, addr, size, 0, flags);
}

int mcl_null(mcl_handle *hdl)
{
    mcl_request *req = NULL;

    Dprintf("Submitting NULL AM");
    if (!hdl)
        return -MCL_ERR_INVARG;

    req = rlist_remove(&ptasks, &ptasks_lock, hdl->rid);
    if (req == NULL)
    {
        eprintf("Error looking for task %u", hdl->rid);
        return MCL_ERR_INVTSK;
    }

    if (req_add(&hash_reqs, hdl->rid, req))
    {
        eprintf("Error adding request %u to pending request table",
                hdl->rid);
        return MCL_ERR_INVREQ;
    }
    ainc(&mcl_desc.num_reqs);
    Dprintf("Executing NULL command");
    return __am_null(req);
}

mcl_handle *mcl_task_create(void)
{
    // Dprintf("Creating a new task...");
    return __task_create(0);
}

mcl_handle *mcl_task_init(char *path, char *name, uint64_t nargs, char *copts, unsigned long flags)
{
    if (!path || !name)
        return NULL;

    // Dprintf("Creating and initializing a new task for kernel %s in %s (nargs %"PRIu64" options %s flags %lu)",
    //	path, name, nargs, copts, flags);

    return __task_init(path, name, nargs, copts, flags);
}

int mcl_prg_load(char *path, char *copts, unsigned long flags)
{
    if (!path)
    {
        eprintf("Invalid path argument");
        return -MCL_ERR_INVARG;
    }

    if (!(flags & MCL_PRG_MASK))
    {
        eprintf("Invalid type of program 0x%lx)", flags);
        return -MCL_ERR_INVARG;
    }

    Dprintf("Loading program %s with options %s and flags 0x%lx", path, copts, flags);

    return __set_prg(path, copts, flags);
}

int mcl_task_set_kernel(mcl_handle *h, char *name, uint64_t nargs)
{
    if (!h || !name)
    {
        eprintf("Invalid argument for task set kernel");
        return -MCL_ERR_INVARG;
    }

    Dprintf("Setting task %u to use kernel %s (nargs %" PRIu64 ")",
            h->rid, name, nargs);

    return __set_kernel(h, name, nargs);
}

int mcl_hdl_free(mcl_handle *h)
{
    mcl_request *req;

    Dprintf("Removing handle %u...", h->rid);
    if (!h)
    {
        eprintf("Invalid argument");
        return -MCL_ERR_INVARG;
    }

    // If the handle has been sent to execute, wait before we free it
    if (!cas(&h->status, MCL_REQ_ALLOCATED, MCL_REQ_COMPLETED))
    {
        mcl_wait(h);
    }

    req = req_del(&hash_reqs, h->rid);
    if (req)
    {
        free(req->tsk);
        free(req);
        stats_dec(mcl_desc.nreqs);
    }

    if (h->flags & MCL_HDL_SHARED)
    {
        mcl_free_shared_hdl(h);
    }
    else
    {
        free(h);
    }

    return 0;
}

uint32_t mcl_get_ndev(void)
{
    return mcl_desc.info->ndevs;
}

int mcl_get_dev(uint32_t id, mcl_dev_info *dev)
{
    if (!dev || id > mcl_desc.info->ndevs)
    {
        eprintf("Invalid argument");
        return -1;
    }

    dev->id = id;
    strcpy(dev->name, mcl_res[id].dev->name);
    strcpy(dev->vendor, mcl_res[id].plt->vendor);
    dev->type = mcl_res[id].dev->type;
    dev->status = mcl_res[id].status;
    dev->mem_size = mcl_res[id].dev->mem_size;
    dev->ndims = mcl_res[id].dev->ndims;
    dev->wgsize = mcl_res[id].dev->wgsize;
    dev->pes = mcl_res[id].dev->pes;
    dev->wisize = (size_t *)malloc(sizeof(size_t) * dev->ndims);

    if (!dev->wisize)
    {
        eprintf("Error allocating max work sizes vector!");
        return -1;
    }
    memcpy((void *)(dev->wisize), (void *)(mcl_res[id].dev->wisize),
           sizeof(size_t) * dev->ndims);

    return 0;
}

int mcl_init(uint64_t workers, uint64_t flags)
{
    uint64_t i;

    if (cas(&status, MCL_NONE, MCL_STARTED) == false)
    {
        Dprintf("MCL library has been/is being already initialized by another thread...");
        return 1;
    }

    if (!workers)
    {
        eprintf("Number of worker threads must be >= 1. Aborting.");
        goto err;
    }

    mcl_desc.workers = workers;
    mcl_desc.flags = flags;
    mcl_desc.pid = getpid();
    mcl_desc.out_msg = 0;
#ifdef _STATS
    mcl_desc.nreqs = 0;
    mcl_desc.max_reqs = 0;
#endif
    pthread_barrier_init(&mcl_desc.wt_barrier, NULL, mcl_desc.workers + 2);

    Dprintf("Initializing Minos Computing Library (wt=%" PRIu64 " flags=0x%" PRIx64 " max_msg=%lu (buffer size=%d msg size=%lu))",
            mcl_desc.workers, mcl_desc.flags, MCL_MSG_MAX, MCL_SND_BUF, MCL_MSG_SIZE);

    if (cli_setup())
    {
        eprintf("Error setting up MCL library. Aborting.");
        goto err;
    }
    Dprintf("Creating cleanup thread...");
    if (pthread_create(&(mcl_desc.wcleanup), NULL, check_pending, NULL))
    {
        eprintf("Error creating cleanup thread.");
        goto err_setup;
    }

    Dprintf("Creating %" PRIu64 " worker threads...", mcl_desc.workers);
    mcl_desc.wids = (struct worker_struct *)malloc(mcl_desc.workers *
                                                   sizeof(struct worker_struct));
    if (!mcl_desc.wids)
    {
        eprintf("Error allocating worker thread ids");
        goto err_setup;
    }
    memset((void *)mcl_desc.wids, 0, mcl_desc.workers * sizeof(struct worker_struct));
    for (i = 0; i < mcl_desc.workers; i++)
    {
        mcl_desc.wids[i].id = i;
        mcl_desc.wids[i].ntasks = 0;
        if (pthread_create(&mcl_desc.wids[i].tid, NULL, worker, (void *)&mcl_desc.wids[i]))
        {
            eprintf("Error creating worker thread %" PRIu64, i);
            goto err_setup;
        }
#ifdef _STATS
        mcl_desc.wids[i].nreqs = 0;
#endif
    }

    if (flags & MCL_SET_BIND_WORKERS)
    {
#ifndef __APPLE__
        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        int core_id = mcl_desc.start_cpu % num_cores;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        Dprintf("\t Binding main thread to CPU: %d", core_id);

        pthread_t current_thread = pthread_self();
        pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
#else
        eprintf("Binding threads to CPUs is unsupported on Apple. Ignoring flag.");
#endif
    }

    if (cas(&status, MCL_STARTED, MCL_ACTIVE) == false)
    {
        eprintf("MCL library failed to initialize for an unkonwn reason.");
        return 1;
    }

    pthread_barrier_wait(&mcl_desc.wt_barrier);

    Dprintf("MCL initialized");

    return 0;

err_setup:
    cli_shutdown();

err:
    return -1;
}

int mcl_finit(void)
{
    uint64_t i;
    Dprintf("Finalizing Minos Computing Library");

    if (__atomic_load_n(&status, __ATOMIC_RELAXED) != MCL_ACTIVE)
    {
        eprintf("Library was not properly initialized.");
        goto err;
    }

    cas(&status, MCL_ACTIVE, MCL_DONE);

    Dprintf("Terminating workers...");
    for (i = 0; i < mcl_desc.workers; i++)
    {
        Dprintf("  Waiting for worker %" PRIu64 " (ntasks=%lu)",
                i, mcl_desc.wids[i].ntasks);
        pthread_join(mcl_desc.wids[i].tid, NULL);
    }
    Dprintf("Worker threads terminated.");

    Dprintf("Terminating cleanup thread...");
    pthread_join(mcl_desc.wcleanup, NULL);
    Dprintf("Cleanup thread terminated.");

    if (cli_shutdown())
    {
        eprintf("Error finalizing MCL library. Aborting.");
        goto err;
    }

    stprintf("Reqs: %lu Max_Reqs: %lu Msg_Out: %" PRIu64,
             mcl_desc.nreqs, mcl_desc.max_reqs, mcl_desc.out_msg);
    return 0;

err:
    return -1;
}

mcl_transfer *mcl_transfer_create(uint64_t nargs, uint64_t ncopies, uint64_t flags)
{
    Dprintf("Creating a new transfer task...");
    return __transfer_create(nargs, ncopies, flags);
}

int mcl_transfer_set_arg(mcl_transfer *t, uint64_t id, void *addr, size_t size, off_t offset, uint64_t flags)
{
    if (id > t->nargs)
    {
        eprintf("Invalid argument");
        return -MCL_ERR_INVARG;
    }

    flags = flags | MCL_ARG_RESIDENT | MCL_ARG_BUFFER;
    if (!t || !size || !addr)
    {
        eprintf("Invalid argument");
        return -MCL_ERR_INVARG;
    }

    Dprintf("Setting argument of transfer (addr: %p size: %lu flags: 0x%" PRIx64 ")",
            addr, size, flags);

    t->args[id] = addr;
    t->sizes[id] = size;
    t->offsets[id] = offset;
    t->flags[id] = flags;

    Dprintf("Finished setting argument %" PRIu64 " for transfer at %p", id, t);
    return 0;
}

int mcl_transfer_exec(mcl_transfer *t, uint64_t flags)
{
    mcl_request *r = NULL;
    uint64_t i, j, ret;

    if (!flags)
        flags = MCL_TASK_DFT_FLAGS;
    if (t->ncopies > 1)
        flags |= MCL_FLAG_NO_RES;

    if (!((flags & MCL_TASK_TYPE_MASK) & avail_dev_types))
    {
        eprintf("Invalid flags, device type not available (flags = 0x%" PRIx64
                " avail dev types = 0x%" PRIx64 ").",
                flags, avail_dev_types);
        return -MCL_ERR_INVDEV;
    }

    Dprintf("Setting arguments for transfer handles.");
    for (i = 0; i < t->ncopies; i++)
    {
        for (j = 0; j < t->nargs; j++)
        {
            if (__set_arg(t->handles[i], j, t->args[j], t->sizes[j], t->offsets[j], t->flags[j]))
            {
                eprintf("Unable to set argument %" PRIu64 " for transfer %" PRIu64 "", j, i);
                ret = MCL_ERR_INVARG;
                goto err;
            };
        }

        Dprintf("Submitting TRANSFER AM (RID=%" PRIu32 ", FLAGS=0x%" PRIu64 ")",
                t->handles[i]->rid, flags);

        r = rlist_remove(&ptasks, &ptasks_lock, t->handles[i]->rid);
        if (!r)
        {
            ret = MCL_ERR_INVREQ;
            goto err;
        }

        if (req_add(&hash_reqs, t->handles[i]->rid, r))
        {
            eprintf("Error adding request %u to hash table", t->handles[i]->rid);
            ret = MCL_ERR_INVREQ;
            goto err;
        }
        ainc(&mcl_desc.num_reqs);

#ifdef _STATS
        stats_inc(mcl_desc.nreqs);
        if (mcl_desc.nreqs > mcl_desc.max_reqs)
            mcl_desc.max_reqs = mcl_desc.nreqs;
#endif

        if (__am_exec(t->handles[i], r, flags))
        {
            ret = MCL_ERR_EXEC;
            goto err;
        }
    }
    return 0;

err:
    t->ncopies = i;
    return -ret;
}

int mcl_transfer_wait(mcl_transfer *t)
{
    int ret;

    for (int i = 0; i < t->ncopies; i++)
    {
        if ((ret = mcl_wait(t->handles[i])))
        {
            return ret;
        }
    }

    return 0;
}

int mcl_transfer_test(mcl_transfer *t)
{
    int ret;

    for (int i = 0; i < t->ncopies; i++)
    {
        ret = mcl_test(t->handles[i]);
        if (ret != MCL_REQ_COMPLETED)
            return MCL_REQ_INPROGRESS;
    }
    return MCL_REQ_COMPLETED;
}

int mcl_transfer_free(mcl_transfer *t)
{
    for (int i = 0; i < t->ncopies; i++)
    {
        if (mcl_hdl_free(t->handles[i]))
            return MCL_ERR_INVREQ;
    }

    free(t->args);
    free(t->sizes);
    free(t->flags);
    free(t->handles);

    free(t);
    return 0;
}

void *mcl_get_shared_buffer(const char *name, size_t size, int flags)
{
    void *buffer = mcl_request_shared_mem(name, size, flags);
    return buffer;
}

void mcl_free_shared_buffer(void *buffer)
{
    mcl_release_shared_mem(buffer);
    return;
}

mcl_handle *mcl_task_create_with_props(uint64_t props)
{
    // Dprintf("Creating a new task...");
    return __task_create(props);
}

uint32_t mcl_task_get_sharing_id(mcl_handle *hdl)
{
    /** Returns an id that can be used to reference this handle from another process or
     * -1 if this is not a shared handle. (Allocated with the flag MCL_HDL_SHARED.) Note: it
     * is not necessary to use this method, shared handles are idenfied by an id that corresponds
     * to the order which they were allocated. That is a reliable method of refering to a task
     * from another process. If for some reason, the order is not known, or is not deterministic, this
     * method may be used to retrieve the id.
     **/

    if (!hdl->flags & MCL_HDL_SHARED)
    {
        return -1;
    }
    return mcl_get_shared_hdl_id(hdl);
}

int mcl_test_shared_hdl(pid_t process, uint32_t shared_id)
{
    mcl_handle *hdl = mcl_get_shared_hdl(process, shared_id);
    if (!hdl)
    {
        // eprintf("Invalid argument");
        return -1;
    }

    return __internal_wait(hdl, 0, MCL_TIMEOUT);
}

int mcl_wait_shared_hdl(pid_t process, uint32_t shared_id)
{
    mcl_handle *hdl = mcl_get_shared_hdl(process, shared_id);
    if (!hdl)
    {
        // eprintf("Invalid argument");
        return -1;
    }
    // Dprintf("Wating for handle from another process");

    return __internal_wait(hdl, 1, MCL_TIMEOUT);
}

int mcl_task_set_arg_buffer(mcl_handle *hdl, uint64_t id, void *addr, size_t size, off_t offset, uint64_t flags)
{
    if (!hdl || !size || !flags)
    {
        eprintf("Invalid argument");
        return -MCL_ERR_INVARG;
    }

    if (offset % MCL_MEM_PAGE_SIZE != 0)
    {
        eprintf("Invalid subbuffer offset.");
        return -MCL_ERR_INVARG;
    }

    // Dprintf("Setting argument of task %u (addr: %p size: %lu flags: 0x%"PRIx64")",
    //	hdl->rid, addr, size, flags);

    return __set_arg(hdl, id, addr, size, offset, flags);
}

int mcl_register_buffer(void *addr, size_t size, uint64_t flags)
{
    return __buffer_register(addr, size, flags);
}

int mcl_unregister_buffer(void *addr)
{
    return __buffer_unregister(addr);
}

int mcl_invalidate_buffer(void *addr)
{
    return __invalidate_buffer(addr);
}

static int mcl_exec_common(mcl_handle *h, uint64_t *pes, uint64_t *lsize, uint64_t *offsets, uint64_t flags, uint64_t ndependencies, mcl_handle **dep_list)
{
    int found = -1;
    mcl_device_t *dev = NULL;
    mcl_request *r = NULL;
    mcl_task *t = NULL;
    uint64_t i;

    if (!h || !pes)
        return -MCL_ERR_INVARG;

    /*
     * PES and LPES (if provided) should be >=1 in each dimension
     */
    for (i = 0; i < MCL_DEV_DIMS; i++)
    {
        if (!pes[i])
            return -MCL_ERR_INVPES;
        if (lsize && !lsize[i])
            return -MCL_ERR_INVPES;
    }

    r = rlist_remove(&ptasks, &ptasks_lock, h->rid);
    if (!r)
        return -MCL_ERR_INVREQ;

    t = r->tsk;

    if (!flags)
        flags = MCL_TASK_DFT_FLAGS;

    if (!((flags & MCL_TASK_TYPE_MASK) & avail_dev_types))
    {
        eprintf("Invalid flags, device type not available (flags = 0x%" PRIx64
                " avail dev types = 0x%" PRIx64 ").",
                flags, avail_dev_types);
        return -MCL_ERR_INVDEV;
    }

    if(!t->kernel)
    {
        return - MCL_ERR_INVKER;
    }

    /*
     * Clearn user provided architectures not supported for this kernel. Currently flags only
     * allows to specify architectures, but in the future we might need to consider
     * MCL_TASK_TYPE_MASK bits only in flags.
     */

    flags = flags & t->kernel->targets;

    if ((flags & MCL_TASK_TYPE_MASK) == 0)
    {
        eprintf("Found no program to run kernel %s on selected devices (0x%" PRIx64 ")", t->kernel->name, flags & MCL_TASK_TYPE_MASK);
        return -MCL_ERR_INVDEV;
    }

    // Check if there is at least a device that can run the task
    VDprintf("  Computing total PES and dimensions...");
    t->tpes = 1;
    for (i = 0; i < MCL_DEV_DIMS; i++)
    {
        t->tpes *= pes[i];
        t->pes[i] = pes[i];
        t->lpes[i] = lsize ? lsize[i] : 0;
        t->offsets[i] = offsets ? offsets[i] : 0;
        if (pes[i] > 1)
            t->dims++;
    }

    if (!(t->dims))
        t->dims = 1;

    for (i = 0; i < mcl_desc.info->ndevs && found < 0; i++)
    {
        int stop = 0;

        dev = mcl_res[i].dev;
        /* Check type... */
        if (!(dev->type & flags))
            continue;

        /* For FPGAs we don't really have a way to test PEs and memory */
        if (flags == MCL_TASK_FPGA && dev->type == MCL_TASK_FPGA)
        {

            found = i;
            continue;
        }

        /* Check memory... */
        if (t->mem > dev->mem_size)
        {
            continue;
        }

        /* Check required dimensions... */
        for (int j = 0; j < MCL_DEV_DIMS && !stop; j++)
        {
            if (dev->wisize[j] == 1 && t->pes[j] > 1)
                stop = 1;
        }
        if (stop)
            continue;

        /* Check per-dimension work-item size... */
        for (int j = 0; j < t->dims; j++)
            if (t->lpes[j] > dev->wisize[j])
                continue;

        /* Check work group size... */
        for (int j = 0; j < t->dims; j++)
            if (t->lpes[j])
                if (t->pes[j] / t->lpes[j] > dev->wgsize)
                    continue;
        found = i;
    }

    if (found < 0)
    {
        Dprintf("No device found for Task %" PRIu32 "", h->rid);
        return -MCL_ERR_INVDEV;
    }

    Dprintf("Task %" PRIu32 " can be executed on resource %d (%" PRIu64 "/%" PRIu64 " PEs, %" PRIu64 "/%" PRIu64 " bytes)",
            h->rid, found, t->tpes, dev->pes, t->mem, dev->mem_size);

    if (ndependencies > 0 && dep_list == NULL)
    {
        eprintf("Invalid arguments, no dependency list is null but ndependencies is 0.");
        return -MCL_ERR_INVARG;
    }
    uint32_t dep_idx = 0;
    for (i = 0; i < ndependencies; i++)
    {
        mcl_handle *dep_hdl = dep_list[i];
        if (dep_hdl->status != MCL_REQ_COMPLETED || dep_hdl->status != MCL_REQ_FINISHING)
        {
            mcl_request *dep = req_search(&hash_reqs, dep_hdl->rid);
            t->dependencies[dep_idx++] = dep;
            if (!dep)
            {
                eprintf("Error finding dependency %u in hash table", dep_hdl->rid);
                return -MCL_ERR_INVREQ;
            }
        }
    }
    t->ndependencies = dep_idx;
    cas(&t->dependency_status, MCL_NONE, MCL_ACTIVE);

    if (req_add(&hash_reqs, h->rid, r))
    {
        eprintf("Error adding request %u to hash table", h->rid);
        return -MCL_ERR_INVREQ;
    }
    ainc(&mcl_desc.num_reqs);

#ifdef _STATS
    stats_inc(mcl_desc.nreqs);
    if (mcl_desc.nreqs > mcl_desc.max_reqs)
        mcl_desc.max_reqs = mcl_desc.nreqs;
#endif

    return __am_exec(h, r, flags);
}

int mcl_exec(mcl_handle *h, uint64_t *pes, uint64_t *lsize, uint64_t flags)
{
    return mcl_exec_common(h, pes, lsize, NULL, flags, 0, NULL);
}

int mcl_exec2(mcl_handle *h, uint64_t *pes, uint64_t *lsize, uint64_t *offsets, uint64_t flags)
{
    return mcl_exec_common(h, pes, lsize, offsets, flags, 0, NULL);
}

int mcl_exec_with_dependencies(mcl_handle *h, uint64_t *pes, uint64_t *lsize, uint64_t flags, uint64_t ndependencies, mcl_handle **dep_list)
{
    return mcl_exec_common(h, pes, lsize, NULL, flags, ndependencies, dep_list);
}

int mcl_exec2_with_dependencies(mcl_handle *h, uint64_t *pes, uint64_t *lsize, uint64_t *offsets, uint64_t flags, uint64_t ndependencies, mcl_handle **dep_list)
{
    return mcl_exec_common(h, pes, lsize, offsets, flags, ndependencies, dep_list);
}
