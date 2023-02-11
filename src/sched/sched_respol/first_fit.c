
#include <atomics.h>
#include <minos_sched_internal.h>

static mcl_resource_t *res;
static int nresources;

static void ff_init_resources(mcl_resource_t *r, int n) {
    res = r;
    nresources = n;
}

static int ff_find_resource(sched_req_t *r) {
    int i;

    Dprintf("Locating resource for (%d,%" PRIu64 ") PES: %" PRIu64 " TYPE: 0x%" PRIx64 " out of %d resources",
            r->key.pid, r->key.rid, r->pes, r->type, nresources);

    for (i = 0; i < nresources; i++) {
        int stop = 0;
        Dprintf("\t Res %d -> type 0x%" PRIx64 " pes %" PRIu64 " mem %" PRIu64 "", i, res[i].dev->type, res[i].dev->pes, res[i].dev->mem_size);

        if (!(res[i].dev->type & r->type))
            continue;

        if (r->type == MCL_TASK_FPGA && res[i].dev->type == MCL_TASK_FPGA)

            goto found;

        for (int j = 0; j < MCL_DEV_DIMS && !stop; j++) {
            if (res[i].dev->wisize[j] == 1 && r->dpes[j] > 1)
                stop = 1;
        }

        if (stop)
            continue;

        for (int j = 0; j < MCL_DEV_DIMS; j++)
            if (r->lpes[j] > res[i].dev->wisize[j])
                continue;

        for (int j = 0; j < MCL_DEV_DIMS; j++)
            if (r->lpes[j])
                if (r->dpes[j] / r->lpes[j] > res[i].dev->wgsize)
                    continue;

        if (ld_acq(&res[i].mem_avail) < r->mem)
            continue;
    found:
        Dprintf("\t Found resource %d %" PRIu64 "/%" PRIu64 " PEs used",
                i, res[i].pes_used, res[i].dev->pes);

        r->dev = i;

        return i;
    }
    r->num_attempts += 1;
    return MCL_SCHED_BLOCK;
}

const struct sched_resource_policy ff_policy = {
    .init = ff_init_resources,
    .find_resource = ff_find_resource,
    .assign_resource = default_assign_resource,
    .put_resource = default_put_resource,
    .stats = default_stats};
