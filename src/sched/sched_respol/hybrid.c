#include <math.h>
#include <minos_sched_internal.h>
#include <utlist.h>

#define HYBRID_SCHED_MAX_ATTEMPTS 16
#define HYBRID_SCHED_COPY_FACTOR 1

typedef struct policy_dev_struct {
    int device;
    struct policy_dev_struct *next;
    struct policy_dev_struct *prev;
} policy_dev_t;

static mcl_resource_t *res;
static int nresources;
static double copy_factor;
static int max_attempts;
static policy_dev_t *device_list;

static void hybrid_init_resources(mcl_resource_t *r, int n) {
    res = r;
    nresources = n;
    for (int i = 0; i < nresources; i++) {
        policy_dev_t *dev = malloc(sizeof(policy_dev_t));
        dev->device = i;
        DL_APPEND(device_list, dev);
    }

    char *value;
    if ((value = getenv("MCL_SCHED_MAX_ATTEMPTS")) != NULL) {
        max_attempts = atoi(value);
    }
    else {
        max_attempts = HYBRID_SCHED_MAX_ATTEMPTS;
    }

    value = NULL;
    if ((value = getenv("MCL_SCHED_COPY_FACTOR")) != NULL) {
        copy_factor = atof(value);
    }
    else {
        copy_factor = HYBRID_SCHED_COPY_FACTOR;
    }

    Dprintf("Initialized Hybrid scheduler with copy factor %f, and max attempts %d", copy_factor, max_attempts);
}

static int calculate_resident_memory(mcl_partition_t *region, uint64_t device, sched_rdata *rdata) {
    mcl_partition_t sentinel = {0, 0, region->offset + region->size - 1, -1, -1};
    int64_t cur_idx = list_search_prev(&rdata->subbuffers, &sentinel);
    mcl_partition_t *cur = list_get(&rdata->subbuffers, cur_idx);
    uint64_t memory = 0;
    while (cur && cur->offset + cur->size > region->offset) {
        if (cur->dev == device) {
            memory += region->offset + region->size - cur->offset < cur->size ? region->offset + region->size - cur->offset : cur->size;
        }
        cur_idx = cur->prev;
        cur = list_get(&rdata->subbuffers, cur_idx);
    }
    return memory;
}

static int has_mem_on_other_dev(uint64_t devs, int dev) {
    return devs && !((devs >> dev) & 0x1);
}

