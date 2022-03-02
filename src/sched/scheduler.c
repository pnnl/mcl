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

mcl_sched_t                   mcl_desc;
mcl_info_t*                   mcl_info  = NULL;
cl_platform_id*               cl_plts   = NULL;
mcl_platform_t*               mcl_plts  = NULL;
mcl_client_t*                 mcl_clist = NULL;
mcl_resource_t*               mcl_res   = NULL;
mcl_class_t*                  mcl_class = NULL;

static volatile sig_atomic_t sched_done = 0;
int                          sock_fd, shm_fd;
struct sockaddr_un           saddr;
static pthread_t             rcv_tid;

struct sched_class *sched_curr = &fifo_class;
extern const struct sched_resource_policy ff_policy;
extern const struct sched_resource_policy rr_policy;
extern const struct sched_resource_policy delay_policy;
extern const struct sched_resource_policy hybrid_policy;

/* The following lock protects sched_req_table */
static pthread_mutex_t sched_req_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sched_req_has_slots = PTHREAD_COND_INITIALIZER;
static struct ph_table *sched_req_table;

static const char* 			 socket_name = NULL;
static const char* 			 shared_mem_name = NULL;

#ifdef _DEBUG
void resource_list(mcl_resource_t* res, uint64_t n);
#endif

static inline int srv_msg_send(struct mcl_msg_struct* msg, struct sockaddr_un* dst)
{
	int ret;

	while(((ret=msg_send(msg, sock_fd, dst)) > 0))
		sched_yield();

	return ret;
}

static inline int srv_msg_recv(struct mcl_msg_struct* msg)
{
	struct sockaddr_un src;
	int                ret;

	ret = msg_recv(msg, sock_fd, &src);

	if(ret)
		return ret;
	
	if(!(msg->pid = (uint64_t) cli_get_pid(src.sun_path))){
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

        uint64_t    res_mem = 0;
        for(int i = 0; i < r->nresident; i++){
                if(sched_rdata_add_device(r->resdata[i], r->dev))
                res_mem += r->resdata[i]->size;
        

                if(r->resdata[i]->flags & MSG_ARGFLAG_EXCLUSIVE){
                        Dprintf("\t\t Allocating exclusive memory to the correct device");
                        for(int dev = 0; dev < mcl_info->ndevs; dev++){
                                if(dev == r->dev)
                                        continue;

                                if( sched_rdata_rm_device(r->resdata[i], dev)){
#ifdef _TRACE
                                        mem_now = add_fetch(&mcl_res[dev].mem_avail, r->resdata[i]->size);
#else
                                        add_fetch(&mcl_res[dev].mem_avail, r->resdata[i]->size);
#endif
                                        TRprintf("R[%d]: %"PRIu64"/%"PRIu64" mem available", dev, mem_now,mcl_res[dev].dev->mem_size);
                                }
                        }

                        Dprintf("\t\t Allocated exclusive memory to the correct device, devs: 0x%016"PRIx64"", r->resdata[i]->devs);
                }
        }
 
        int64_t needed_mem = r->mem - res_mem;
        Dprintf("Needed Mem: %"PRId64", Task Mem: %"PRIu64", Resident Mem: %"PRIu64", Num Res: %"PRIu64", Avail Mem: %"PRIu64"",
        needed_mem, r->mem, res_mem, r->nresident, res->mem_avail);


#if defined _TRACE || defined _DEBUG
	pes_now = add_fetch(&res->pes_used, r->pes);
#else
        add_fetch(&res->pes_used, r->pes);
#endif
	mem_now = add_fetch(&res->mem_avail, -needed_mem);
	kernels_now = ainc(&res->nkernels) + 1;

	assert(mem_now < res->dev->mem_size); /* safety check against wrap around */
	if(!mem_now || kernels_now >= res->dev->max_kernels)		
		res->status = MCL_DEV_FULL;
	else
		res->status = MCL_DEV_ALLOCATED;

        Dprintf("  Resource %"PRIu64" now has %"PRIu64" kernels running, %"PRIu64"/%"PRIu64" PEs used and %"PRIu64"/%"PRIu64" MEM available", 
		 r->dev, kernels_now, pes_now, res->dev->pes, mem_now, res->dev->mem_size);

	TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" pes used", r->dev, pes_now,
		res->dev->pes);
        TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" mem available", r->dev, mem_now,
		res->dev->mem_size);
        TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" kernels running", r->dev, kernels_now,
		res->dev->max_kernels);
	return r->dev;
}

