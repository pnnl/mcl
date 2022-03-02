#include <minos_sched.h>
#include <math.h>
#define HYBRID_SCHED_MAX_ATTEMPTS   16
#define HYBRID_SCHED_COPY_FACTOR    16

static mcl_resource_t *res;
static int nresources;
static int next_dev;
static double copy_factor;
static int max_attempts;

static void hybrid_init_resources(mcl_resource_t *r, int n)
{
	res = r;
	nresources = n;
    next_dev = 0;
    char* value;
    if((value = getenv("MCL_SCHED_MAX_ATTEMPTS")) != NULL){
        max_attempts = atoi(value);
    } else {
        max_attempts = HYBRID_SCHED_MAX_ATTEMPTS;
    }

    value = NULL;
    if((value = getenv("MCL_SCHED_COPY_FACTOR")) != NULL){
        copy_factor = atof(value);
    } else {
        copy_factor = HYBRID_SCHED_COPY_FACTOR;
    }

    Dprintf("Initialized Hybrid scheduler with copy factor %f, and max attempts %d", copy_factor, max_attempts);
}

static int has_mem_on_other_dev(uint64_t devs, int dev)
{
    return devs && !((devs >> dev) & 0x1);
}

static int hybrid_find_resource(sched_req_t *r)
{	
	VDprintf("Locating resource for (%d,%"PRIu64") PES: %"PRIu64" MEM: %"PRIu64" TYPE: 0x%"PRIx64"",
		r->pid, r->rid, r->pes, r->mem, r->type);

    int i = next_dev;
    int cnt = 0;
    int num_fit = 0;
    uint64_t devs = 0;
    uint64_t mem_max = 0;
    uint64_t* res_mem = malloc(sizeof(uint64_t) * nresources);
    memset(res_mem, 0, sizeof(uint64_t) * nresources);

    if(!(r->flags & MCL_FLAG_NO_RES)){
        sched_rdata* el;
        for(int j = 0; j < r->nresident; j++){
            el = r->resdata[j];
            uint64_t max_copies = copy_factor != 1 ? (int)(log((double)el->refs)/log(copy_factor)) : el->refs - 1;
            for(int k = 0; k < nresources; k++){
                if((max_copies < el->ndevs) 
                        && ((el->devs >> k) & 0x1)) {
                    res_mem[k] += r->resdata[j]->size;
                    if(res_mem[k] > mem_max)
                        mem_max = res_mem[k];
                }
            }
            Dprintf("For request (%d, %"PRIu64"), found MEMID: %"PRIu64", REFs: %"PRIu64", NDEVS: %"PRIu64", MAX COPIES:%"PRIu64"",
                r->pid, r->rid, r->resdata[j]->mem_id, r->resdata[j]->refs, r->resdata[j]->ndevs, 
                max_copies + 1
            );  
        }

        for(int j = 0; j < nresources; j++){
            if(mem_max != 0 && res_mem[j] == mem_max)
                devs |= (0x01 << j);
        }
    }

    do {
        uint64_t needed_mem = r->mem - res_mem[i];
        Dprintf("\tNeeded on resource %d: %"PRIu64" MEM, %"PRIu64" MEM available", i, needed_mem, res[i].mem_avail);

        if((res[i].dev->type & r->type) && (res[i].mem_avail >= needed_mem) && (res[i].nkernels < res[i].dev->max_kernels)){
            if(has_mem_on_other_dev(devs, i) && r->num_attempts < max_attempts){
                num_fit += 1;
                r->num_attempts += 1;
            } else {
                Dprintf("Found resource %d: %"PRIu64"/%"PRIu64" PEs used %"PRIu64
                "/%"PRIu64" MEM available, %"PRIu64" attempts",
                i, res[i].pes_used, res[i].dev->pes, res[i].mem_avail, 
                res[i].dev->mem_size, r->num_attempts);

                r->dev = i;
                next_dev = (i + 1) % nresources;
                free(res_mem);
                return i;
            }
        }
        i = (i+1) % nresources;
        cnt += 1;
	}while(cnt < nresources);

    free(res_mem);
    if(num_fit)
        return MCL_SCHED_AGAIN;

    return MCL_SCHED_BLOCK;
}

const struct sched_resource_policy hybrid_policy = {
	.init = hybrid_init_resources,
	.find_resource = hybrid_find_resource,
	.assign_resource = default_assign_resource,
	.put_resource = default_put_resource,
};
