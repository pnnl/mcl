#include <minos_sched_internal.h>
#define DELAY_SCHED_MAX_ATTEMPTS 8

static mcl_resource_t *res;
static int nresources;
static int next_dev;
static int max_attempts;

static void delay_init_resources(mcl_resource_t *r, int n) {
    res = r;
    nresources = n;
    next_dev = 0;

    char *value;
    if ((value = getenv("MCL_SCHED_MAX_ATTEMPTS")) != NULL) {
        max_attempts = atoi(value);
    }
    else {
        max_attempts = DELAY_SCHED_MAX_ATTEMPTS;
    }
}

static int has_mem_on_other_dev(uint64_t devs, int dev) {
    return devs && !((devs >> dev) & 0x1);
}

static int delay_find_resource(sched_req_t *r) {
    VDprintf("Locating resource for (%d,%" PRIu64 ") PES: %" PRIu64 " MEM: %" PRIu64 " TYPE: 0x%" PRIx64 "",
             r->key.pid, r->key.rid, r->pes, r->mem, r->type);

    int i = next_dev;
    int cnt = 0;
    int num_fit = 0;
    uint64_t devs = 0;
    uint64_t mem_max = 0;
    uint64_t *res_mem = malloc(sizeof(uint64_t) * nresources);
    memset(res_mem, 0, sizeof(uint64_t) * nresources);

    if (!(r->flags & MCL_FLAG_NO_RES)) {
        sched_rdata *el;
        for (int j = 0; j < r->nresident; j++) {
            el = r->resdata[j];
            for (int k = 0; k < nresources; k++) {
                if (((el->devs >> k) & 0x1)) {
                    res_mem[k] += r->resdata[j]->size;
                    if (res_mem[k] > mem_max)
                        mem_max = res_mem[k];
                }
            }
        }

        for (int j = 0; j < nresources; j++) {
            if (res_mem[j] == mem_max)
                devs |= (0x01 << j);
        }
    }

    do {
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
        case MCL_TASK_DF:
            mult = MCL_DEV_MUL_DF;
            break;
        }

        uint64_t needed_mem = r->mem - res_mem[i];
        Dprintf("\tNeeded on resource %d: %" PRIu64 " MEM", i, needed_mem);

        if ((res[i].dev->type & r->type) && ((res[i].mem_avail >= needed_mem) || (res[i].dev->type & MCL_TASK_FPGA)) && res[i].pes_used <= res[i].dev->pes * mult) {
            if (has_mem_on_other_dev(devs, i) && r->num_attempts < max_attempts) {
                num_fit += 1;
                r->num_attempts += 1;
            }
            else {
                Dprintf("Found resource %d: %" PRIu64 "/%" PRIu64 " PEs used %" PRIu64
                        "/%" PRIu64 " MEM available, %" PRIu64 " attempts",
                        i, res[i].pes_used, res[i].dev->pes, res[i].mem_avail,
                        res[i].dev->mem_size, r->num_attempts);

                r->dev = i;
                next_dev = (i + 1) % nresources;
                free(res_mem);
                return i;
            }
        }
        i = (i + 1) % nresources;
        cnt += 1;
    } while (cnt < nresources);

    free(res_mem);
    if (num_fit)
        return MCL_SCHED_AGAIN;

    return MCL_SCHED_BLOCK;
}

const struct sched_resource_policy delay_policy = {
    .init = delay_init_resources,
    .find_resource = delay_find_resource,
    .assign_resource = default_assign_resource,
    .put_resource = default_put_resource,
    .stats = default_stats};
