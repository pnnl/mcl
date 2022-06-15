#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <utlist.h>
#include <atomics.h>
#include <minos.h>
#include <minos_internal.h>
#include <minos_sched.h>
#include <ptrhash.h>
#include <stats.h>
#include <tracer.h>

#define SCHED_REQ_TABLE_SIZE_SHIFT 20

mcl_sched_t mcl_sched_desc;
mcl_info_t *mcl_info = NULL;
cl_platform_id *cl_plts = NULL;
mcl_platform_t *mcl_plts = NULL;
mcl_client_t *mcl_clist = NULL;
mcl_resource_t *mcl_res = NULL;
mcl_class_t *mcl_class = NULL;

static volatile sig_atomic_t sched_done = 0;
int sock_fd, shm_fd;
struct sockaddr_un saddr;
static pthread_t rcv_tid;

static uint64_t num_threads = 0;

struct sched_class *sched_curr = &fifo_class;
extern const struct sched_resource_policy ff_policy;
extern const struct sched_resource_policy rr_policy;
extern const struct sched_resource_policy delay_policy;
extern const struct sched_resource_policy hybrid_policy;

extern const struct sched_eviction_policy lru_eviction_policy;

/* The following lock protects sched_req_table */
static pthread_mutex_t sched_req_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sched_req_has_slots = PTHREAD_COND_INITIALIZER;
static struct ph_table *sched_req_table;

static const char *socket_name = NULL;
static const char *shared_mem_name = NULL;

#ifdef _DEBUG
void resource_list(mcl_resource_t *res, uint64_t n);
#endif

static inline int srv_msg_send(struct mcl_msg_struct *msg, struct sockaddr_un *dst)
{
    int ret;

    while (((ret = msg_send(msg, sock_fd, dst)) > 0))
    {
        Dprintf("Tried to send message, but recieved error: %d", ret);
        sched_yield();
    }

    return ret;
}

static inline int srv_msg_recv(struct mcl_msg_struct *msg)
{
    struct sockaddr_un src;
    int ret;

    ret = msg_recv(msg, sock_fd, &src);

    if (ret)
        return ret;

    if (!(msg->pid = (uint64_t)cli_get_pid(src.sun_path)))
    {
        eprintf("Error extracting source PID");
        return -1;
    }

    return 0;
}

int default_assign_resource(sched_req_t *r)
{
#if defined _TRACE || defined _DEBUG
    uint64_t pes_now;
#endif
    uint64_t mem_now, kernels_now;
    mcl_resource_t *res;
    assert(r->dev >= 0);
    res = mcl_res + r->dev;

    uint64_t res_mem = 0;
    for (int i = 0; i < r->nresident; i++)
    {
        if (sched_rdata_add_device(r->resdata[i], r->dev))
        {
            res_mem += r->resdata[i]->size;
        }
        eviction_policy_used(&r->resdata[i]->enodes[r->dev]);

        if (r->resdata[i]->flags & MSG_ARGFLAG_EXCLUSIVE)
        {
            Dprintf("\t\t Allocating exclusive memory to the correct device");
            mcl_partition_t *region = &r->regions[i];
            mcl_partition_t sentinel;
            sentinel.offset = region->offset + region->size - 1;
            int64_t cur_idx = list_search_prev(&r->resdata[i]->subbuffers, &sentinel);
            mcl_partition_t *cur = list_get(&r->resdata[i]->subbuffers, cur_idx);

            while (cur && cur->offset + cur->size > region->offset)
            {
                if (cur->offset < region->offset)
                {
                    cur->size = region->offset - cur->offset;
                    if (cur->offset + cur->size > region->offset + region->size)
                    {
                        cur->size = cur->offset + cur->size - (region->offset + region->size);
                        cur->offset = region->offset + region->size;
                    }
                    cur_idx = cur->prev;
                }
                else if (cur->offset + cur->size > region->offset + region->size)
                {
                    cur->size = cur->offset + cur->size - (region->offset + region->size);
                    cur->offset = region->offset + region->size;
                    cur_idx = cur->prev;
                }
                else
                {
                    cur_idx = cur->prev;
                    list_delete(&r->resdata[i]->subbuffers, cur);
                }
                cur = list_get(&r->resdata[i]->subbuffers, cur_idx);
            }
            region->dev = r->dev;
            list_insert(&r->resdata[i]->subbuffers, region);
            Dprintf("\t\t Allocated exclusive memory to the correct device, devs: 0x%016" PRIx64 "", r->resdata[i]->devs);
        }
    }
    int64_t needed_mem = r->mem - res_mem;
    Dprintf("Needed Mem: %" PRId64 ", Task Mem: %" PRIu64 ", Resident Mem: %" PRIu64 ", Num Res: %" PRIu64 ", Avail Mem: %" PRIu64 "",
            needed_mem, r->mem, res_mem, r->nresident, res->mem_avail);

#if defined _TRACE || defined _DEBUG
    pes_now = add_fetch(&res->pes_used, r->pes);
#else
    add_fetch(&res->pes_used, r->pes);
#endif
	mem_now = add_fetch(&res->mem_avail, -needed_mem);
	kernels_now = ainc(&res->nkernels) + 1;

	if(!(r->type & MCL_TASK_FPGA)){
                assert(mem_now < res->dev->mem_size); /* safety check against wrap around */
	        if(!mem_now || kernels_now >= res->dev->max_kernels)		
		        res->status = MCL_DEV_FULL;
	        else
		        res->status = MCL_DEV_ALLOCATED;

                Dprintf("  Resource %"PRIu64" now has %"PRIu64" kernels running, %"PRIu64"/%"PRIu64" PEs used and %"PRIu64"/%"PRIu64" MEM available", r->dev, kernels_now, pes_now, res->dev->pes, mem_now, res->dev->mem_size);

	        TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" pes used", r->dev, pes_now,
		        res->dev->pes);
                TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" mem available", r->dev, mem_now,
		        res->dev->mem_size);
                TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" kernels running", r->dev, kernels_now,
		        res->dev->max_kernels);
        }
        
	return r->dev;
}

