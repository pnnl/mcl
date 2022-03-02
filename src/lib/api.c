#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>

#include <minos.h>
#include <minos_internal.h>
#include <stats.h>

extern volatile uint64_t status;
extern mcl_request*      hash_reqs;
extern mcl_desc_t        mcl_desc;
extern mcl_resource_t*   mcl_res;
extern mcl_rlist*        ptasks;
extern pthread_rwlock_t  ptasks_lock;

int mcl_test(mcl_handle* h)
{
	if(!h){
		eprintf("Invalid argument");
		return -1;
	}
	return __internal_wait(h, 0, MCL_TIMEOUT);
}

int mcl_wait(mcl_handle* h)
{
	if(!h){
		eprintf("Invalid argument");
		return -1;
	}

	Dprintf("Waiting for request %u to complete...", h->rid);
	
	return __internal_wait(h, 1, MCL_TIMEOUT);
}

int mcl_wait_all(void)
{
	while(req_count(&hash_reqs))
		sched_yield();

	return 0;
}

int mcl_exec(mcl_handle* h, uint64_t* pes, uint64_t* lsize, uint64_t flags)
{
	int           found = -1;
	mcl_device_t* dev   = NULL;
	mcl_request*  r     = NULL;
	mcl_task*     t     = NULL;
	mcl_program*  p     = NULL; 
	uint64_t      i;
	
	if(!h || !pes)
		return -MCL_ERR_INVARG;

	Dprintf("Submitting EXEC AM (PEs=%"PRIu64",%"PRIu64",%"PRIu64
		" LPEs=%"PRIu64",%"PRIu64",%"PRIu64 " FLAGS=0x%"PRIu64")",
		pes[0], pes[1], pes[2], lsize ? lsize[0]:0, lsize ? lsize[1]:0,
		lsize ? lsize[2]:0, flags);
	
	/*
	 * PES and LPES (if provided) should be >=1 in each dimension
	 */
	for(i=0; i<MCL_DEV_DIMS; i++){		
		if(!pes[i])
			return -MCL_ERR_INVPES;
		if(lsize && !lsize[i])
			return -MCL_ERR_INVPES;
	}

	r = rlist_remove(&ptasks, &ptasks_lock, h->rid);
	if(!r)
		return -MCL_ERR_INVREQ;

	t = r->tsk;
	p = t->prg;
	
	if(!p)
		return -MCL_ERR_INVPRG;

	if(!flags)
		flags = MCL_TASK_DFT_FLAGS;
	
	if (!((flags & MCL_TASK_TYPE_MASK) & avail_dev_types)) {
		eprintf("Invalid flags, device type not available (flags = 0x%"PRIx64
			" avail dev types = 0x%"PRIx64").",flags, avail_dev_types); 
		return -MCL_ERR_INVDEV;
	}

	//Check if there is at least a device that can run the task
	VDprintf("  Computing total PES and dimensions...");
	t->tpes = 1;
	for(i=0; i<MCL_DEV_DIMS; i++){
		t->tpes *= pes[i];
		t->pes[i] = pes[i];
		t->lpes[i] = lsize ? lsize[i] : 0;
		if(pes[i] > 1)
			t->dims++;
	}
	
	if(!(t->dims))
	   t->dims = 1;

	for(i=0; i<mcl_desc.info->ndevs && found<0; i++){
                int stop = 0;

		dev = mcl_res[i].dev;
                /* Check type... */
		if(!(dev->type & flags))
			continue;

                /* Check memory... */
		if(t->mem > dev->mem_size){
			continue;
		}

                /* Check total PEs... */
                if(t->tpes > dev->pes){
                        continue;
                }

                /* Check required dimensions... */
                for(int j=0; j<MCL_DEV_DIMS && !stop; j++){    
                        if(dev->wisize[j]==1 && t->pes[j] > 1)
                                stop = 1;
                }
                if(stop)
                        continue;

                /* Check per-dimension work-item size... */
                for(int j=0; j<t->dims; j++)
                        if(t->lpes[j] > dev->wisize[j])
                                continue;

                /* Check work group size... */
                for(int j=0; j<t->dims; j++)
                        if(t->lpes[j])
                                if(t->pes[j]/t->lpes[j] > dev->wgsize)
                                        continue;
                found = i;
	}

	if(found<0){
                Dprintf("No device found for Task %"PRIu32"", h->rid);
		return -MCL_ERR_INVDEV;
	}

	Dprintf("Task %"PRIu32" can be executed on resource %d (%"PRIu64"/%"PRIu64" PEs, %"
		PRIu64"/%"PRIu64" bytes)", 
		h->rid, found, t->tpes, dev->pes, t->mem, dev->mem_size);
	
	if(req_add(&hash_reqs, h->rid, r)){
		eprintf("Error adding request %u to hash table", h->rid);
		return -MCL_ERR_INVREQ;
	}

#ifdef _STATS
	stats_inc(mcl_desc.nreqs);
	if(mcl_desc.nreqs > mcl_desc.max_reqs)
		mcl_desc.max_reqs = mcl_desc.nreqs;
#endif
	
	return __am_exec(h, r, flags);
}


