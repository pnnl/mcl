
#include <minos_sched.h>

static mcl_resource_t *res;
static int nresources;

static int next_dev;

static void rr_init_resources(mcl_resource_t *r, int n)
{
	res = r;
	nresources = n;
	next_dev = 0;
}

static uint64_t rr_mem_on_dev(sched_req_t* r, int dev){
    uint64_t mem = 0;
    for(int i = 0; i < r->nresident; i++){
        if(sched_rdata_on_device(r->resdata[i], dev)){
            mem += r->resdata[i]->size;
        }
    }
    return mem;
}

static int rr_find_resource(sched_req_t *r)
{
	int i, cnt = 0;
	
	VDprintf("Locating resource for (%d,%"PRIu64") PES: %"PRIu64" MEM: %"PRIu64" TYPE: 0x%"PRIx64"",
		r->key.pid, r->key.rid, r->pes, r->mem, r->type);
	
	/* Scan num_res devices starting from next_dev, wrap around if necessary. */
	for (i = next_dev; cnt < nresources; cnt++, i = (i+1)%nresources) {
		if (!(res[i].dev->type & r->type))
			continue;

        uint64_t needed_mem = r->mem - rr_mem_on_dev(r, i);
        Dprintf("\tNeeded on resource %d: %"PRIu64" MEM", i, needed_mem);
		if ((res[i].mem_avail >= needed_mem || res[i].dev->type & MCL_TASK_FPGA) && res[i].pes_used <= res[i].dev->pes) {
			Dprintf("Found resource %d: %"PRIu64"/%"PRIu64" PEs used %"PRIu64
				"/%"PRIu64" MEM available",
				i, res[i].pes_used, res[i].dev->pes, res[i].mem_avail, 
				res[i].dev->mem_size);

			r->dev = i;

			/* Update next device */
			next_dev = (i + 1) % nresources;
			
			return i;
		}
	}
    r->num_attempts += 1;
    Dprintf("No resource for task available: (%d,%"PRIu64") PES: %"PRIu64" MEM: %"PRIu64" TYPE: 0x%"PRIx64"",
				r->key.pid, r->key.rid, r->pes, r->mem, r->type);

	return MCL_SCHED_BLOCK;
}

const struct sched_resource_policy rr_policy = {
	.init = rr_init_resources,
	.find_resource = rr_find_resource,
	.assign_resource = default_assign_resource,
	.put_resource = default_put_resource,
	.stats = default_stats
};