int default_put_resource(sched_req_t *r)
{
#if defined _DEBUG || defined _TRACE
    uint64_t pes_now;
#endif

    uint64_t mem_now, kernels_now;
    mcl_resource_t *res;
    if (r->dev < 0)
        return -1;
    res = mcl_res + r->dev;

    uint64_t mem_freed = r->mem;
    for (int i = 0; i < r->nresident; i++)
    {
        uint64_t refs = adec(&(r->resdata[i]->refs)) - 1;
        eviction_policy_released(&r->resdata[i]->enodes[r->dev]);
        mem_freed -= r->resdata[i]->size;
        if(!(r->resdata[i]->valid) && refs == 0){
            free(r->resdata[i]->enodes);
            free(r->resdata[i]);
        }
        
    }
    Dprintf("Task Mem: %"PRIu64", Mem Freed: %"PRIu64", Num Resident: %"PRIu64"", r->mem, mem_freed, r->nresident);

    mem_now = add_fetch(&res->mem_avail, mem_freed);
    kernels_now = adec(&res->nkernels) - 1;

#if defined _DEBUG || defined _TRACE
    pes_now = add_fetch(&res->pes_used, -r->pes);
#else
    add_fetch(&res->pes_used, -r->pes);
#endif

    res->status = mem_now || kernels_now ? MCL_DEV_ALLOCATED : MCL_DEV_READY;

    Dprintf("  Resource %" PRIu64 " now has %" PRIu64 " kernels running, %" PRIu64 "/%" PRIu64 " PEs used and %" PRIu64 "/%" PRIu64 " MEM available",
            r->dev, kernels_now, pes_now, res->dev->pes, mem_now, res->dev->mem_size);

    TRprintf("R[%" PRIu64 "]: %" PRIu64 "/%" PRIu64 " pes used", r->dev,
             pes_now, res->dev->pes);
    TRprintf("R[%" PRIu64 "]: %" PRIu64 "/%" PRIu64 " mem available", r->dev, mem_now,
             res->dev->mem_size);
    TRprintf("R[%" PRIu64 "]: %" PRIu64 "/%" PRIu64 " kernels running", r->dev, kernels_now,
             res->dev->max_kernels);

    if (r->policy_data)
    {
        free(r->policy_data);
        r->policy_data = NULL;
    }

    return 0;
}

int default_stats()
{
    return 0;
}

int scheduler_evict_mem(int dev)
{
    enode_t *enode_to_free;
    if (dev < 0)
        enode_to_free = eviction_policy_evict();
    else
        enode_to_free = eviction_policy_evict_from_dev(dev);

    if (!enode_to_free)
    {
        eprintf("Could not free memory by eviction");
        return -1;
    }

    sched_rdata *mem = enode_to_free->mem_data;

    dev = enode_to_free->dev;
    sched_rdata_rm_device(mem, dev);
    Dprintf("Evicting memory %" PRIu64 " from device %d, size: %" PRIu64 "", mem->mem_id, dev, mem->size);

    mcl_resource_t *res = mcl_res + dev;
    uint64_t mem_now = add_fetch(&res->mem_avail, mem->size);
    res->status = mem_now || res->nkernels ? MCL_DEV_ALLOCATED : MCL_DEV_READY;

    TRprintf("R[%d]: %" PRIu64 "/%" PRIu64 " mem available", dev, mem_now,
             res->dev->mem_size);

    struct mcl_msg_struct msg;
    struct mcl_client_struct *dst;

    msg_init(&msg);
    msg.cmd = MSG_CMD_FREE;
    msg.nres = 1;
    msg.resdata = malloc(sizeof(msg_arg_t));
    msg.resdata->mem_id = mem->mem_id;
    msg.resdata->mem_size = mem->size;
    msg.res = dev;

    int error = 0;
    process_t *el;
    DL_FOREACH(mem->processes, el)
    {
        dst = cli_search(&mcl_clist, el->pid);
        if (srv_msg_send(&msg, &(dst->addr)))
        {
            eprintf("Error sending FREE to client %d", el->pid);
            error = -1;
            break;
        }
    }
    msg_free(&msg);
    return error;
    ;
}