static int hybrid_find_resource(sched_req_t *r) {
    VDprintf("Locating resource for (%d,%" PRIu64 ") PES: %" PRIu64 " MEM: %" PRIu64 " TYPE: 0x%" PRIx64 "",
             r->key.pid, r->key.rid, r->pes, r->mem, r->type);

    int num_fit = 0;
    uint64_t devs = 0;
    uint64_t mem_max = 0;
    uint64_t allocated_max = 0;
    uint64_t *res_mem = malloc(sizeof(uint64_t) * nresources);
    uint64_t *allocated_mem = malloc(sizeof(uint64_t) * nresources);
    memset(res_mem, 0, sizeof(uint64_t) * nresources);
    memset(allocated_mem, 0, sizeof(uint64_t) * nresources);

    if (!(r->flags & MCL_FLAG_NO_RES)) {
        sched_rdata *el;
        for (int j = 0; j < r->nresident; j++) {
            el = r->resdata[j];
            uint64_t max_copies = copy_factor != 1 ? (int)(log((double)el->refs) / log(copy_factor)) : el->ndevs;
            for (int k = 0; k < nresources; k++) {
                if ((max_copies < el->ndevs || el->flags & MSG_ARGFLAG_EXCLUSIVE) && ((el->devs >> k) & 0x1)) {
                    if (el->flags & MSG_ARGFLAG_EXCLUSIVE) {
                        res_mem[k] += calculate_resident_memory(&r->regions[j], (uint64_t)k, r->resdata[j]);
                    }
                    else {
                        res_mem[k] += r->resdata[j]->size;
                    }
                    allocated_mem[k] += r->resdata[j]->size;
                    if (res_mem[k] > mem_max)
                        mem_max = res_mem[k];
                    if (allocated_mem[k] > allocated_max)
                        allocated_max = allocated_mem[k];
                }
            }
            Dprintf("For request (%d, %" PRIu64 "), found MEMID: %" PRIu64 ", REFs: %" PRIu64 ", NDEVS: %" PRIu64 ", MAX COPIES:%" PRIu64 "",
                    r->key.pid, r->key.rid, r->resdata[j]->mem_id, r->resdata[j]->refs, r->resdata[j]->ndevs,
                    max_copies + 1);
        }

        for (int j = 0; j < nresources; j++) {
            if (allocated_max != 0 && res_mem[j] == mem_max && allocated_mem[j] == allocated_max)
                devs |= (0x01 << j);
        }
    }

    int i;
    policy_dev_t *el, *tmp;
    LL_FOREACH_SAFE(device_list, el, tmp) {
        i = el->device;
        uint64_t needed_mem = r->mem - allocated_mem[i];
        Dprintf("\tNeeded on resource %d: %" PRIu64 " MEM, %" PRIu64 " MEM available", i, needed_mem, res[i].mem_avail);
        uint64_t mult = 1;
        switch (res[i].dev->type) {
        case MCL_TASK_GPU:
            mult = MCL_DEV_MUL_GPU;
            break;
        case MCL_TASK_CPU:
            mult = MCL_DEV_MUL_CPU;
            break;
        case MCL_TASK_FPGA:
            mult = MCL_DEV_MUL_FPGA;
            break;
        default:
            // case MCL_TASK_VX:
            if(strstr(res[i].dev->name, "vortex") != NULL) {
                mult = MCL_DEV_MUL_VX;
            }
            // case MCL_TASK_DF:
            else {
                mult = MCL_DEV_MUL_DF;
            }
            break;
        }

        if (!(res[i].dev->type & r->type) || (res[i].pes_used > res[i].dev->pes * mult)) {
            continue;
        }

        if (res[i].dev->type & MCL_TASK_FPGA) {
            r->dev = i;
            free(res_mem);
            free(allocated_mem);
            DL_DELETE(device_list, el);
            DL_APPEND(device_list, el);
            return i;
        }

        if (res[i].mem_avail >= needed_mem)
            num_fit += 1;

        if ((has_mem_on_other_dev(devs, i) || res[i].mem_avail < needed_mem) && r->num_attempts < max_attempts) {
            // This is not the best device, so wait on another device
            r->num_attempts += 1;
            continue;
        }

        while (res[i].mem_avail < needed_mem) {
            // Try to evict mem. If we can't, break
            if (scheduler_evict_mem(i) < 0)
                break;
        }

        if (res[i].mem_avail < needed_mem) {
            // If there still isn't space on the device, move on
            continue;
        }

        Dprintf("Found resource %d: %" PRIu64 "/%" PRIu64 " PEs used %" PRIu64
                "/%" PRIu64 " MEM available, %" PRIu64 " attempts",
                i, res[i].pes_used, res[i].dev->pes, res[i].mem_avail,
                res[i].dev->mem_size, r->num_attempts);

        r->dev = i;
        free(res_mem);
        free(allocated_mem);
        DL_DELETE(device_list, el);
        DL_APPEND(device_list, el);
        return i;
    }

    free(res_mem);
    free(allocated_mem);
    if (num_fit)
        return MCL_SCHED_AGAIN;

    return MCL_SCHED_BLOCK;
}

const struct sched_resource_policy hybrid_policy = {
    .init = hybrid_init_resources,
    .find_resource = hybrid_find_resource,
    .assign_resource = default_assign_resource,
    .put_resource = default_put_resource,
    .stats = default_stats};
