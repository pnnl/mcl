#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#include <minos.h>
#include <minos_internal.h>
#include <atomics.h>
#include <stats.h>
#include <utlist.h>

mcl_desc_t mcl_desc;
uint32_t curr_h = 0;
uint32_t curr_mem_id = 0;
static uint64_t    curr_q[CL_MAX_DEVICES];
volatile uint64_t status = MCL_NONE;

mcl_request *hash_reqs = NULL;
mcl_rlist *ptasks = NULL;
pthread_rwlock_t ptasks_lock;

cl_platform_id *cl_plts = NULL;
mcl_platform_t *mcl_plts = NULL;
mcl_resource_t *mcl_res = NULL;
mcl_class_t *mcl_class = NULL;

char *shared_mem_name = NULL;

static inline int cli_msg_send(struct mcl_msg_struct *msg)
{
        int ret;

        while (mcl_desc.out_msg >= MCL_MSG_MAX)
                sched_yield();

        while (((ret = msg_send(msg, mcl_desc.sock_fd, &mcl_desc.saddr)) > 0))
                sched_yield();

        return ret;
}

static inline int cli_msg_recv(struct mcl_msg_struct *msg)
{
        struct sockaddr_un src;

        return msg_recv(msg, mcl_desc.sock_fd, &src);
}

static inline uint32_t get_rid(void)
{
        return ainc(&curr_h);
}

static inline uint32_t get_mem_id(void)
{
        return ainc(&curr_mem_id);
}

cl_command_queue __get_queue(uint64_t dev){
	uint64_t q_idx = curr_q[dev];
	curr_q[dev] = (curr_q[dev] + 1) % res_getDev(dev)->nqueues;
	return res_getClQueue(dev, q_idx);
}

static inline mcl_handle *hdl_init(uint64_t cmd, uint32_t rid, uint64_t status)
{
        mcl_handle *h = NULL;

        h = (mcl_handle *)malloc(sizeof(mcl_handle));
        if (!h)
                return NULL;

        h->cmd = cmd;
        h->rid = rid;
        h->status = status;
        h->ret = MCL_RET_UNDEFINED;

        return h;
}