static inline int sched_run(sched_req_t *r)
{
    struct mcl_msg_struct ack;
    struct mcl_client_struct *dst;

    dst = cli_search(&mcl_clist, r->key.pid);
    if (!dst)
    {
        eprintf("Client %d not registered.", r->key.pid);
        goto err;
    }

    msg_init(&ack);
    ack.cmd = MSG_CMD_ACK;
    ack.rid = r->key.rid;
    ack.pid = r->key.pid;
    ack.res = r->dev;

    if (srv_msg_send(&ack, &(dst->addr)))
    {
        eprintf("Error sending ACK to client %d", ack.pid);
        msg_free(&ack);
        goto err;
    }
    msg_free(&ack);
    return 0;

err:
    return -1;
}

static inline int sched_request_track(sched_req_t *r)
{
    int ret;
    pthread_mutex_lock(&sched_req_lock);
    while ((ret = ph_add(sched_req_table, r, key)) < 0)
    {
        /* table full */
        eprintf("Table full, unable to insert req.\n");
        pthread_cond_wait(&sched_req_has_slots, &sched_req_lock);
    }

    /* here request can be either inserted or refused because duplicate */
    pthread_mutex_unlock(&sched_req_lock);

    return ret;
}

static inline sched_req_t *sched_request_get(const void *key)
{
    sched_req_t *r;

    pthread_mutex_lock(&sched_req_lock);
    ph_get(sched_req_table, key, key, r);
    if (r)
    {
        /* signal waiters */
        pthread_cond_signal(&sched_req_has_slots);
    }
    pthread_mutex_unlock(&sched_req_lock);

    return r;
}

static inline sched_req_t *sched_request_untrack(const void *key)
{
    sched_req_t *r;

    pthread_mutex_lock(&sched_req_lock);
    ph_remove(sched_req_table, key, key, r);
    if (r)
    {
        /* signal waiters */
        pthread_cond_signal(&sched_req_has_slots);
    }
    pthread_mutex_unlock(&sched_req_lock);

    return r;
}