int default_put_resource(sched_req_t *r)
{
#if defined _DEBUG || defined _TRACE
        uint64_t pes_now;
#endif
 
        uint64_t mem_now, kernels_now, refs;
	mcl_resource_t *res;
        if (r->dev < 0)
		return -1;
	res = mcl_res + r->dev;

        uint64_t mem_freed = r->mem;
        for(int i = 0; i < r->nresident; i++){
                mem_freed -= r->resdata[i]->size;
                refs = adec(&(r->resdata[i]->refs));
                if(refs == 0 && r->resdata[i]->ndevs == 0)
                        free(r->resdata[i]);
        }


	mem_now = add_fetch(&res->mem_avail, mem_freed);
        kernels_now = adec(&res->nkernels) - 1;

#if defined _DEBUG || defined _TRACE
	pes_now = add_fetch(&res->pes_used, -r->pes);
#else
        add_fetch(&res->pes_used, -r->pes);
#endif

	res->status = mem_now && kernels_now < res->dev->max_kernels ? MCL_DEV_ALLOCATED : MCL_DEV_READY;

	Dprintf("  Resource %"PRIu64" now has %"PRIu64" kernels running, %"PRIu64"/%"PRIu64" PEs used and %"PRIu64"/%"PRIu64" MEM available", 
		 r->dev, kernels_now, pes_now, res->dev->pes, mem_now, res->dev->mem_size);
	
	TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" pes used", r->dev,
		 pes_now, res->dev->pes);
        TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" mem available", r->dev, mem_now,
	        res->dev->mem_size);
        TRprintf("R[%"PRIu64"]: %"PRIu64"/%"PRIu64" kernels running", r->dev, kernels_now,
		res->dev->max_kernels);

	return 0;
}