mcl_handle *__task_create(void)
{
        mcl_handle *hdl = NULL;
        mcl_task *tsk = NULL;
        mcl_request *req = NULL;

        hdl = hdl_init(MSG_CMD_NEX, 0, MCL_REQ_ALLOCATED);
        if (!hdl)
        {
                eprintf("Error allocating MCL handle.");
                goto err;
        }

        tsk = (mcl_task *)malloc(sizeof(mcl_task));
        if (!tsk)
        {
                eprintf("Error allocating MCL task");
                goto err_hdl;
        }

        req = (mcl_request *)malloc(sizeof(mcl_request));
        if (!req)
        {
                eprintf("Error allocating MCL request");
                goto err_tsk;
        }

        hdl->rid = get_rid();
        memset((void *)tsk, 0, sizeof(mcl_task));
        memset((void *)req, 0, sizeof(mcl_request));

        req->hdl = hdl;
        req->tsk = tsk;

        if (rlist_add(&ptasks, &ptasks_lock, req))
        {
                eprintf("Error adding task %u to not actvie task list", hdl->rid);
                goto err_req;
        }

        Dprintf("Task %u created (req=%p)", hdl->rid, req);
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

mcl_transfer *__transfer_create(uint64_t nargs, uint64_t ncopies)
{
        mcl_transfer *t = (mcl_transfer *)malloc(sizeof(mcl_transfer));
        t->nargs = nargs;
        t->args = (void **)malloc(sizeof(void *) * nargs);
        t->sizes = (uint64_t *)malloc(sizeof(uint64_t) * nargs);
        t->flags = (uint64_t *)malloc(sizeof(uint64_t) * nargs);

        t->ncopies = ncopies;
        t->handles = (mcl_handle **)malloc(sizeof(mcl_handle *) * ncopies);

        return t;
}

mcl_handle *__transfer_hdl_create(uint64_t nargs)
{
        mcl_handle *h = __task_create();
        if (!h)
                return NULL;
        h->cmd = MSG_CMD_TRAN;

        mcl_request *r = NULL;
        mcl_task *t = NULL;

        r = rlist_search(&ptasks, &ptasks_lock, h->rid);
        if (!r)
        {
                goto err;
        }
        t = req_getTask(r);
        if (nargs)
        {
                t->args = (mcl_arg *)malloc(nargs * sizeof(mcl_arg));
                if (!(t->args))
                {
                        eprintf("Error allocating host memory for transfer");
                        goto err;
                }
        }
        t->nargs = nargs;
        return h;

err:
        return NULL;
}

int __set_kernel(mcl_handle *h, char *path, char *name, uint64_t nargs, char *copts,
                 unsigned long flags)
{
        mcl_request *r = NULL;
        mcl_task *t = NULL;
        unsigned int ret = 0;
        size_t klen = 0;

        r = rlist_search(&ptasks, &ptasks_lock, h->rid);
        if (!r)
        {
                ret = MCL_ERR_INVREQ;
                goto err;
        }

        t = req_getTask(r);
        t->prg = prgMap_add(path, copts);
        if (t->prg == NULL)
        {
                Dprintf("Error linking program %s to task %u. Aborting.", path, r->hdl->rid);
                ret = MCL_ERR_INVPRG;
                goto err;
        }

        klen = strlen(name);
        t->kname = (char *)malloc(sizeof(char) * klen + 1);
        if (t->kname == NULL)
        {
                Dprintf("Error allocating memory for kernel %s in task %u. Aborting.", name,
                        r->hdl->rid);
                ret = MCL_ERR_INVKER;
                goto err;
        }

        if (nargs)
        {
                t->args = (mcl_arg *)malloc(nargs * sizeof(mcl_arg));
                if (!(t->args))
                {
                        ret = MCL_ERR_MEMALLOC;
                        goto err_kname;
                }
        }
        t->nargs = nargs;
        strcpy(t->kname, name);
        memset((void *)(t->args), 0, nargs * sizeof(mcl_arg));

        return 0;

err_kname:
        free(t->kname);
err:
        return -ret;
}

mcl_handle *__task_init(char *path, char *name, uint64_t nargs, char *opts, unsigned long flags)
{
        mcl_handle *h = __task_create();

        if (!h)
                return NULL;

        if (__set_kernel(h, path, name, nargs, opts, flags))
                return NULL;

        return h;
}

int __set_arg(mcl_handle *h, uint64_t id, void *addr, size_t size, uint64_t flags)
{
        mcl_request *r = NULL;
        mcl_task *tsk = NULL;
        mcl_arg *a = NULL;

        r = rlist_search(&ptasks, &ptasks_lock, h->rid);
        if (!r)
                return -MCL_ERR_INVREQ;

        tsk = req_getTask(r);
        a = task_getArgAddr(tsk, id);

        if (flags & MCL_ARG_SCALAR)
        {
                a->addr = (void *)malloc(size);
                if (!(a->addr))
                        return -MCL_ERR_MEMALLOC;

                memcpy(a->addr, addr, size);
        }
        else
                a->addr = addr;

        a->size = size;
        a->flags = flags;

        tsk->mem += size;
        Dprintf("  Added argument %" PRIu64 " to task %" PRIu32 " (mem=%" PRIu64 ")", id, h->rid, tsk->mem);

        return 0;
}

int __am_exec(mcl_handle *hdl, mcl_request *req, uint64_t flags)
{
        mcl_task *t = req_getTask(req);
        struct mcl_msg_struct msg;
        int retcode = 0;
        mcl_arg *a;

        assert(hdl->status == MCL_REQ_ALLOCATED);
        msg_init(&msg);

        msg.cmd   = MSG_CMD_EXE;
        msg.rid   = hdl->rid;
        msg.pes   = t->tpes;
        msg.type  = flags & MCL_TASK_TYPE_MASK;
        msg.mem   = ceil((t->mem * (1 + MCL_TASK_OVERHEAD)) / MCL_PAGE_SIZE);
        msg.flags = (flags & MCL_TASK_FLAG_MASK) >> MCL_TASK_FLAG_SHIFT;
        msg.nres  = 0;

        for(int i=0; i<MCL_DEV_DIMS; i++){
                msg.pesdata.pes[i]  = t->pes[i];
                msg.pesdata.lpes[i] = t->lpes[i];
        }
        /*
         * It is most time efficient to allocate the greatest memory that could be needed, but likely it
         * is an overshoot
         */
        msg.resdata = malloc(t->nargs * sizeof(msg_arg_t));
        memset(msg.resdata, 0, t->nargs * sizeof(msg_arg_t));

        mcl_rdata *el;
        mcl_rdata *r;

        for (uint64_t i = 0; i < t->nargs; i++){
                a = &(t->args[i]);
                if ((a->flags & MCL_ARG_BUFFER) && (a->flags & MCL_ARG_INVALID) && (el = rdata_get(a->addr, -1))){
                        rdata_put(el);
                        mcl_msg free_msg;
                        msg_init(&free_msg);
                        free_msg.cmd = MSG_CMD_FREE;
                        free_msg.nres = 1;
                        free_msg.resdata = (msg_arg_t *)malloc(sizeof(msg_arg_t));
                        memset(free_msg.resdata, 0, sizeof(msg_arg_t));
                        free_msg.resdata[0].mem_id = el->id;
                        free_msg.resdata[0].mem_size = a->size;
                        if (cli_msg_send(&free_msg)){
                                eprintf("Error sending msg 0x%" PRIx64 ".", free_msg.cmd);
                                msg_free(&msg);
                                goto err;
                        }
                        msg_free(&free_msg);
                        Dprintf("\t Argument %" PRIu64 " free message sent", i);
                }

                if ((a->flags & MCL_ARG_BUFFER) && (a->flags & MCL_ARG_RESIDENT)){
                        if ((el = rdata_get(a->addr, -1))){
                                msg.resdata[msg.nres].mem_id = el->id;
                                rdata_put(el);
                                Dprintf("\t Argument %" PRIu64 " located on device %d", i, -1);
                        }else{
                                msg.resdata[msg.nres].mem_id = get_mem_id();
                                r = malloc(sizeof(mcl_rdata));
                                r->key.addr = (unsigned long)a->addr;
                                r->key.device = -1;
                                r->size = a->size;
                                r->id = msg.resdata[msg.nres].mem_id;
                                r->refs = 0;
                                rdata_add(r, 0);
                        }
                        msg.resdata[msg.nres].mem_size = a->size;
                        if (a->flags & MCL_ARG_DYNAMIC)
                                msg.resdata[msg.nres].flags = MSG_ARGFLAG_EXCLUSIVE;
                        msg.nres += 1;
                }
        }

        Dprintf("Sending EXE AM RID: %" PRIu64 " PEs: %" PRIu64 " MEM: %" PRIu64, msg.rid, msg.pes,
                msg.mem);
        ainc(&(mcl_desc.out_msg));
        cas(&(hdl->status), MCL_REQ_ALLOCATED, MCL_REQ_PENDING);
        assert(hdl->status == MCL_REQ_PENDING);
        stats_timestamp(hdl->stat_submit);
        if (cli_msg_send(&msg))
        {
                eprintf("Error sending msg 0x%" PRIx64, msg.cmd);
                retcode = MCL_ERR_SRVCOMM;
                goto err;
        }
        msg_free(&msg);

        return 0;

err:
        msg_free(&msg);
        cas(&(hdl->status), MCL_REQ_ALLOCATED, MCL_REQ_COMPLETED);
        assert(hdl->status == MCL_REQ_COMPLETED);
        hdl->ret = MCL_RET_ERROR;
        return -retcode;
}

int __am_null(mcl_request *req)
{
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

        if (cli_msg_send(&msg))
        {
                eprintf("Error sending msg 0x%" PRIx64, msg.cmd);
                goto err;
        }
        ainc(&(mcl_desc.out_msg));

        //Doesn't really do anything, nor wait for the scheduler...

        cas(&(hdl->status), MCL_REQ_ALLOCATED, MCL_REQ_COMPLETED);
        assert(hdl->status == MCL_REQ_COMPLETED);
        hdl->ret = MCL_RET_SUCCESS;

        adec(&(mcl_desc.out_msg));

        if (req_del(&hash_reqs, hdl->rid) == NULL)
        {
                eprintf("Error removing request %u from pending request table",
                        hdl->rid);
        }

        free(req->tsk);
        free(req);
        msg_free(&msg);

        return 0;

err:
        req_del(&hash_reqs, hdl->rid);
        free(req);
        msg_free(&msg);

        return -MCL_ERR_MEMALLOC;
}

int __internal_wait(mcl_handle *h, int blocking, uint64_t timeout)
{
        struct timespec start, now;

        /* This is s shortcut for the common case */
        if (h->status == MCL_REQ_COMPLETED)
                return 0;

        if (!blocking)
                return h->status;

        __get_time(&start);
        __get_time(&now);

        while (__diff_time_ns(now, start) < timeout && h->status != MCL_REQ_COMPLETED)
        {
                sched_yield();
                __get_time(&now);
        }

        if (h->status == MCL_REQ_COMPLETED)
        {
                Dprintf("Request %u completed.", h->rid);
                return 0;
        }

        eprintf("Timeout occurred, blocking for more than %f s!", __nstos(MCL_TIMEOUT));

        return -1;
}

int cli_register(void)
{
        struct mcl_msg_struct msg;
        int ret;

        msg_init(&msg);
        msg.cmd = MSG_CMD_REG;
        msg.rid = get_rid();

        if (cli_msg_send(&msg))
        {
                eprintf("Error sending msg 0x%" PRIx64, msg.cmd);
                goto err;
        }
        msg_free(&msg);
        ainc(&(mcl_desc.out_msg));

        Dprintf("Waiting for scheduler to accept registration request...");

        while ((ret = cli_msg_recv(&msg)) >= 0)
        {
                if (ret == 0)
                        break;
                sched_yield();
        }

        if (msg.cmd == MSG_CMD_ERR)
        {
                eprintf("Client registration failed!");
                goto err;
        }
        adec(&(mcl_desc.out_msg));
        Dprintf("Client registration confirmed.");
        msg_free(&msg);
        return 0;

err:
        msg_free(&msg);
        return -1;
}

int cli_deregister(void)
{
        struct mcl_msg_struct msg;

        msg_init(&msg);
        msg.cmd = MSG_CMD_END;
        msg.rid = get_rid();

        if (cli_msg_send(&msg))
        {
                eprintf("Error sending msg 0x%" PRIx64 ".", msg.cmd);
                goto err;
        }
        msg_free(&msg);

        //Wait for request to be accepted...

        return 0;

err:
        msg_free(&msg);
        return -1;
}

int cli_setup(void)
{

        if ((shared_mem_name = getenv("MCL_SHM_NAME")) == NULL)
        {
                shared_mem_name = MCL_SHM_NAME;
        }
        Dprintf("Opening shared memory object (%s)...", shared_mem_name);
        mcl_desc.shm_fd = shm_open(shared_mem_name, O_RDONLY, 0);
        if (mcl_desc.shm_fd < 0)
        {
                eprintf("Error opening shared memory object %s.", shared_mem_name);
                perror("shm_open");
                goto err;
        }

        mcl_desc.info = (mcl_info_t *)mmap(NULL, MCL_SHM_SIZE, PROT_READ, MAP_SHARED,
                                           mcl_desc.shm_fd, 0);
        if (!mcl_desc.info)
        {
                eprintf("Error mapping shared memory object.");
                perror("mmap");
                goto err_shm_fd;
        }

        Dprintf("Shared memory ojbect mapped at address %p", mcl_desc.info);
        close(mcl_desc.shm_fd);

        const char *socket_name;
        if ((socket_name = getenv("MCL_SOCK_NAME")) == NULL)
        {
                socket_name = MCL_SOCK_NAME;
        }
        memset(&mcl_desc.saddr, 0, sizeof(struct sockaddr_un));
        mcl_desc.saddr.sun_family = PF_UNIX;
        strncpy(mcl_desc.saddr.sun_path, socket_name, sizeof(mcl_desc.saddr.sun_path) - 1);

        mcl_desc.sock_fd = socket(PF_LOCAL, SOCK_DGRAM, 0);
        if (mcl_desc.sock_fd < 0)
        {
                eprintf("Error creating communication socket");
                perror("socket");
                goto err_shm_fd;
        }

        if (fcntl(mcl_desc.sock_fd, F_SETFL, O_NONBLOCK))
        {
                eprintf("Error setting scheduler socket flags");
                perror("fcntl");
                goto err_shm_fd;
        }

        if (setsockopt(mcl_desc.sock_fd, SOL_SOCKET, SO_SNDBUF, &(mcl_desc.info->sndbuf),
                       sizeof(uint64_t)))
        {
                eprintf("Error setting scheduler sending buffer");
                perror("setsockopt");
                goto err_shm_fd;
        }

        if (setsockopt(mcl_desc.sock_fd, SOL_SOCKET, SO_RCVBUF, &(mcl_desc.info->rcvbuf),
                       sizeof(uint64_t)))
        {
                eprintf("Error setting scheduler receiving buffer");
                perror("setsockopt");
                goto err_shm_fd;
        }

#if _DEBUG
        uint64_t r = 0, s = 0;
        socklen_t len = sizeof(uint64_t);

        if (getsockopt(mcl_desc.sock_fd, SOL_SOCKET, SO_SNDBUF, &s, &len))
        {
                eprintf("Error getting scheduler sended buffer");
                perror("getsockopt");
        }

        len = sizeof(uint64_t);

        if (getsockopt(mcl_desc.sock_fd, SOL_SOCKET, SO_RCVBUF, &r, &len))
        {
                eprintf("Error getting scheduler receiving buffer");
                perror("getsockopt");
        }

        Dprintf("Sending buffer set to %" PRIu64 " - Receiving buffer set to %" PRIu64 " ", s, r);
#endif

        const char *client_format;
        if ((client_format = getenv("MCL_SOCK_CNAME")) == NULL)
        {
                client_format = MCL_SOCK_CNAME;
        }
        memset(&mcl_desc.caddr, 0, sizeof(struct sockaddr_un));
        mcl_desc.caddr.sun_family = PF_UNIX;
        snprintf(mcl_desc.caddr.sun_path, sizeof(mcl_desc.caddr.sun_path),
                 client_format, (long)mcl_desc.pid);

        Dprintf("Client communication socket %s created", mcl_desc.caddr.sun_path);

        if (bind(mcl_desc.sock_fd, (struct sockaddr *)&mcl_desc.caddr,
                 sizeof(struct sockaddr_un)) < 0)
        {
                eprintf("Error binding client communication socket %s.", mcl_desc.caddr.sun_path);
                perror("bind");
                goto err_socket;
        }
        Dprintf("Client communication socket bound to %s", mcl_desc.caddr.sun_path);

        if (resource_discover(&cl_plts, &mcl_plts, &mcl_class, NULL, NULL))
        {
                eprintf("Error discovering computing elements.");
                goto err_socket;
        }

        mcl_res = (mcl_resource_t *)malloc(mcl_desc.info->ndevs * sizeof(mcl_resource_t));
        if (!mcl_res)
        {
                eprintf("Error allocating memory to map MCL resources.");
                goto err_socket;
        }

        if (resource_map(mcl_plts, mcl_desc.info->nplts, mcl_class, mcl_res))
        {
                eprintf("Error mapping devices to MCL resources.");
                goto err_res;
        }

        if (resource_create_ctxt(mcl_res, mcl_desc.info->ndevs))
        {
                eprintf("Error creating resource contexts.");
                goto err_res;
        }

        Dprintf("Discovered %" PRIu64 " platforms and a total of %" PRIu64 " devices",
                mcl_desc.info->nplts, mcl_desc.info->ndevs);

        if (req_init())
        {
                eprintf("Error initializing request hash table");
                goto err_res;
        }

        if (rlist_init(&ptasks_lock))
        {
                eprintf("Error initializing not active task list lock");
                goto err_res;
        }

        if (rdata_init())
        {
                eprintf("Error initializing resident data hash table");
                goto err_res;
        }

        if (msg_setup(mcl_desc.info->ndevs))
        {
                eprintf("Error setting up messages");
                goto err_rdata;
        }

        if (cli_register())
        {
                eprintf("Error registering process %d.", mcl_desc.pid);
                goto err_rdata;
        }

        return 0;

err_rdata:
        rdata_free();
err_res:
        free(mcl_res);
err_socket:
        close(mcl_desc.sock_fd);
        unlink(mcl_desc.caddr.sun_path);
err_shm_fd:
        close(mcl_desc.shm_fd);
        shm_unlink(shared_mem_name);
err:
        return -1;
}

static inline int cli_shutdown_dev(void)
{
#ifdef _STATS
        mcl_device_t *dev;
        int i;
#endif
        Dprintf("Shutting down devices...");
#ifdef _STATS
        for (i = 0; i < mcl_desc.info->ndevs; i++)
        {
                dev = mcl_res[i].dev;
                stprintf("Resource[%d] Task Executed=%" PRIu64 " Successful=%" PRIu64 " Failed=%" PRIu64 "",
                         i, dev->task_executed, dev->task_successful, dev->task_failed);
        }
#endif
        return 0;
}

int cli_shutdown(void)
{
        rdata_free();

        if (cli_shutdown_dev())
        {
                eprintf("Error shutting down devices...");
                goto err;
        }

        if (cli_deregister())
                eprintf("Error de-registering process %d", mcl_desc.pid);

        close(mcl_desc.sock_fd);
        unlink(mcl_desc.caddr.sun_path);

        if (munmap(mcl_desc.info, MCL_SHM_SIZE))
        {
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
        //Cleanup better the devices in mcl_plts
        free(mcl_plts);
        free(cl_plts);
        return 0;

err:
        return -1;
}

void CL_CALLBACK __move_complete(cl_event read_e, cl_int status, void *args)
{
        cl_event *write_e = ((void **)args)[0];
        mcl_rdata *old = ((void **)args)[1];

        cl_int ret = clSetUserEventStatus(*write_e, CL_COMPLETE);
        Dprintf("Move executed: address %lu transfered from device %" PRId64 "", old->key.addr, old->key.device);
        if (ret != CL_SUCCESS)
        {
                eprintf("Error setting user event status: %d", ret);
                /** FIXME: Not sure how to handle error in callback **/
        }

        clReleaseMemObject(old->clBuffer);
        free(old);
        free(args);
}

static inline int __task_setup_buffers(mcl_request *r)
{
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
        mcl_rdata *old = NULL;

        if (!context->buffers){
                eprintf("Error allocating OpenCL object buffers.");
                return -MCL_ERR_MEMALLOC;
        }

        for (i = 0; i < t->nargs; i++){
                a = &(t->args[i]);
                a->new_buffer = 1;
                a->moved_data = 0;

                Dprintf("  Setting up arg %d addr %p size %lu flags 0x%" PRIx64 "", i, a->addr, a->size, a->flags);

                if ((a->flags & MCL_ARG_BUFFER) && !(a->flags & MCL_ARG_LOCAL)){
                        if (a->flags & MCL_ARG_INVALID){
                                while (rdata_mv(a->addr, -1, NULL) > 0){
                                        ;
                                }
                                Dprintf("All previous instances of <%p,%" PRIu64 "> have been removed", a->addr, r->res);
                        }

                        if (a->flags & MCL_ARG_RESIDENT){
                                a->rdata_el = rdata_get(a->addr, r->res);
                                if (a->rdata_el != NULL){
                                        Dprintf("\t Resident data <%p,%" PRIu64 "> located",
                                                (void *)(a->rdata_el->key.addr),
                                                a->rdata_el->key.device);
                                        context->buffers[i] = a->rdata_el->clBuffer;
                                        a->new_buffer = 0;
                                }else
                                        Dprintf("\t Resident data <%p,%" PRIu64 "> not located", a->addr, r->res);
                        }

                        if (a->new_buffer){
                                Dprintf("\t Creating buffer...");
#ifdef _DEBUG
                                __get_time(&start);
#endif
                                if (a->flags & MCL_ARG_RDONLY)
                                        flags = CL_MEM_READ_ONLY;
                                else if (a->flags & MCL_ARG_WRONLY)
                                        flags = CL_MEM_WRITE_ONLY;
                                else
                                        flags = CL_MEM_READ_WRITE;

                                context->buffers[i] = clCreateBuffer(ctxt, flags, a->size, NULL, &ret);

                                if (ret != CL_SUCCESS){
                                        eprintf("Error creating input OpenCL buffer %d (%d).", i, ret);
                                        retcode = MCL_ERR_MEMALLOC;
                                        goto err;
                                }

                                if (a->flags & MCL_ARG_DYNAMIC){
                                        old = NULL;
                                        /**FIXME: This will block till all other references to the data have completed*/
                                        rdata_mv(a->addr, r->res, &old);
                                        if (old){
                                                cl_command_queue src_queue = __get_queue(old->key.device);
                                                ret = clEnqueueReadBuffer(src_queue, old->clBuffer, CL_TRUE, 0,
                                                                          a->size, a->addr, 0, NULL, NULL);
                                                if (ret != CL_SUCCESS){
                                                        eprintf("Error for arg %d, OpenCL device %d.", i, ret);
                                                        retcode = MCL_ERR_MEMCOPY;
                                                        goto err1;
                                                }
     
                                                ret = clEnqueueWriteBuffer(queue, context->buffers[i], CL_TRUE, 0,
                                                                           a->size, a->addr, 0, NULL, NULL);
                                                if (ret != CL_SUCCESS){
                                                        eprintf("Error for arg %d, OpenCL device %d.", i, ret);
                                                        retcode = MCL_ERR_MEMCOPY;
                                                        goto err1;
                                                }

                                                stats_add(r->worker->bytes_transfered, (2 * a->size));
                                                stats_inc(r->worker->n_transfers);
                                                stats_inc(r->worker->n_transfers);
                                                Dprintf("Done incrementing stats.");
                                        }
                                }
                                if ((a->flags & MCL_ARG_INPUT) && !a->moved_data){
                                        Dprintf("    Writing data to buffer...");
                                        ret = clEnqueueWriteBuffer(queue, context->buffers[i], CL_TRUE, 0,
                                                                   a->size, a->addr, 0, NULL, NULL);
                                        stats_add(r->worker->bytes_transfered, (a->size));
                                        stats_inc(r->worker->n_transfers);
                                }else if (a->flags & MCL_ARG_OUTPUT && !a->moved_data){
                                        /* Output only area: memset to 0
					 * NOTE: this could probably be done faster by using
					 *       pattern_size greater than 1, it would require proper
					 *       computations to handle sizes that are not multiple of
					 *       pattern size.
					 */
                                        // uint32_t __zero_pattern = 0u;
                                        // Dprintf("\t\t Zeroing the buffer...");
                                        // ret = clEnqueueFillBuffer(queue, context->buffers[i], &__zero_pattern,
                                        //                           1, 0, a->size, 0, NULL, NULL);
                                }

                                if (ret != CL_SUCCESS){
                                        eprintf("Error for arg %d, OpenCL device %d.", i, ret);
                                        retcode = MCL_ERR_MEMCOPY;
                                        goto err1;
                                }

                                if (a->flags & MCL_ARG_RESIDENT){
                                        mcl_rdata *res_arg;
                                        Dprintf("\t\t Adding resident argument to hash table");

                                        res_arg = (mcl_rdata *)malloc(sizeof(mcl_rdata));
                                        if (res_arg == NULL)
                                        {
                                                eprintf("Error allocating memory for resident data hash table entry!");
                                                ret = 1;
                                                continue;
                                        }
                                        memset(res_arg, 0, sizeof(mcl_rdata));
                                        res_arg->key.addr = (unsigned long)(a->addr);
                                        res_arg->key.device = r->res;
                                        res_arg->clBuffer = context->buffers[i];
                                        res_arg->flags = a->flags;
                                        res_arg->size = a->size;
                                        res_arg->refs = 0;
                                        ret = rdata_add(res_arg, 1);
                                        switch (ret)
                                        {
                                        case 0:
                                                // New buffer successfully added, nothing to do
                                                a->rdata_el = res_arg;
                                                break;
                                        case 1:
                                                // Someone added the new buffer just before us (concurrent update),
                                                // give up and use that buffer.
                                                a->rdata_el = rdata_get(a->addr, r->res);
                                                if (a->rdata_el)
                                                {
                                                        free(res_arg);
                                                        clReleaseMemObject(context->buffers[i]);
                                                        context->buffers[i] = a->rdata_el->clBuffer;
                                                        break;
                                                }
                                        case -1:
                                                // Error case
                                        default:
                                                free(res_arg);
                                                eprintf("Error adding buffer to resident data hash table");
                                                ret = 1;
                                        }
                                }

#ifdef _DEBUG
                                __get_time(&end);
#endif
                                Dprintf("  Time to create buffer: %lld", __diff_time_ns(end, start));
                        }
                        arg_size = sizeof(cl_mem);
                        arg_value = (void *)&(context->buffers[i]);
                }
                else if (a->flags & MCL_ARG_LOCAL)
                {
                        if (a->addr != NULL)
                        {
                                eprintf("Error, argument address must be NULL for local memory");
                                retcode = MCL_ERR_INVARG;
                                goto err1;
                        }
                        context->buffers[i] = NULL;
                        arg_size = a->size;
                        arg_value = (void *)NULL;
                }
                else if (a->flags & MCL_ARG_SCALAR)
                {
                        context->buffers[i] = NULL;
                        arg_size = a->size;
                        arg_value = (void *)a->addr;
                }
                else
                {
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

                if (ret != CL_SUCCESS)
                {
                        eprintf("Error setting OpenCL argument %d (%d).", i, ret);
                        retcode = MCL_ERR_INVARG;
                        goto err;
                }
        }
        return 0;
err1:
        /** Rollback buffer allocations, FIXME: this needs to check for resident data **/
        while (i)
                clReleaseMemObject(context->buffers[i--]);
err:
        free(context->buffers);
        context->buffers = NULL;
        return -retcode;
}

static inline int __save_program(mcl_pobj *obj, cl_program prg)
{
        cl_int err;

        Dprintf("  Saving program...");

        err = clGetProgramInfo(prg, CL_PROGRAM_BINARY_SIZES,
                               sizeof(size_t),
                               &(obj->binary_len), NULL);

        if (err != CL_SUCCESS)
        {
                eprintf("Error quiering program binary size (%d)", err);
                return -1;
        }

        Dprintf("    Binary size %lu", obj->binary_len);
        obj->binary = (unsigned char *)malloc(sizeof(unsigned char) * (obj->binary_len));
        err = clGetProgramInfo(prg, CL_PROGRAM_BINARIES, sizeof(char *),
                               &(obj->binary), NULL);

        if (err != CL_SUCCESS)
        {
                eprintf("Error quiering program binary (%d)", err);
                return -1;
        }

        obj->status = MCL_KER_BUILT;

        return 0;
}

static inline mcl_pobj *__build_program(mcl_task *t, mcl_program *p, unsigned int n)
{
        mcl_context *ctx = task_getCtxAddr(t);
        mcl_pobj *obj = prg_getObj(p, n);
        cl_context clctx = res_getClCtx(n);
        cl_device_id dev = res_getClDev(n);
        cl_int ret = 0;
#ifdef _DEBUG
        struct timespec start, end;
#endif

        Dprintf("Buiding program %s for resource %u...", p->path, n);

#ifdef _DEBUG
        __get_time(&start);
#endif
        if (cas(&(obj->status), MCL_KER_NONE, MCL_KER_COMPILING))
        {

                Dprintf("  Building program %s for device %u...", p->key, n);
                Dprintf("  Loading program source (%lu bytes)...", p->src_len);
                VDprintf("  Program source: \n %s", p->src_len);

                ctx->prg = clCreateProgramWithSource(clctx, 1, (const char **)&(p->src),
                                                     (const size_t *)&(p->src_len), &ret);
                if (ret != CL_SUCCESS)
                {
                        eprintf("Error loading CL program source (ret = %d)", ret);
                        goto err;
                }

                Dprintf("  Compiling program for resources %u (options %s)...", n, p->opts);

                ret = clBuildProgram(ctx->prg, 1, &dev, p->opts, NULL, NULL);
                if (ret != CL_SUCCESS)
                {
                        eprintf("Error compiling CL program for resources %u (ret = %d). \n", n, ret);
                        size_t log_size;
                        clGetProgramBuildInfo(ctx->prg, dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
                        char *log = (char *)malloc(log_size);
                        clGetProgramBuildInfo(ctx->prg, dev, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
                        eprintf("%s\n", log);
                        goto err_prg;
                }

                obj->cl_prg = ctx->prg;
                cas(&(obj->status), MCL_KER_COMPILING, MCL_KER_BUILT);
        }
        else
        {
                while (obj->status == MCL_KER_COMPILING)
                        sched_yield();

                assert(obj->status == MCL_KER_BUILT);
                Dprintf("  Program %s has been already built for device %u.",
                        p->key, n);
                ctx->prg = obj->cl_prg;
        }

#ifdef _DEBUG
        __get_time(&end);
#endif
        Dprintf("  Compile time: %lld", __diff_time_ns(end, start));
        Dprintf("  Creating kernel %s...", t->kname);
        ctx->kernel = clCreateKernel(ctx->prg, (const char *)(t->kname), &ret);
        if (ret != CL_SUCCESS)
        {
                eprintf("Error creating kernel %s (ret = %d)",
                        t->kname, ret);
                goto err_prg;
        }

        return obj;

err_prg:
        clReleaseProgram(ctx->prg);
err:
        obj->status = MCL_KER_ERR;
        return NULL;
}

static inline int __task_setup(mcl_request *r)
{
        cl_int ret;
        mcl_task *t = req_getTask(r);
        mcl_program *p = task_getPrg(t);
        mcl_context *ctx = task_getCtxAddr(t);
        int retcode = 0;
        size_t kwgroup;
        mcl_pobj *obj;

        stats_timestamp(r->hdl->stat_setup);

        if (r->hdl->cmd != MSG_CMD_TRAN){
                obj = __build_program(t, p, r->res);
                if (!obj){
                        retcode = MCL_ERR_INVPRG;
                        goto err;
                }

                /*
        * Determine workgroup size automatically if the user hasn't explicitely set 
        * them (lpes[0] = lpes[1] = lpes[2] = 0
        */
                if (t->lpes[0] == 0)
                {
                        ret = clGetKernelWorkGroupInfo(ctx->kernel, mcl_res[r->res].dev->cl_dev,
                                                       CL_KERNEL_WORK_GROUP_SIZE,
                                                       sizeof(size_t), &kwgroup, NULL);
                        if (ret != CL_SUCCESS)
                        {
                                eprintf("Error querying the kernel work group size! (%d)\n", ret);
                                retcode = MCL_ERR_INVTSK;
                                goto err;
                        }
                        Dprintf("Setting local dimensions, kernel work group size = %lu", kwgroup);
                        switch (t->dims){
                        case 1:
                                t->lpes[0] = t->pes[0] < kwgroup ? t->pes[0] : kwgroup;
                                t->lpes[1] = t->lpes[2] = 1;
                                break;
                        case 2:
                                kwgroup = (size_t)sqrt((double)kwgroup);
                                t->lpes[0] = t->pes[0] < kwgroup ? t->pes[0] : kwgroup;
                                t->lpes[1] = t->pes[1] < kwgroup ? t->pes[1] : kwgroup;
                                t->lpes[2] = 1;
                                break;
                        case 3:
                                kwgroup = (size_t)pow((double)kwgroup, (1.0 / 3.0));
                                t->lpes[0] = t->pes[0] < kwgroup ? t->pes[0] : kwgroup;
                                t->lpes[1] = t->pes[1] < kwgroup ? t->pes[1] : kwgroup;
                                t->lpes[2] = t->pes[2] < kwgroup ? t->pes[2] : kwgroup;
                                break;
                        default:
                                eprintf("Number of dimensions > 3!");
                                retcode = MCL_ERR_INVTSK;
                                goto err;
                        }
                }
        }

        Dprintf("Preparting input and output arguments (%" PRIu64
                " arguments)...",
                t->nargs);

        retcode = __task_setup_buffers(r);
        if (retcode < 0){
                retcode = -retcode;
                goto err;
        }

        return 0;

err:
        return -retcode;
}

static inline int __task_release(mcl_request *r)
{
        mcl_task *t = req_getTask(r);
        mcl_context *ctx = task_getCtxAddr(t);
        int i;
        int ret = 0;
        Dprintf("Task %u Removing %" PRIu64 " arguemnts", req_getHdl(r)->rid, t->nargs);

        for (i = 0; i < t->nargs; i++){
                Dprintf("\t Removing argument %d: ADDR: %p SIZE: %lu FLAGS: 0x%" PRIx64 " ", i,
                        t->args[i].addr, t->args[i].size, t->args[i].flags);

                if (t->args[i].flags & MCL_ARG_SCALAR){
                        free(t->args[i].addr);
                        Dprintf("\t\t Scalar arguement removed");
                        continue;
                }

                if ((t->args[i].flags & MCL_ARG_DONE) && (t->args[i].flags & MCL_ARG_RESIDENT)){
                        rdata_put(t->args[i].rdata_el);
                        mcl_rdata *el = rdata_get((void *)t->args[i].rdata_el->key.addr, -1);

                        mcl_msg free_msg;
                        msg_init(&free_msg);
                        free_msg.cmd = MSG_CMD_FREE;
                        free_msg.nres = 1;
                        free_msg.resdata = (msg_arg_t *)malloc(sizeof(msg_arg_t));
                        memset(free_msg.resdata, 0, sizeof(msg_arg_t));
                        free_msg.resdata[0].mem_id = el->id;
                        free_msg.resdata[0].mem_size = t->args[i].size;
                        if (cli_msg_send(&free_msg)){
                                eprintf("Error sending msg 0x%" PRIx64 ".", free_msg.cmd);
                                msg_free(&free_msg);
                                ret = MCL_ERR_SRVCOMM;
                                continue;
                        }
                        msg_free(&free_msg);
                        rdata_put(el);

                        Dprintf("\t Argument %d free message sent", i);
                        rdata_rm((void *)t->args[i].rdata_el->key.addr);
                        continue;
                }else if ((t->args[i].flags & MCL_ARG_RESIDENT)){
                        rdata_put(t->args[i].rdata_el);
                        continue;
                }

                if (ctx->buffers[i] != NULL){
                        clReleaseMemObject(ctx->buffers[i]);
                        continue;
                }
        }

        if (r->hdl->cmd != MSG_CMD_TRAN)
                clReleaseKernel(ctx->kernel);

        clReleaseEvent(ctx->event);
        free(t->args);
        free(t);

        return ret;
}

static inline int cli_exec_am(struct worker_struct *desc, struct mcl_msg_struct *msg)
{
        mcl_request *r = NULL;
        mcl_handle *h = NULL;
        mcl_task *t = NULL;
        mcl_context *ctx;
        mcl_msg ack;
        cl_int ret = CL_SUCCESS;
        int i;
        cl_command_queue queue;
        int retcode = 0;

        adec(&(mcl_desc.out_msg));
        msg_init(&ack);

        VDprintf("  Searching request %" PRIu64 "", msg->rid);
        r = req_search(&hash_reqs, msg->rid);
        if (!r){
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

        cas(&(h->status), MCL_REQ_PENDING, MCL_REQ_INPROGRESS);
        if (h->status != MCL_REQ_INPROGRESS){
                eprintf("Request (RID=%u), status incorrect. Status: %" PRIu64 "!", h->rid, h->status);
                goto err_req;
        }

        if (__task_setup(r)){
                eprintf("Error setting up task for execution (RID=%u)!", h->rid);
                retcode = MCL_ERR_INVTSK;
                goto err_req;
        }
        
        if (h->cmd != MSG_CMD_TRAN){
                Dprintf("Executing Task RID=%u RES=%" PRIu64 " K=%s DIMS=%u "
                        "PEs=[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "] "
                        "LPEs=[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]",
                        h->rid, r->res, t->kname, t->dims,
                        t->pes[0], t->pes[1], t->pes[2],
                        t->lpes[0], t->lpes[1], t->lpes[2]);
        }

        stats_timestamp(h->stat_exec_start);

        cl_event kernel_event;
        if(h->cmd != MSG_CMD_TRAN){
                ret = clEnqueueNDRangeKernel(queue, ctx->kernel, t->dims, NULL,
                                (size_t*) t->pes, t->lpes[0] == 0 ? NULL : (size_t*) t->lpes,
                                0, NULL, &kernel_event);
        } else {
                        // Wait for all enqueued events (write events)
                ret = clEnqueueMarkerWithWaitList(queue, 0, NULL, &kernel_event);
        }


        if (ret != CL_SUCCESS){
                retcode = MCL_ERR_EXEC;
                Dprintf("Error executing task %u (%d)", h->rid, ret);
                if (h->cmd == MSG_CMD_TRAN)
                        goto err_setup;
                goto err_kernel;
        }


        int buffer_num = 0;
        cl_event* read_events = malloc(sizeof(cl_event) * t->nargs);
        for(i=0; i<t->nargs; i++) {
		if(t->args[i].flags & MCL_ARG_OUTPUT){
			ret = clEnqueueReadBuffer(queue, ctx->buffers[i],
						CL_FALSE, 0, t->args[i].size,
						t->args[i].addr, 0,
						NULL, &read_events[buffer_num++]);
			if(ret != CL_SUCCESS){
                                retcode = MCL_ERR_MEMCOPY;
                                if(h->cmd == MSG_CMD_TRAN) goto err_setup;
				goto err_kernel;
                        
			}
                        stats_add(r->worker->bytes_transfered, (t->args[i].size));
                        stats_inc(r->worker->n_transfers);
		}
        }

/* 
*  Set the waiting event as the last output argument (if any) or as the kernel itself (if no
*  argument is provide or if this is a transfer command)
*/
        if(buffer_num >= 1){
		ctx->event = read_events[buffer_num - 1];
		for(i = 0; i < buffer_num-1; i++) {
			clReleaseEvent(read_events[i]);
		}
		clReleaseEvent(kernel_event);
        } else{
                ctx->event = kernel_event;
        }

#ifdef _EVENTS
        ret = clSetEventCallback(ctx->event, CL_COMPLETE, __task_complete, r);
        if (ret != CL_SUCCESS){
                retcode = MCL_ERR_EXEC;
                if (h->cmd == MSG_CMD_TRAN)
                        goto err_setup;
                goto err_kernel;
        }
        free(read_events);
        ainc(&desc->ntasks);
                Dprintf("Added task %u to queue (total: %ld)", h->rid, desc->ntasks);
#else
        ret = clEnqueueMarkerWithWaitList(queue, buffer_num, read_events, &(ctx->event));

        assert(rlist_add(&(desc->ftasks), &(desc->ftasks_lock), r) == 0);
        ainc(&(desc->ntasks));
//        ret = sem_post(desc->pending_tasks);
//        if(ret)
//                perror("Task semaphore returned an error");
        Dprintf("Added task %u to queue (total: %ld, ret: %d)", h->rid,  desc->ntasks, ret);
#endif

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
        req_del(&hash_reqs, h->rid);
        cas(&(h->status), MCL_REQ_INPROGRESS, MCL_REQ_COMPLETED);
        assert(h->status == MCL_REQ_COMPLETED);
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

#ifdef _EVENTS
void CL_CALLBACK __task_complete(cl_event e, cl_int status, void *v_request)
{
        mcl_request *r = (mcl_request *)v_request;
        struct worker_struct *desc = r->worker;

        rlist_add(&(desc->ftasks), &desc->ftasks_lock, r);
        stats_timestamp(r->hdl->stat_exec_end);
        Dprintf("Executed task callback for rid: %" PRIu32 "", r->hdl->rid);
        sem_post(desc->pending_tasks);
}
#endif

static inline int __check_task(mcl_request *r)
{
        mcl_handle*      h = req_getHdl(r);
	mcl_task*        t = req_getTask(r);
        mcl_context*     ctx = task_getCtxAddr(t);
	cl_command_queue queue = __get_queue(r->res);
        mcl_msg          ack;
        int              retcode = 0;
	cl_int           eventStatus = CL_SUBMITTED;
	int              attempts = MCL_TASK_CHECK_ATTEMPTS;

        assert(h->status == MCL_REQ_INPROGRESS);
        Dprintf("\t Checking task %u", r->hdl->rid);

	while(eventStatus != CL_COMPLETE && attempts){
		sched_yield();
		
		clFlush(queue);		
		retcode = clGetEventInfo(ctx->event, CL_EVENT_COMMAND_EXECUTION_STATUS,
				     sizeof(cl_int), &eventStatus, NULL );
		
		if(retcode != CL_SUCCESS){
			retcode = MCL_ERR_EXEC;
                        return retcode;
		}
		attempts--;
	}

	if(eventStatus != CL_COMPLETE)
		return 1;

        cas(&(h->status), MCL_REQ_INPROGRESS, MCL_REQ_FINISHING);
        assert(h->status == MCL_REQ_FINISHING);

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

 
        if (req_del(&hash_reqs, h->rid))
                free(r);

        cas(&(h->status), MCL_REQ_FINISHING, MCL_REQ_COMPLETED);
        assert(h->status == MCL_REQ_COMPLETED);

        return -retcode;
}

static inline void *check_pending(void *data)
{
        mcl_rlist *li;
        mcl_request *r;
        struct worker_struct *desc = (struct worker_struct *)data;
        int ret;
#ifdef _DEBUG
        struct timespec last, now;

        __get_time(&last);
#endif

        while (status != MCL_DONE || rlist_count(&desc->ftasks, &desc->ftasks_lock) > 0){                
//                Dprintf("Worker %llu checking (pending tasks=%d)....", desc->id, rlist_count(&desc->ftasks, &desc->ftasks_lock));
//                ret = sem_wait(desc->pending_tasks);
//                assert(ret == 0);
#if 0
                int count;
                sem_getvalue(desc->pending_tasks, &count);
                if(count)
                        Dprintf("\t Sem value %d", count);
#endif
                li = NULL;
                r = NULL;
#ifndef _EVENTS
                li = rlist_pop(&(desc->ftasks), &(desc->ftasks_lock));
#else
                cl_int eventStatus = CL_SUBMITTED;
                while (rlist_count(&desc->ftasks, &desc->ftasks_lock) && eventStatus != CL_COMPLETE){
                        li = rlist_pop(&(desc->ftasks), &desc->ftasks_lock);
                        if (li == NULL)
                                break;
                        for (int i = 0; i < MCL_TASK_CHECK_ATTEMPTS; i++){
                                if (li->req->hdl->cmd == MSG_CMD_TRAN){
                                        eventStatus = CL_COMPLETE;
                                        break;
                                }
                                clGetEventInfo(li->req->tsk->ctx.event, CL_EVENT_COMMAND_EXECUTION_STATUS,
                                               sizeof(cl_int), &eventStatus, NULL);
                                if (eventStatus == CL_COMPLETE)
                                        break;
                        }
                        if (eventStatus != CL_COMPLETE)
                                rlist_append(&desc->ftasks, &desc->ftasks_lock, li);
                }
#endif
                if (li == NULL)
                        continue;
                r = li->req;
                ret = __check_task(r);
		if(ret > 0){
			assert(rlist_append(&(desc->ftasks), &(desc->ftasks_lock), li) == 0);
		}
		else if(ret == 0){
  //                      Dprintf("Removing %u", r->key);
			if(req_del(&hash_reqs, r->key))
				free(r);
                        adec(&desc->ntasks);
		} else {
			eprintf("Error checking task %u status.", r->key);
		}
                
#ifdef _DEBUG
                __get_time(&now);
                if(__diff_time_ns(now, last) > 1e9){
                        int count = rlist_count(&(desc->ftasks), &desc->ftasks_lock); 
                        if(count)
                                Dprintf("There %d pending tasks", count);
                        __get_time(&last);
                }
#endif                
        }
        sem_close(desc->pending_tasks);
        sem_unlink(desc->sem_name);
        return NULL;
}
/*
 * Woker threads process incoming messages and pending tasks. Processing incoming
 * messages has higher priority becuase each message may spawn new tasks. If there
 * are no incoming messages, workers check for all pending tasks. Since the list
 * of pending tasks may be large, we want to make sure that there are no incoming
 * messages waiting to be processed.
 */
void *worker(void *data)
{
        struct worker_struct *desc = (struct worker_struct *)data;
        int ret;
        struct mcl_msg_struct msg;

        Dprintf("Starting worker thread %" PRIu64 " (ntasks=%lu)",
                desc->id, desc->ntasks);
        desc->ftasks = NULL;
        rlist_init(&desc->ftasks_lock);
        desc->ntasks = 0;

        const char *sem_name_format;
        if ((sem_name_format = getenv("MCL_SEM_NAME")) == NULL)
                sem_name_format = MCL_SEM_NAME;
        snprintf(desc->sem_name, 64, sem_name_format, mcl_desc.pid, desc->id);

        // Clean up semaphore in case of previous error
        sem_unlink(desc->sem_name);
        Dprintf("Creating semaphore %s for worker %" PRIu64, desc->sem_name, desc->id);
        desc->pending_tasks = sem_open(desc->sem_name, O_CREAT | O_EXCL, S_IRWXU, 0);
        if(desc->pending_tasks == SEM_FAILED)
                perror("Error crearing semaphore");
        pthread_t worker_cleanup;

#ifdef _STATS
        desc->max_tasks = 0;
        desc->n_transfers = 0;
        desc->bytes_transfered = 0;
#endif
        pthread_barrier_wait(&mcl_desc.wt_barrier);

        pthread_create(&worker_cleanup, NULL, check_pending, data);

        while (status != MCL_DONE || rlist_count(&desc->ftasks, &desc->ftasks_lock) > 0){
                sched_yield();
                ret = cli_msg_recv(&msg);

                if (ret == -1){
                        eprintf("Worker %" PRIu64 ": Error receiving message.",
                                desc->id);
                        pthread_exit(NULL);
                }

                if (ret == 0){
                        Dprintf("Worker %" PRIu64 " received message 0x%" PRIx64
                                " for request %" PRIu64,
                                desc->id, msg.cmd,
                                msg.rid);
#ifdef _STATS
                        desc->nreqs++;
                        stats_inc(mcl_desc.nreqs);
#endif
                        ret = cli_exec_am(desc, &msg);
                        if (ret)
                                eprintf("Error executing AM %" PRIu64 " (%d).",
                                        msg.cmd, ret);
                        msg_free(&msg);
                }
        }

        Dprintf("Worker thread %" PRIu64 " terminated", desc->id);
#ifdef _STATS
        stprintf("W[%" PRIu64 "] NREQS: %" PRIu64 " Pending Tasks: %lu, Max Tasks: %lu",
                 desc->id, desc->nreqs, desc->ntasks, desc->max_tasks);
        stprintf("    transfers: %" PRIu64 " bytes transfered: %" PRIu64 "",
                 desc->n_transfers, desc->bytes_transfered);
#endif
        pthread_exit(NULL);
}