static inline int am_exe(mcl_msg msg)
{
    sched_req_t *r = NULL;

    r = sched_alloc_request();
    if (!r)
    {
        eprintf("Error creating new request for (%d,%" PRIu64 ") ",
                msg.pid, msg.rid);
        goto err;
    }

    r->key.pid = msg.pid;
    r->pes = msg.pes;
    r->mem = msg.mem * MCL_PAGE_SIZE;
    r->key.rid = msg.rid;
    r->flags = msg.flags << MCL_TASK_FLAG_SHIFT;
    r->type = msg.type;
    r->status = 0x0;
    r->num_attempts = 0;
    
    for(int i=0; i<MCL_DEV_DIMS; i++){
        r->dpes[i] = msg.pesdata.pes[i];
        r->lpes[i] = msg.pesdata.lpes[i];
    }

    r->nresident = msg.nres;
    r->task_id = msg.taskid;
    r->policy_data = NULL;
    r->resdata = (sched_rdata **)malloc(sizeof(sched_rdata *) * msg.nres);
    if (!r->resdata)
    {
        eprintf("Error allocating memory for new request (%d,%" PRIu64 ") ",
                msg.pid, msg.rid);
    }
    r->regions = (mcl_partition_t *)malloc(sizeof(mcl_partition_t) * msg.nres);
    if (!r->regions)
    {
        eprintf("Error allocating memory for new request (%d,%" PRIu64 ") ",
                msg.pid, msg.rid);
    }

    Dprintf("Number of resident arguments: %" PRIu64 ".", msg.nres);
    for (uint64_t i = 0; i < msg.nres; i++)
    {
        sched_rdata *el;
        pid_t pid = msg.resdata[i].flags & MSG_ARGFLAG_SHARED ? msg.resdata[i].pid : r->key.pid;
        if (!(el = sched_rdata_get(msg.resdata[i].mem_id, pid)))
        {
            Dprintf("Could not find resident memory in scheduler.");
            el = malloc(sizeof(sched_rdata));
            el->key[0] = 0;
            el->key[1] = 0;
            el->mem_id = msg.resdata[i].mem_id;
            el->pid = pid;
            el->size = (size_t)(msg.resdata[i].overall_size * MCL_MEM_PAGE_SIZE);
            el->flags = msg.resdata[i].flags;
            el->devs = 0;
            el->valid = 1;
            el->refs = 0;
            el->ndevs = 0;
            el->processes = NULL;
            el->enodes = malloc(sizeof(enode_t) * mcl_info->ndevs);
            for (int j = 0; j < mcl_info->ndevs; j++)
            {
                el->enodes[j].dev = j;
                el->enodes[j].refs = 0;
                el->enodes[j].mem_data = el;
                eviction_policy_init_node(&el->enodes[j]);
            }
            Dprintf("Created Scheduler rdata with size: %"PRIu64"", el->size);
            list_init(&el->subbuffers);
            sched_rdata_add(el);
        }
        else
        {
            Dprintf("Found resident memory in scheduler.");
        }
        ainc(&el->refs);

        if (msg.resdata[i].flags & MSG_ARGFLAG_SHARED)
        {
            process_t *process = malloc(sizeof(process_t));
            process->pid = r->key.pid;
            process_t *out = NULL;
            DL_SEARCH(el->processes, out, process, process_cmp);
            if (!out)
                DL_APPEND(el->processes, process);
        }
        r->resdata[i] = el;
        r->regions[i].size = (size_t)(msg.resdata[i].mem_size * MCL_MEM_PAGE_SIZE);
        r->regions[i].offset = (off_t)(msg.resdata[i].mem_offset * MCL_MEM_PAGE_SIZE);

        Dprintf("For request (%d, %" PRIu64 ") arg %" PRIu64 ", found MEMID: %" PRIu64 ", REFs: %" PRIu64 ", NDEVS: %" PRIu64 " DEV: 0x%016" PRIx64 " SIZE: %" PRIu64 " OFFSET: %" PRIu64 "",
                r->key.pid, r->key.rid, i, r->resdata[i]->mem_id, r->resdata[i]->refs, r->resdata[i]->ndevs, r->resdata[i]->devs, r->regions[i].size, r->regions[i].offset);
    }

    Dprintf("Executing EXEC AM (RID=%" PRIu64 " PES=%" PRIu64 " (%" PRIu64 ") MEM=%" PRIu64 " (%" PRIu64 ") )...",
            r->key.rid, r->pes, msg.pes, r->mem, msg.mem);

    pthread_mutex_init(&r->dependent_lock, NULL);
    pthread_mutex_lock(&r->dependent_lock);

    r->dependents = NULL;
    r->dependencies_execing = 0;
    r->dependencies_waiting = 0;
    int exec_count = 0;
    int wait_count = 0;
    for (int i = 0; i < msg.ndependencies; i++)
    {
        struct identifier id;
        id.pid = msg.pid;
        id.rid = msg.dependencies[i];
        dep_list *dep = malloc(sizeof(dep_list));
        dep->r = sched_request_get(&id);
        if (!dep->r)
        {
            free(dep);
            continue;
        }

        pthread_mutex_lock(&dep->r->dependent_lock);
        if (dep->r->status == SCHED_REQ_DONE)
        {
            free(dep);
        }
        else
        {
            dep_list *this = malloc(sizeof(dep_list));
            this->r = r;
            LL_APPEND(dep->r->dependents, this);
            if (dep->r->status <= SCHED_REQ_SCHED_READY)
            {
                wait_count += 1;
                r->dependencies_waiting += 1;
            }
            else if (dep->r->status == SCHED_REQ_EXEC_READY)
            {
                exec_count += 1;
                r->dependencies_execing += 1;
            }
        }
        pthread_mutex_unlock(&dep->r->dependent_lock);
    }

    int ret = sched_request_track(r);

    if (ret > 0)
    {
        eprintf("schedule: duplicate request, discard => (%d, %" PRIu64 ")\n",
                msg.pid, msg.rid);
        sched_release_request(r);
        return -1;
    }

    if (wait_count == 0 && exec_count == 0)
    {
        r->status = SCHED_REQ_EXEC_READY;
        sched_enqueue(r);
    }
    else if (wait_count == 0)
    {
        r->status = SCHED_REQ_SCHED_READY;
        sched_enqueue(r);
    }
    else
    {
        r->status = SCHED_REQ_WAIT;
    }
    pthread_mutex_unlock(&r->dependent_lock);

    return 0;

err:
    return -1;
}

static inline int update_dependencies(sched_req_t *r)
{
    pthread_mutex_lock(&r->dependent_lock);
    r->status = SCHED_REQ_DONE;
    pthread_mutex_unlock(&r->dependent_lock);

    sched_req_t *dep;
    dep_list *el, *temp, *el2;
    LL_FOREACH_SAFE(r->dependents, el, temp)
    {
        dep = el->r;
        pthread_mutex_lock(&dep->dependent_lock);
        dep->dependencies_execing -= 1;
        if (dep->dependencies_execing == 0)
        {
            r->status = SCHED_REQ_EXEC_READY;
            pthread_mutex_unlock(&dep->dependent_lock);
            LL_FOREACH(dep->dependents, el2)
            {
                pthread_mutex_lock(&el2->r->dependent_lock);
                el2->r->dependencies_execing += 1;
                el2->r->dependencies_waiting -= 1;
                if (el2->r->dependencies_waiting == 0)
                {
                    Dprintf("Request %"PRIu64" finished, releasing request %"PRIu64"", r->key.rid, dep->key.rid);
                    el2->r->status = SCHED_REQ_SCHED_READY;
                    sched_enqueue(el2->r);
                }
                pthread_mutex_unlock(&el2->r->dependent_lock);
            }
        }
        else
        {
            pthread_mutex_unlock(&dep->dependent_lock);
        }
    }

    return 0;
}