static inline int sched_run(sched_req_t* r)
{
	struct mcl_msg_struct     ack;
	struct mcl_client_struct* dst;
	
	dst = cli_search(&mcl_clist, r->pid);
	if(!dst){
		eprintf("Client %d not registered.", r->pid);
		goto err;
	}
	
	msg_init(&ack);
	ack.cmd  = MSG_CMD_ACK;
	ack.rid  = r->rid;
	ack.pid  = r->pid;
	ack.res  = r->dev;

	if(srv_msg_send(&ack, &(dst->addr))){
		eprintf("Error sending ACK to client %d",ack.pid);
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
	while ((ret=ph_add(sched_req_table, r, key)) < 0) {
		/* table full */
		eprintf("Table full, unable to insert req.\n");
		pthread_cond_wait(&sched_req_has_slots, &sched_req_lock);
	}

	/* here request can be either inserted or refused because duplicate */
	pthread_mutex_unlock(&sched_req_lock);

	return ret;
}

static inline sched_req_t *sched_request_untrack(const void *key)
{
	sched_req_t *r;

	pthread_mutex_lock(&sched_req_lock);
	ph_remove(sched_req_table, key, key, r);
	if (r) {
		/* signal waiters */
		pthread_cond_signal(&sched_req_has_slots);
	}
	pthread_mutex_unlock(&sched_req_lock);

	return r;
}


static inline int am_exe(mcl_msg msg)
{
	sched_req_t* r = NULL;
	
	r = sched_alloc_request();
	if(!r){
		eprintf("Error creating new request for (%d,%"PRIu64") ",
			msg.pid, msg.rid);
		goto err;
	}
	
	r->pid    = msg.pid;
	r->pes    = msg.pes;
	r->mem    = msg.mem * MCL_PAGE_SIZE;
	r->rid    = msg.rid;
        r->flags  = msg.flags << MCL_TASK_FLAG_SHIFT;
	r->type   = msg.type;
	r->status = 0x0;
        r->num_attempts = 0;
        for(int i=0; i<MCL_DEV_DIMS; i++){
                r->dpes[i] = msg.pesdata.pes[i];
                r->lpes[i] = msg.pesdata.lpes[i];
        }
        r->nresident   = msg.nres;
        r->resdata = (sched_rdata**)malloc(sizeof(sched_rdata*) * msg.nres);
        if(!r->resdata) {
                eprintf("Error allocating memory for new request (%d,%"PRIu64") ",msg.pid, msg.rid);
        }

        for(int i = 0; i < msg.nres; i++) {
                sched_rdata* el;
                if(!(el = sched_rdata_get(msg.resdata[i].mem_id, r->pid))){
                        el = malloc(sizeof(sched_rdata));
                        el->key[0] = 0;
                        el->key[1] = 0;
                        el->pid = r->pid;
                        el->mem_id = msg.resdata[i].mem_id;
                        el->size = msg.resdata[i].mem_size;
                        el->flags = msg.resdata[i].flags;
                        el->devs = 0;
                        el->refs = 0;
                        el->ndevs = 0;
                        sched_rdata_add(el);
                }
                ainc(&el->refs);
                r->resdata[i] = el;

                Dprintf("For request (%d, %"PRIu64"), found MEMID: %"PRIu64", REFs: %"PRIu64", NDEVS: %"PRIu64" DEV: 0x%016"PRIx64"",
                r->pid, r->rid, r->resdata[i]->mem_id, r->resdata[i]->refs, r->resdata[i]->ndevs, r->resdata[i]->devs);
        }
    
	Dprintf("Executing EXEC AM (RID=%"PRIu64" PES=%"PRIu64" (%"PRIu64") MEM=%"PRIu64" (%"PRIu64") PES=[%"PRIu64",%"PRIu64",%"PRIu64"] LPES=[%"PRIu64",%"PRIu64",%"PRIu64"])...",
		r->rid, r->pes, msg.pes, r->mem, msg.mem, r->dpes[0], r->dpes[1], r->dpes[2], r->lpes[0], r->lpes[1], r->lpes[2]);
	
	if(sched_enqueue(r)){
		eprintf("Error enqueueing request (%d,%"PRIu64")",
			msg.pid, msg.rid);
		goto err_req;
	}
	
	return 0;
	
 err_req:
	sched_release_request(r);
 err:
	return -1;
}

static inline int am_null(mcl_msg msg)
{
	Dprintf("Executing NULL AM...");
		
	return 0;
}

static inline int am_reg(mcl_msg msg)
{
	struct mcl_client_struct* el;
	struct mcl_msg_struct     ack;
	
	Dprintf("Executing REG AM...");

	el = (struct mcl_client_struct*) malloc(sizeof(struct mcl_client_struct));
	if(!el){
		eprintf("Error allocating memory for new client");
		goto err;
	}

	el->pid    = msg.pid;
	el->flags  = 0x0;
	el->status = CLI_ACTIVE;
	el->addr.sun_family = PF_UNIX;
	const char* client_format;
        if((client_format = getenv("MCL_SOCK_CNAME")) == NULL) {
                client_format = MCL_SOCK_CNAME;
        }
	snprintf(el->addr.sun_path, sizeof(el->addr.sun_path),
		client_format, (long) el->pid);
	
	if(cli_add(&mcl_clist, el)){
		eprintf("Error adding new client.");
		goto err_el;
	}

	msg_init(&ack);
	ack.cmd = MSG_CMD_ACK;
	ack.rid = msg.rid;
	ack.pid = msg.pid;

	if(srv_msg_send(&ack, &(el->addr))){
		eprintf("Error sending ACK to client %d",ack.pid);
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
#if defined _DEBUG || defined _TRACE
        uint64_t mem_now;
#endif
        uint64_t* mem_freed = (uint64_t*)malloc(mcl_info->ndevs * sizeof(uint64_t));
        mcl_resource_t* res = mcl_res;
        
	Dprintf("Executing END AM...");

	cli_remove(&mcl_clist, msg.pid);

        if(!mem_freed){
                eprintf("Error allocating memory.");
                return -1;
        }
        sched_rdata_rm_pid(msg.pid, mem_freed, mcl_info->ndevs);
        for(int i = 0; i < mcl_info->ndevs; i++, res++){
#if defined _DEBUG || defined _TRACE
	        mem_now = add_fetch(&res->mem_avail, mem_freed[i]);
#else
                add_fetch(&res->mem_avail, mem_freed[i]);
#endif
                Dprintf("  Resource %d now %"PRIu64"/%"PRIu64" MEM available", 
                        i, mem_now, mcl_res[i].dev->mem_size);

                TRprintf("R[%d]: %"PRIu64"/%"PRIu64" mem available", i, mem_now,
		        res->dev->mem_size);

        }
        free(mem_freed);

	return 0;
}

static inline int am_done(mcl_msg msg)
{
	sched_req_t* r = NULL;
	/* FIXME: this could be endian dependent */
	uint64_t key[2] = { msg.pid, msg.rid };
	
#ifdef _DEBUG
	if(msg.cmd == MSG_CMD_DONE)
		Dprintf("Request (%d,%"PRIu64") completed successfully", msg.pid, msg.rid);
	else
		Dprintf("Request (%d,%"PRIu64") completed with errors", msg.pid, msg.rid);
#endif

	r = sched_request_untrack(key);

	if(!r){
		eprintf("Error post-processing request (%d,%"PRIu64")",
			msg.pid, msg.rid);
		return -1;
	}

	if (sched_put_resource(r) < 0) {
		eprintf("  Unable to put resource for (%d, %"PRIu64")",
		        r->pid, r->rid);
	}

	sched_complete(r);

	sched_release_request(r);

	return 0;
}

int am_free(struct mcl_msg_struct msg)
{
    /* This is not ideal because in all other cases round robin deals with device memory*/
    mcl_resource_t *res;
    sched_rdata    *el;
    uint64_t       devs;
    int            i;
#if defined _DEBUG || defined _TRACE
    uint64_t       mem_now;
    int            curr_dev;
#endif

	Dprintf("Number of resources to free: %"PRIu64"", msg.nres);

    for(i = 0; i < msg.nres; i++){
	    el = sched_rdata_rm(msg.resdata[i].mem_id, msg.pid);
        if(!el){
            Dprintf("Could not find memory to free <%d, %"PRIu64">", 
                msg.pid, msg.resdata[i].mem_id);
            continue;
        }
        res = mcl_res;
        devs = el->devs;
#if defined _DEBUG || defined _TRACE
        curr_dev = 0;
#endif
        while(devs){
            if(devs & 0x01){
#if defined _DEBUG || defined _TRACE
	            mem_now = add_fetch(&res->mem_avail, msg.resdata[i].mem_size);
#else
                add_fetch(&res->mem_avail, msg.resdata[i].mem_size);
#endif
                Dprintf("  Resource %d now %"PRIu64"/%"PRIu64" MEM available", 
		        curr_dev, mem_now, res->dev->mem_size);

                TRprintf("R[%d]: %"PRIu64"/%"PRIu64" mem available", curr_dev, mem_now,
		            res->dev->mem_size);
            }
            devs = devs >> 1;
            res += 1;
#if defined _DEBUG || defined _TRACE
            curr_dev += 1;
#endif
        }
        el->devs = 0;
        el->ndevs = 0;
    }
    /* FIXME: Need a way to notify waiting scheduler...
    *  This is kind of hacky but works with both fffs and fifo
    */
    sched_complete(NULL);
    return 0;
}


int exec_am(struct mcl_msg_struct msg)
{
	switch(msg.cmd){
	case MSG_CMD_NULL:
		if(am_null(msg))
			goto err;				
		break;
	case MSG_CMD_REG:
		if(am_reg(msg))
			goto err;
		break;
	case MSG_CMD_EXE:
		if(am_exe(msg))
			goto err;
		break;		
	case MSG_CMD_END:
		if(am_end(msg))
			goto err;		
		break;
	case MSG_CMD_DONE:
		if(am_done(msg))
			goto err;		
		break;
    case MSG_CMD_FREE:
        if(am_free(msg))
            goto err;
        break;
	case MSG_CMD_ERR:
		if(am_done(msg))
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

void* receiver(void* data)
{
	struct mcl_msg_struct msg;

	Dprintf("Schedule Receiver thread started.");
	
	while(!sched_done){
		if(srv_msg_recv(&msg) == 1){
			sched_yield();
			continue;
		}
		
		if(exec_am(msg)){
			eprintf("Error executing AM");
		}
        msg_free(&msg);
	}

	Dprintf("Schedule Receiver thread terminating...");
	pthread_exit(0);
}

int schedule(void)
{
	sched_req_t* r;
	int ret;
	
	while(!sched_done){
		if((r=sched_pick_next())){
			Dprintf("Scheduling request %"PRIu64" on resource %"PRIu64"", r->rid, r->dev);

			sched_assign_resource(r);
			
			ret = sched_request_track(r);
			
			if (ret > 0) {
				eprintf("schedule: duplicate request, discard => (%d, %"PRIu64")\n",
				        r->pid, r->rid);
				sched_release_request(r);
				continue;
			}
			sched_run(r);
		}
		else{
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
    if((socket_name = getenv("MCL_SOCK_NAME")) == NULL) {
        socket_name = MCL_SOCK_NAME;
    }
	if((shared_mem_name = getenv("MCL_SHM_NAME")) == NULL) {
        shared_mem_name = MCL_SHM_NAME;
    }


	shm_unlink(shared_mem_name);
	unlink(socket_name);

	Dprintf("Creating shared memory object %s...", shared_mem_name);
	shm_fd = shm_open(shared_mem_name, O_RDWR|O_CREAT|O_EXCL, S_IRWXU|S_IRGRP|S_IROTH);
	if(shm_fd == -1){
		eprintf("Error creating shared memory descriptor. Aborting.");
		perror("shm_open");
		goto err;
	}

	if(ftruncate(shm_fd, MCL_SHM_SIZE)){
	  eprintf("Error truncating shared memory file. Aborting.");
	  perror("ftruncate");
	  goto err;
	}

	mcl_info = (mcl_info_t*) mmap(NULL,MCL_SHM_SIZE,PROT_READ|PROT_WRITE,
						  MAP_SHARED,shm_fd, 0);
	if(!mcl_info){
		eprintf("Error mapping shared memory object. Aborting.");
		perror("mmap");
		goto err_shm_fd;
	}
	memset((void*)mcl_info, 0, MCL_SHM_SIZE);
	Dprintf("Shared memory mapped at address %p.", mcl_info);

	if(resource_discover(&cl_plts, &mcl_plts, &mcl_class, &mcl_info->nplts, &mcl_info->ndevs)){
		eprintf("Error discovering computing elements! Aborting!");
		goto err_mmap;
	}

	mcl_info->nclass = class_count(mcl_class);
	mcl_res = (mcl_resource_t*) malloc(mcl_info->ndevs * sizeof(mcl_resource_t));
	if(!mcl_res){
		eprintf("Error allocating memory to map MCL resources.");
		goto err_mmap;
	}
	
	if(resource_map(mcl_plts, mcl_info->nplts, mcl_class, mcl_res)){
		eprintf("Error mapping devices to MCL resources");
		goto err_res;
	}
	
	Dprintf("Discovered %"PRIu64" platforms and a total of %"PRIu64" devices",
		mcl_info->nplts, mcl_info->ndevs);

    if(msg_setup(mcl_info->ndevs)){
        eprintf("Error setting up message");
		goto err_res;
    }
	
	mcl_desc.nclients = 0;
#ifdef _STATS
	mcl_desc.nreqs    = 0;
#endif
	
	ff_policy.init(mcl_res, mcl_info->ndevs);
	Dprintf("Init First-Fit resource scheduling at %p.", &ff_policy);
	rr_policy.init(mcl_res, mcl_info->ndevs);
	Dprintf("Init Round-Robin resource scheduling at %p.", &rr_policy);
    delay_policy.init(mcl_res, mcl_info->ndevs);
	Dprintf("Init DelaySched resource scheduling at %p.", &delay_policy);
    hybrid_policy.init(mcl_res, mcl_info->ndevs);
	Dprintf("Init DelaySched resource scheduling at %p.", &hybrid_policy);
	
	Dprintf("MCL descriptor at %p size = 0x%lx.",mcl_info,sizeof(struct mcl_desc_struct));
	
	mcl_info->sndbuf = MCL_SND_BUF;
	mcl_info->rcvbuf = MCL_RCV_BUF;
	
	sock_fd = socket(PF_LOCAL, SOCK_DGRAM, 0);
	if(sock_fd < 0){
		eprintf("Error creating communication socket.");
		perror("socket");
		goto err_res;
	}

	if(fcntl(sock_fd, F_SETFL, O_NONBLOCK)){
		eprintf("Error setting scheduler socket flags");
		perror("fcntl");
		goto err_res;
	}
	
	if(setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &(mcl_info->sndbuf), sizeof(uint64_t))){
		eprintf("Error setting scheduler sending buffer");
		perror("setsockopt");
		goto err_res;
	}		

	if(setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &(mcl_info->rcvbuf), sizeof(uint64_t))){
		eprintf("Error setting scheduler receiving buffer");
		perror("setsockopt");
		goto err_res;
	}		

#if _DEBUG
	uint64_t r = 0, s = 0;
	socklen_t len = sizeof(uint64_t);
	
	if(getsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &s, &len)){
		eprintf("Error getting scheduler sended buffer");
		perror("getsockopt");
	}

	len = sizeof(uint64_t);

	if(getsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &r, &len)){
		eprintf("Error getting scheduler receiving buffer");
		perror("getsockopt");		
	}
	
	Dprintf("Sending buffer set to %"PRIu64" - Receiving buffer set to %"PRIu64" ", s, r);
	
#endif
	
	memset(&saddr, 0, sizeof(struct sockaddr_un));
	saddr.sun_family = PF_UNIX;
	strncpy (saddr.sun_path, socket_name, sizeof(saddr.sun_path)-1);
	Dprintf("Scheduler communication socket %s created", saddr.sun_path);
	
	if(bind(sock_fd, (struct sockaddr*) &saddr, sizeof(struct sockaddr_un)) < 0){
		eprintf("Error binding communication socket.");
		perror("bind");
		goto err_socket;
	}
	Dprintf("Communication socket bound to %s", socket_name);

    if(sched_rdata_init()){
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
	int      ret = 0;
	
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

	if(munmap(mcl_info, MCL_SHM_SIZE)){
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

	while(sched_queue_len())
		;
	sched_done = 1;
}

static int sched_set_class(const char *sc)
{
	if (!strcmp(sc, "fifo")) {
		sched_curr = &fifo_class;
	}
	else if (!strcmp(sc, "fffs")) {
		sched_curr = &fffs_class;
	}
	else {
		return -1;
	}

	return 0;
}

static int sched_set_resource_policy(const char *policy)
{
	assert(sched_curr);

	if (!strcmp(policy, "ff")) {
		sched_curr->respol = &ff_policy;
	}
	else if (!strcmp(policy, "rr")) {
		sched_curr->respol = &rr_policy;
	}
    else if (!strcmp(policy, "delay")) {
        sched_curr->respol = &delay_policy;
    }else if (!strcmp(policy, "hybrid")) {
        sched_curr->respol = &hybrid_policy;
    }
	else {
		return -1;
	}

	return 0;
}

static void print_help(const char *prog)
{
	fprintf(stderr, "Usage: %s [options]\n"
	                "\t-s, --sched-class {fifo|fffs}  Select scheduler class (def = 'fifo')\n"
	                "\t-p, --res-policy {ff|rr|delay|hybrid}  Select resource policy (def = class dependant)\n"
	                "\t-h, --help                     Show this help\n",
	                prog
	);

	exit(EXIT_FAILURE);
}

static void parse_arguments(int argc, char *argv[])
{
	static struct option long_args[] = {
		{ "help",        no_argument, NULL, 'h' },
		{ "sched-class", required_argument, NULL, 's' },
		{ "res-policy",  required_argument, NULL, 'p' },
	};

	const char *policy = NULL;

	int opt;

	do {
		opt = getopt_long(argc, argv, "s:p:h", long_args, NULL);

		switch (opt) {
			case 's':
				if (sched_set_class(optarg) < 0) {
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
			case 'h': /* fall through */
			case '?':
				print_help(argv[0]);
			default:
				break;
		};
	}
	while (opt != -1);

	/* all argument parsed */
	if (policy) {
		if (sched_set_resource_policy(policy) < 0) {
			fprintf(stderr, "parse_arguments: cannot find '%s' policy.\n",
					policy);
			print_help(argv[0]);
		}
		else
			Dprintf("Set resource policy: '%s'\n", policy);
	}
}

int main(int argc, char *argv[])
{
	struct sigaction act;
	
 	Dprintf("Starting Minos scheduler...");
	
	parse_arguments(argc, argv);
	
	if(__setup()){
		eprintf("Error setting up Minos.");
		goto err;
	}
	
	memset (&act, '\0', sizeof(act));
	act.sa_handler = &hdl;
	if (sigaction(SIGINT, &act, NULL) < 0) {
		eprintf("Error setting up signal action.");
		goto err_setup;
	}
	
#ifdef _DEBUG
	resource_list(mcl_res, mcl_info->ndevs);
#endif
	
	if(sched_init(NULL)){
		Dprintf("Error initializing scheduling algorithm");
		goto err_setup;
	}
	
	sched_req_table = ph_init(1u << SCHED_REQ_TABLE_SIZE_SHIFT);
	
	if (!sched_req_table) {
		eprintf("Error setting up scheduler request table.");
		goto err_setup;
	}
	
	Dprintf("Request table initialized: mask=%08lx count=%lu",
	        sched_req_table->mask, ph_count(sched_req_table));
	
	if(pthread_create(&rcv_tid, NULL, receiver, NULL)){
		eprintf("Error starting scheduling receiver thread.");
		goto err_sched;
	}
	
	if(schedule()){
		eprintf("Error executing scheduling algorithm!");
		goto err_sched;
	}
	
	Dprintf("Minos scheduler shutting down.");
	
	pthread_join(rcv_tid, NULL);
	Dprintf("Receiver thread terminated.");
	
	if(sched_finit()){
		Dprintf("Error finilizing FIFO scheduler");
		goto err_setup;
	}
	
	if(__shutdown()){
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