int mcl_task_set_arg(mcl_handle* h, uint64_t id, void* addr, size_t size, uint64_t flags)
{
	if(!h || !size || !flags){
		eprintf("Invalid argument");
		return -MCL_ERR_INVARG;
	}

	Dprintf("Setting argument of task %u (addr: %p size: %lu flags: 0x%"PRIx64")",
		h->rid, addr, size, flags);

	return __set_arg(h, id, addr, size, flags);
		
}

int mcl_null(mcl_handle* hdl)
{
	mcl_request*  req = NULL;
	
	Dprintf("Submitting NULL AM");
	if(!hdl)
		return -MCL_ERR_INVARG;

	req = rlist_remove(&ptasks, &ptasks_lock, hdl->rid);
	if(req ==  NULL){
		eprintf("Error looking for task %u", hdl->rid);
		return MCL_ERR_INVTSK;
	}
	
	if(req_add(&hash_reqs, hdl->rid, req)){
		eprintf("Error adding request %u to pending request table",
			hdl->rid);
		return MCL_ERR_INVREQ;
	}
	Dprintf("Executing NULL command");
	return __am_null(req);
}

mcl_handle* mcl_task_create(void)
{
	Dprintf("Creating a new task...");
	return __task_create();
}

mcl_handle* mcl_task_init(char* path, char* name, uint64_t nargs, char* copts, unsigned long flags)
{
	if(!path || !name)
		return NULL;
	
	Dprintf("Creating and initializing a new task for kernel %s in %s (nargs %"PRIu64" options %s flags %lu)",
		path, name, nargs, copts, flags);

	return __task_init(path, name, nargs, copts, flags);	
}

int mcl_task_set_kernel(mcl_handle* h, char* path, char* name, uint64_t nargs,
			char* copts, unsigned long flags)
{
	if(!h || !path || !name){
		eprintf("Invalid argument for task set kernel");
		return -MCL_ERR_INVARG;
	}
	
	Dprintf("Setting task %u to use kernel %s in %s (nargs %"PRIu64" optionns %s flags %lu)",
		h->rid, name, path, nargs, copts, flags);
	
	return __set_kernel(h, path, name, nargs, copts, flags);
}

int mcl_hdl_free(mcl_handle* h)
{
	mcl_request* req;
	
	Dprintf("Removing handle %u...", h->rid);
	if(!h){
		eprintf("Invalid argument");
		return -MCL_ERR_INVARG;
	}

	req = req_del(&hash_reqs, h->rid);
	if(req){
		free(req);
		stats_dec(mcl_desc.nreqs);
	}
	
	free(h);
	
	return 0;
}

uint32_t mcl_get_ndev(void)
{
	return mcl_desc.info->ndevs;
}