static inline int am_null(mcl_msg msg)
{
    Dprintf("Executing NULL AM...");

    return 0;
}

static inline int am_reg(mcl_msg msg)
{
    struct mcl_client_struct *el;
    struct mcl_msg_struct ack;

    Dprintf("Executing REG AM...");

    el = (struct mcl_client_struct *)malloc(sizeof(struct mcl_client_struct));
    if (!el)
    {
        eprintf("Error allocating memory for new client");
        goto err;
    }

    el->pid = msg.pid;
    el->flags = 0x0;
    el->status = CLI_ACTIVE;
    el->addr.sun_family = PF_UNIX;
    const char *client_format;
    if ((client_format = getenv("MCL_SOCK_CNAME")) == NULL)
    {
        client_format = MCL_SOCK_CNAME;
    }
    snprintf(el->addr.sun_path, sizeof(el->addr.sun_path),
             client_format, (long)el->pid);

    el->start_cpu = num_threads;
    el->num_threads = msg.threads;
    num_threads += msg.threads;

    if (cli_add(&mcl_clist, el))
    {
        eprintf("Error adding new client.");
        goto err_el;
    }

    msg_init(&ack);
    ack.cmd = MSG_CMD_ACK;
    ack.rid = msg.rid;
    ack.pid = msg.pid;
    ack.res = el->start_cpu;

    if (srv_msg_send(&ack, &(el->addr)))
    {
        eprintf("Error sending ACK to client %d", ack.pid);
        goto err_send;
    }
    msg_free(&ack);

    return 0;

err_send:
    msg_free(&ack);
err_el:
    free(el);
err:
    return -1;
}

static inline int am_end(mcl_msg msg)
{
    Dprintf("Executing END AM...");

#ifdef _STATS
    sched_stats();
#endif

    cli_remove(&mcl_clist, msg.pid);

#if defined _DEBUG || defined _TRACE
    uint64_t mem_now;
#endif
    uint64_t *mem_freed = (uint64_t *)malloc(mcl_info->ndevs * sizeof(uint64_t));
    mcl_resource_t *res = mcl_res;
    if (!mem_freed)
    {
        eprintf("Error allocating memory.");
        return -1;
    }
    sched_rdata_rm_pid(msg.pid, mem_freed, mcl_info->ndevs);
    for (int i = 0; i < mcl_info->ndevs; i++, res++)
    {
#if defined _DEBUG || defined _TRACE
        mem_now = add_fetch(&res->mem_avail, mem_freed[i]);
#else
        add_fetch(&res->mem_avail, mem_freed[i]);
#endif
        Dprintf("  Resource %d now %" PRIu64 "/%" PRIu64 " MEM available",
                i, mem_now, mcl_res[i].dev->mem_size);

        TRprintf("R[%d]: %" PRIu64 "/%" PRIu64 " mem available", i, mem_now,
                 res->dev->mem_size);
    }
    free(mem_freed);

    return 0;
}

static inline int am_done(mcl_msg msg)
{
    sched_req_t *r = NULL;
    /* FIXME: this could be endian dependent */
    uint64_t key[2] = {msg.pid, msg.rid};

#ifdef _DEBUG
    if (msg.cmd == MSG_CMD_DONE)
        Dprintf("Request (%d,%" PRIu64 ") completed successfully", msg.pid, msg.rid);
    else
        Dprintf("Request (%d,%" PRIu64 ") completed with errors", msg.pid, msg.rid);
#endif

    r = sched_request_untrack(key);

    if (!r)
    {
        eprintf("Error post-processing request (%d,%" PRIu64 ")",
                msg.pid, msg.rid);
        return -1;
    }

    if (sched_put_resource(r) < 0)
    {
        eprintf("  Unable to put resource for (%d, %" PRIu64 ")",
                r->key.pid, r->key.rid);
    }

    update_dependencies(r);

    sched_complete(r);

    sched_release_request(r);

    return 0;
}

int am_free(struct mcl_msg_struct msg)
{
    /* This is not ideal because in all other cases round robin deals with device memory*/
    mcl_resource_t *res;
    sched_rdata *el;
    uint64_t devs;
    int i;
    int cur_dev;
#if defined _DEBUG || defined _TRACE
    uint64_t mem_now;
#endif

    Dprintf("Number of resources to free: %" PRIu64 "", msg.nres);

    for (i = 0; i < msg.nres; i++)
    {
        el = sched_rdata_rm(msg.resdata[i].mem_id, msg.pid);
        if (!el)
        {
            Dprintf("Could not find memory to free <%d, %" PRIu64 ">",
                    msg.pid, msg.resdata[i].mem_id);
            continue;
        }
        res = mcl_res;
        devs = el->devs;
        cur_dev = 0;
        while (devs)
        {
            if (devs & 0x01)
            {
                eviction_policy_removed(&el->enodes[cur_dev]);
#if defined _DEBUG || defined _TRACE
                mem_now = add_fetch(&res->mem_avail, el->size);
                Dprintf("  Resource %d now %" PRIu64 "/%" PRIu64 " MEM available",
                        cur_dev, mem_now, res->dev->mem_size);
                TRprintf("R[%d]: %" PRIu64 "/%" PRIu64 " mem available", cur_dev, mem_now,
                         res->dev->mem_size);
#else
                add_fetch(&res->mem_avail, el->size);
#endif
            }
            devs = devs >> 1;
            res += 1;
            cur_dev += 1;
        }
        el->devs = 0;
        el->ndevs = 0;
        el->valid = 0;

        if(ld_acq(&el->refs) == 0)
        {
            free(el->enodes);
            free(el);
        }
    }
    /* FIXME: Need a way to notify waiting scheduler...
     *  This is kind of hacky but works with both fffs and fifo
     */
    sched_complete(NULL);
    return 0;
}

int exec_am(struct mcl_msg_struct msg)
{
    switch (msg.cmd)
    {
    case MSG_CMD_NULL:
        if (am_null(msg))
            goto err;
        break;
    case MSG_CMD_REG:
        if (am_reg(msg))
            goto err;
        break;
    case MSG_CMD_EXE:
        if (am_exe(msg))
            goto err;
        break;
    case MSG_CMD_END:
        if (am_end(msg))
            goto err;
        break;
    case MSG_CMD_DONE:
        if (am_done(msg))
            goto err;
        break;
    case MSG_CMD_FREE:
        if (am_free(msg))
            goto err;
        break;
    case MSG_CMD_ERR:
        if (am_done(msg))
            goto err;
        break;
    default:
        eprintf("Unrecognied AM 0x%" PRIx64 ".", msg.cmd);
        return -1;
    }

    return 0;

err:
    eprintf("Error executing AM 0x%" PRIx64, msg.cmd);
    return -1;
}

void *receiver(void *data)
{
    struct mcl_msg_struct msg;

    Dprintf("Schedule Receiver thread started.");

    while (!sched_done)
    {
        if (srv_msg_recv(&msg) == 1)
        {
            sched_yield();
            continue;
        }

        if (exec_am(msg))
        {
            eprintf("Error executing AM");
        }
        msg_free(&msg);
    }

    Dprintf("Schedule Receiver thread terminating...");
    pthread_exit(0);
}

int schedule(void)
{
    sched_req_t *r;
    while (!sched_done)
    {
        if ((r = sched_pick_next()))
        {
            Dprintf("Scheduling request %" PRIu64 " on resource %" PRIu64 "", r->key.rid, r->dev);

            sched_assign_resource(r);
            sched_run(r);
        }
        else
        {
            sched_yield();
        }
    }

    return 0;
}

int __setup(void)
{
    /*
     * Cleanup in case of previous errors
     */
    if ((socket_name = getenv("MCL_SOCK_NAME")) == NULL)
    {
        socket_name = MCL_SOCK_NAME;
    }
    if ((shared_mem_name = getenv("MCL_SHM_NAME")) == NULL)
    {
        shared_mem_name = MCL_SHM_NAME;
    }

    shm_unlink(shared_mem_name);
    unlink(socket_name);

    Dprintf("Creating shared memory object %s...", shared_mem_name);
    shm_fd = shm_open(shared_mem_name, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRGRP | S_IROTH);
    if (shm_fd == -1)
    {
        eprintf("Error creating shared memory descriptor. Aborting.");
        perror("shm_open");
        goto err;
    }

    if (ftruncate(shm_fd, MCL_SHM_SIZE))
    {
        eprintf("Error truncating shared memory file. Aborting.");
        perror("ftruncate");
        goto err;
    }

    mcl_info = (mcl_info_t *)mmap(NULL, MCL_SHM_SIZE, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, shm_fd, 0);
    if (!mcl_info)
    {
        eprintf("Error mapping shared memory object. Aborting.");
        perror("mmap");
        goto err_shm_fd;
    }
    memset((void *)mcl_info, 0, MCL_SHM_SIZE);
    Dprintf("Shared memory mapped at address %p.", mcl_info);

    if (resource_discover(&cl_plts, &mcl_plts, &mcl_class, &mcl_info->nplts, &mcl_info->ndevs))
    {
        eprintf("Error discovering computing elements! Aborting!");
        goto err_mmap;
    }

    mcl_info->nclass = class_count(mcl_class);
    mcl_res = (mcl_resource_t *)malloc(mcl_info->ndevs * sizeof(mcl_resource_t));
    if (!mcl_res)
    {
        eprintf("Error allocating memory to map MCL resources.");
        goto err_mmap;
    }

    if (resource_map(mcl_plts, mcl_info->nplts, mcl_class, mcl_res))
    {
        eprintf("Error mapping devices to MCL resources");
        goto err_res;
    }

    Dprintf("Discovered %" PRIu64 " platforms and a total of %" PRIu64 " devices",
            mcl_info->nplts, mcl_info->ndevs);

    if (msg_setup(mcl_info->ndevs))
    {
        eprintf("Error setting up message");
        goto err_res;
    }

    if (resource_create_ctxt(mcl_res, mcl_info->ndevs))
    {
        eprintf("Error creating resource contexts.");
        goto err_res;
    }

    mcl_sched_desc.nclients = 0;
#ifdef _STATS
    mcl_sched_desc.nreqs = 0;
#endif
    lru_eviction_policy.init(mcl_res, mcl_info->ndevs);
    Dprintf("Init EvictionPolicy at %p.", &lru_eviction_policy);

    ff_policy.init(mcl_res, mcl_info->ndevs);
    Dprintf("Init First-Fit resource scheduling at %p.", &ff_policy);
    rr_policy.init(mcl_res, mcl_info->ndevs);
    Dprintf("Init Round-Robin resource scheduling at %p.", &rr_policy);
    delay_policy.init(mcl_res, mcl_info->ndevs);
    Dprintf("Init DelaySched resource scheduling at %p.", &delay_policy);
    hybrid_policy.init(mcl_res, mcl_info->ndevs);
    Dprintf("Init HybridSched resource scheduling at %p.", &hybrid_policy);

    Dprintf("MCL descriptor at %p size = 0x%lx.", mcl_info, sizeof(struct mcl_desc_struct));

    for (uint64_t i = 0; i < mcl_info->ndevs; i++)
    {
        for (uint64_t j = 0; j < res_getDev(i)->nqueues; j++)
        {
            clReleaseCommandQueue(res_getClQueue(i, j));
        }
        clReleaseContext(res_getClCtx(i));
    }

    mcl_info->sndbuf = MCL_SND_BUF;
    mcl_info->rcvbuf = MCL_RCV_BUF;

    sock_fd = socket(PF_LOCAL, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        eprintf("Error creating communication socket.");
        perror("socket");
        goto err_res;
    }

    if (fcntl(sock_fd, F_SETFL, O_NONBLOCK))
    {
        eprintf("Error setting scheduler socket flags");
        perror("fcntl");
        goto err_res;
    }

    if (setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &(mcl_info->sndbuf), sizeof(uint64_t)))
    {
        eprintf("Error setting scheduler sending buffer");
        perror("setsockopt");
        goto err_res;
    }

    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &(mcl_info->rcvbuf), sizeof(uint64_t)))
    {
        eprintf("Error setting scheduler receiving buffer");
        perror("setsockopt");
        goto err_res;
    }