int mcl_get_dev(uint32_t id, mcl_dev_info* dev)
{
	if(!dev || id > mcl_desc.info->ndevs){
		eprintf("Invalid argument");
		return -1;
	}

	dev->id       = id;
	strcpy(dev->name,   mcl_res[id].dev->name);
	strcpy(dev->vendor, mcl_res[id].plt->vendor);
	dev->type     = mcl_res[id].dev->type;
	dev->status   = mcl_res[id].status;
	dev->mem_size = mcl_res[id].dev->mem_size;
	dev->ndims    = mcl_res[id].dev->ndims;
	dev->wgsize   = mcl_res[id].dev->wgsize;
	dev->pes      = mcl_res[id].dev->pes;
	dev->wisize   = (size_t*) malloc(sizeof(size_t) * dev->ndims);

	if(! dev->wisize){
		eprintf("Error allocating max work sizes vector!");
		return -1;
	}
	memcpy((void*)(dev->wisize), (void*)(mcl_res[id].dev->wisize),
	       sizeof(size_t) * dev->ndims);
	
	return 0;
}

int mcl_init(uint64_t workers, uint64_t flags)
{
	uint64_t i;

	if(cas(&status, MCL_NONE, MCL_STARTED) == false){
		Dprintf("MCL library has been/is being already initialized by another thread...");
		return 1;
	}
	
	if(!workers){
		eprintf("Number of worker threads must be >= 1. Aborting.");
		goto err;
	}

	mcl_desc.workers  = workers;
	mcl_desc.flags    = flags;
	mcl_desc.pid      = getpid();
	mcl_desc.out_msg  = 0;
#ifdef _STATS
	mcl_desc.nreqs    = 0;
	mcl_desc.max_reqs = 0;
#endif
	pthread_barrier_init(&mcl_desc.wt_barrier, NULL, mcl_desc.workers + 2);
	
	Dprintf("Initializing Minos Computing Library (wt=%" PRIu64 " flags=0x%"
	        PRIx64 " max_msg=%lu (buffer size=%d msg size=%lu))",
		mcl_desc.workers, mcl_desc.flags, MCL_MSG_MAX, MCL_SND_BUF, MCL_MSG_SIZE);

	if(cli_setup()){
		eprintf("Error setting up MCL library. Aborting.");
		goto err;
	}
        Dprintf("Creating cleanup thread...");
        if(pthread_create(&(mcl_desc.wcleanup), NULL, check_pending, NULL)){
                eprintf("Error creating cleanup thread.");
                goto err_setup;
        } 

	Dprintf("Creating %"PRIu64" worker threads...", mcl_desc.workers);
	mcl_desc.wids = (struct worker_struct*) malloc(mcl_desc.workers * 
							sizeof(struct worker_struct));
	if(!mcl_desc.wids){
		eprintf("Error allocating worker thread ids");
		goto err_setup;
	}
	memset((void*) mcl_desc.wids, 0, mcl_desc.workers * sizeof(struct worker_struct));
	for(i=0; i<mcl_desc.workers; i++){
		mcl_desc.wids[i].id = i;
		mcl_desc.wids[i].ntasks = 0;
		if(pthread_create(&mcl_desc.wids[i].tid,NULL,worker,(void*) &mcl_desc.wids[i])){
			eprintf("Error creating worker thread %" PRIu64, i);
			goto err_setup;
		}
#ifdef _STATS
		mcl_desc.wids[i].nreqs = 0;
#endif
	}

	if(cas(&status, MCL_STARTED, MCL_ACTIVE) == false){
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

	if (status != MCL_ACTIVE) {
		eprintf("Library was not properly initialized.");
		goto err;
	}

	cas(&status, MCL_ACTIVE, MCL_DONE);

	Dprintf("Terminating workers...");
	for(i=0; i<mcl_desc.workers; i++) {
		Dprintf("  Waiting for worker %"PRIu64" (ntasks=%lu)", 
			i, mcl_desc.wids[i].ntasks);
		pthread_join(mcl_desc.wids[i].tid, NULL);
	}
	Dprintf("Worker threads terminated.");
	
        Dprintf("Terminating cleanup thread...");
        pthread_join(mcl_desc.wcleanup, NULL);
        Dprintf("Cleanup thread terminated.");

	if(cli_shutdown()){
		eprintf("Error finalizing MCL library. Aborting.");
		goto err;
	}

	stprintf("Reqs: %lu Max_Reqs: %lu Msg_Out: %"PRIu64,
		 mcl_desc.nreqs, mcl_desc.max_reqs, mcl_desc.out_msg);
	return 0;

 err:
	return -1;
}

mcl_transfer* mcl_transfer_create(uint64_t nargs, uint64_t ncopies)
{
        Dprintf("Creating a new transfer task...");
        return __transfer_create(nargs, ncopies);
}

int mcl_transfer_set_arg(mcl_transfer* t, uint64_t id, void* addr, size_t size, uint64_t flags)
{
    if(id > t->nargs){
        eprintf("Invalid argument");
		return -MCL_ERR_INVARG;
    }

    flags = flags | MCL_ARG_RESIDENT | MCL_ARG_BUFFER;
    if(!t || !size || !addr){
		eprintf("Invalid argument");
		return -MCL_ERR_INVARG;
	}

	Dprintf("Setting argument of transfer (addr: %p size: %lu flags: 0x%"PRIx64")",
		addr, size, flags);
    
    t->args[id] = addr;
    t->sizes[id] = size;
    t->flags[id] = flags;
    return 0;
}

int mcl_transfer_exec(mcl_transfer* t, uint64_t flags)
{
	mcl_request*  r     = NULL;
	uint64_t      i, j, ret;

    if(!flags)
        flags = MCL_TASK_DFT_FLAGS;
    if(t->ncopies > 1)
        flags |= MCL_FLAG_NO_RES;
	
    if (!((flags & MCL_TASK_TYPE_MASK) & avail_dev_types)) {
        eprintf("Invalid flags, device type not available (flags = 0x%"PRIx64
            " avail dev types = 0x%"PRIx64").",flags, avail_dev_types); 
        return -MCL_ERR_INVDEV;
    }
	
    for(i = 0; i < t->ncopies; i++){
        t->handles[i] = __transfer_hdl_create(t->nargs);
        if(!t->handles[i]){
            eprintf("Unable to create task for transfer %"PRIu64"", i);
            ret = MCL_ERR_MEMALLOC;
            goto err;
        }

        for(j=0; j < t->nargs; j++){
            if(__set_arg(t->handles[i], j, t->args[j], t->sizes[j], t->flags[j])){
                eprintf("Unable to set argument %"PRIu64" for transfer %"PRIu64"", j, i);
                ret = MCL_ERR_INVARG;
                goto err;
            };
        }

        Dprintf("Submitting TRANSFER AM (RID=%"PRIu32", FLAGS=0x%"PRIu64")",
		t->handles[i]->rid, flags);

        r = rlist_remove(&ptasks, &ptasks_lock, t->handles[i]->rid);
        if(!r){
            ret = MCL_ERR_INVREQ;
            goto err;
        }
        
        if(req_add(&hash_reqs, t->handles[i]->rid, r)){
            eprintf("Error adding request %u to hash table", t->handles[i]->rid);
            ret = MCL_ERR_INVREQ;
            goto err;
        }

#ifdef _STATS
        stats_inc(mcl_desc.nreqs);
        if(mcl_desc.nreqs > mcl_desc.max_reqs)
            mcl_desc.max_reqs = mcl_desc.nreqs;
#endif

        if(__am_exec(t->handles[i], r, flags)){
            ret = MCL_ERR_EXEC;
            goto err;
        }

    }
    return 0;

err:
    t->ncopies = i;
    return -ret;
}

int mcl_transfer_wait(mcl_transfer* t)
{
    int ret;

    for(int i = 0; i < t->ncopies; i++){
        if((ret=mcl_wait(t->handles[i]))){
            return ret;
        }
    }

    return 0;

}

int mcl_transfer_test(mcl_transfer* t)
{
    int ret;

    for(int i = 0; i < t->ncopies; i++){
        ret=mcl_test(t->handles[i]);
        if(ret != MCL_REQ_COMPLETED)
            return MCL_REQ_INPROGRESS;
    }
    return MCL_REQ_COMPLETED;
}

int mcl_transfer_free(mcl_transfer* t)
{
    for(int i = 0; i < t->ncopies; i++){
        if(mcl_hdl_free(t->handles[i]))
            return MCL_ERR_INVREQ;
    }

    free(t->args);
    free(t->sizes);
    free(t->flags);
    free(t->handles);

    free(t);
    return 0;
}