#if _DEBUG
    uint64_t r = 0, s = 0;
    socklen_t len = sizeof(uint64_t);

    if (getsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &s, &len))
    {
        eprintf("Error getting scheduler sended buffer");
        perror("getsockopt");
    }

    len = sizeof(uint64_t);

    if (getsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &r, &len))
    {
        eprintf("Error getting scheduler receiving buffer");
        perror("getsockopt");
    }

    Dprintf("Sending buffer set to %" PRIu64 " - Receiving buffer set to %" PRIu64 " ", s, r);

#endif

    memset(&saddr, 0, sizeof(struct sockaddr_un));
    saddr.sun_family = PF_UNIX;
    strncpy(saddr.sun_path, socket_name, sizeof(saddr.sun_path) - 1);
    Dprintf("Scheduler communication socket %s created", saddr.sun_path);

    if (bind(sock_fd, (struct sockaddr *)&saddr, sizeof(struct sockaddr_un)) < 0)
    {
        eprintf("Error binding communication socket.");
        perror("bind");
        goto err_socket;
    }
    Dprintf("Communication socket bound to %s", socket_name);

    if (sched_rdata_init())
    {
        eprintf("Error initializing rdata table.");
        goto err_socket;
    }

    close(shm_fd);

    return 0;

err_socket:
    close(sock_fd);
    unlink(socket_name);
err_res:
    free(mcl_res);
err_mmap:
    munmap(mcl_info, MCL_SHM_SIZE);
err_shm_fd:
    close(shm_fd);
    shm_unlink(shared_mem_name);
err:
    return -1;
}

int __shutdown(void)
{
    int ret = 0;

    close(sock_fd);
    unlink(socket_name);
    Dprintf("Communicaiton socket removed.");

    free(mcl_res);
#if 0
	//FIXME: clean up resource...
	for(i=0; i<mcl_info->nplts; i++)
		for(j=0; j<mcl_plts[i].ndev; j++)
			free(mcl_plts[i].cl_dev);
#endif
    free(mcl_plts);
    free(cl_plts);

    if (munmap(mcl_info, MCL_SHM_SIZE))
    {
        eprintf("Error unmapping Minos scheduler shared memory.");
        perror("munmap");
        ret = -1;
    }
    Dprintf("Shared memory object unmapped.");

    shm_unlink(shared_mem_name);
    Dprintf("Shared memory ojbect %s removed", shared_mem_name);

    return ret;
}

static void hdl(int sig)
{
    Dprintf("Signal cough by Minos scheduler. Exiting...");

    while (sched_queue_len())
        ;
    sched_done = 1;
}

static int sched_set_class(const char *sc)
{
    if (!strcmp(sc, "fifo"))
    {
        sched_curr = &fifo_class;
    }
    else if (!strcmp(sc, "fffs"))
    {
        sched_curr = &fffs_class;
    }
    else
    {
        return -1;
    }

    return 0;
}

static int sched_set_resource_policy(const char *policy)
{
    assert(sched_curr);

    if (!strcmp(policy, "ff"))
    {
        sched_curr->respol = &ff_policy;
    }
    else if (!strcmp(policy, "rr"))
    {
        sched_curr->respol = &rr_policy;
    }
    else if (!strcmp(policy, "delay"))
    {
        sched_curr->respol = &delay_policy;
    }
    else if (!strcmp(policy, "hybrid"))
    {
        sched_curr->respol = &hybrid_policy;
    }
    else
    {
        return -1;
    }

    return 0;
}

static int sched_set_eviction_policy(const char *policy)
{
    assert(sched_curr);

    if (!strcmp(policy, "lru"))
    {
        sched_curr->evictionpol = &lru_eviction_policy;
    }
    else
    {
        return -1;
    }

    return 0;
}

static void print_help(const char *prog)
{
    fprintf(stderr, "Usage: %s [options]\n"
                    "\t-s, --sched-class {fifo|fffs}  Select scheduler class (def = 'fifo')\n"
                    "\t-p, --res-policy {ff|rr|delay|hybrid|lws}  Select resource policy (def = class dependant)\n"
                    "\t-e, --evict_policy {lru}  Select eviction policy (def = lru)\n"
                    "\t-h, --help                     Show this help\n",
            prog);

    exit(EXIT_FAILURE);
}

static void parse_arguments(int argc, char *argv[])
{
    static struct option long_args[] = {
        {"help", no_argument, NULL, 'h'},
        {"sched-class", required_argument, NULL, 's'},
        {"res-policy", required_argument, NULL, 'p'},
        {"evict-policy", required_argument, NULL, 'e'},
    };

    const char *policy = NULL;
    const char *evict_policy = NULL;

    int opt;

    do
    {
        opt = getopt_long(argc, argv, "s:p:e:h", long_args, NULL);

        switch (opt)
        {
        case 's':
            if (sched_set_class(optarg) < 0)
            {
                fprintf(stderr, "parse_arguments: cannot find '%s' sched class.\n",
                        optarg);
                print_help(argv[0]);
            }
            else
                Dprintf("Set scheduler class: '%s'\n", optarg);
            break;
        case 'p':
            policy = optarg;
            break;
        case 'e':
            evict_policy = optarg;
            break;
        case 'h': /* fall through */
        case '?':
            print_help(argv[0]);
        default:
            break;
        };
    } while (opt != -1);

    /* all argument parsed */
    if (policy)
    {
        if (sched_set_resource_policy(policy) < 0)
        {
            fprintf(stderr, "parse_arguments: cannot find '%s' policy.\n",
                    policy);
            print_help(argv[0]);
        }
        else
            Dprintf("Set resource policy: '%s'\n", policy);
    }

    if (evict_policy)
    {
        if (sched_set_eviction_policy(evict_policy) < 0)
        {
            fprintf(stderr, "parse_arguments: cannot find '%s' eviction policy.\n",
                    evict_policy);
            print_help(argv[0]);
        }
        else
            Dprintf("Set eviction policy: '%s'\n", evict_policy);
    }
}

int main(int argc, char *argv[])
{
    struct sigaction act;

    Dprintf("Starting Minos scheduler...");

    parse_arguments(argc, argv);

    if (__setup())
    {
        eprintf("Error setting up Minos.");
        goto err;
    }

    memset(&act, '\0', sizeof(act));
    act.sa_handler = &hdl;
    if (sigaction(SIGINT, &act, NULL) < 0)
    {
        eprintf("Error setting up signal action.");
        goto err_setup;
    }

#ifdef _DEBUG
    resource_list(mcl_res, mcl_info->ndevs);
#endif

    if (sched_init(NULL))
    {
        Dprintf("Error initializing scheduling algorithm");
        goto err_setup;
    }

    sched_req_table = ph_init(1u << SCHED_REQ_TABLE_SIZE_SHIFT);

    if (!sched_req_table)
    {
        eprintf("Error setting up scheduler request table.");
        goto err_setup;
    }

    Dprintf("Request table initialized: mask=%08lx count=%lu",
            sched_req_table->mask, ph_count(sched_req_table));

    if (pthread_create(&rcv_tid, NULL, receiver, NULL))
    {
        eprintf("Error starting scheduling receiver thread.");
        goto err_sched;
    }

    if (schedule())
    {
        eprintf("Error executing scheduling algorithm!");
        goto err_sched;
    }

    Dprintf("Minos scheduler shutting down.");

    pthread_join(rcv_tid, NULL);
    Dprintf("Receiver thread terminated.");

    if (sched_finit())
    {
        Dprintf("Error finilizing FIFO scheduler");
        goto err_setup;
    }

    if (__shutdown())
    {
        eprintf("Error shutting down Minos scheduler.");
        goto err;
    }

    return 0;

err_sched:
    sched_finit();
err_setup:
    __shutdown();
err:
    return -1;
}
