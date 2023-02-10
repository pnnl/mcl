#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"
#include <minos.h>

static inline int test_results(FPTYPE *a, FPTYPE *b, size_t n) {
    int i, j;

    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            if (b[i * n + j] < (1.0 - TOLERANCE) * a[i * n + j] ||
                b[i * n + j] > (1.0 + TOLERANCE) * a[i * n + j]) {
                printf("Element %d,%d: %f != %f!\n", i, j,
                       a[i * n + j], b[i * n + j]);
                return 1;
            }

    return 0;
}

int test_mcl(uint64_t *X, uint64_t *V, size_t N) {
    struct timespec start, end;
    mcl_handle **hdl = NULL;
    uint64_t pes[MCL_DEV_DIMS] = {N, 1, 1};
    const size_t msize = N * sizeof(uint64_t);
    unsigned int i;
    unsigned int errs = 0;
    double rtime;
    int ret;
    unsigned long arg_flags = MCL_ARG_BUFFER | MCL_ARG_INPUT | MCL_ARG_RESIDENT | MCL_ARG_DYNAMIC;
    char src_path[1024];

    strcpy(src_path, XSTR(_MCL_TEST_PATH));
    strcat(src_path, "/integrate.cl");
    // fprintf(stderr, "Allocating handles.\n");

    hdl = (mcl_handle **)malloc(sizeof(mcl_handle *) * rep);
    if (!hdl) {
        printf("Error allocating memmory. Aborting.\n");
        goto err;
    }

    mcl_prg_load(src_path, "", MCL_PRG_SRC);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (i = 0; i < rep; i++) {
        hdl[i] = mcl_task_create();
        if (!hdl[i]) {
            printf("Error creating MCL task. Aborting.\n");
            goto err_hdl;
        }
        if (mcl_task_set_kernel(hdl[i], "VADD", 2)) {
            printf("Error setting %s kernel. Aborting.\n", "VADD");
            goto err_hdl;
        }
        if (i + 1 == rep)
            arg_flags |= MCL_ARG_OUTPUT;
        if (mcl_task_set_arg(hdl[i], 0, (void *)X, msize, arg_flags)) {
            printf("Error setting up task input A. Aborting.\n");
            goto err_hdl;
        }

        if (mcl_task_set_arg(hdl[i], 1, (void *)V, msize, MCL_ARG_BUFFER | MCL_ARG_INPUT | MCL_ARG_RESIDENT)) {
            printf("Error setting up task input B. Aborting.\n");
            goto err_hdl;
        }

        if (i == 0) {
            if ((ret = mcl_exec(hdl[i], pes, NULL, flags))) {
                printf("Error submitting task (%d)! Aborting.\n", ret);
                goto err_hdl;
            }
        }
        else {
            if (synct) {
                mcl_wait(hdl[i - 1]);
                ret = mcl_exec(hdl[i], pes, NULL, flags);
            }
            else {
                ret = mcl_exec_with_dependencies(hdl[i], pes, NULL, flags, 1, &hdl[i - 1]);
            }
            if (ret) {
                printf("Error submitting task (%d)! Aborting.\n", ret);
                goto err_hdl;
            }
        }
    }

    if (mcl_wait_all()) {
        printf("Error waiting for requests to complete!\n");
        goto err_hdl;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    for (i = 0; i < rep; i++)
        if (hdl[i]->ret == MCL_RET_ERROR) {
            printf("Error executing task %u!\n", i);
            errs++;
        }
    if (errs)
        printf("Detected %u errors!\n", errs);
    else {
        rtime = ((FPTYPE)tdiff(end, start)) / BILLION;
        printf("Done.\n  Test time : %f seconds\n", rtime);
        printf("  Throughput: %f tasks/s\n", ((FPTYPE)rep) / rtime);
    }

    for (i = 0; i < rep; i++)
        mcl_hdl_free(hdl[i]);
    free(hdl);

    return errs;

err_hdl:
    free(hdl);
err:
    return -1;
}

int main(int argc, char **argv) {
    uint64_t *X, *V;
    int i, ret = 0;

    mcl_banner("Task Dependency Test");

    parse_global_opts(argc, argv);

    switch (type) {
    case 0: {
        flags = MCL_TASK_CPU;
        break;
    }
    case 1: {
        flags = MCL_TASK_GPU;
        break;
    }
    case 2: {
        flags = MCL_TASK_ANY;
        break;
    }
    default: {
        printf("Unrecognized resource type (%" PRIu64 "). Aborting.\n", type);
        return -1;
    }
    }

    mcl_init(workers, 0x0);

    fprintf(stderr, "MCL initialized creating vectors.\n");

    X = (uint64_t *)malloc(size * sizeof(uint64_t));
    V = (uint64_t *)malloc(size * sizeof(uint64_t));

    if (!X || !V) {
        printf("Error allocating vectors. Aborting.");
        goto err;
    }

    srand48(13579862);
    for (i = 0; i < size; ++i) {
        X[i] = 0;
        V[i] = 1;
    }

    fprintf(stderr, "Initialized all the vectors, beginning test.\n");

    // Test 0
    ret = test_mcl(X, V, size);
    if (ret) {
        printf("Error performing computation (%d). Aborting.\n", ret);
        ret = -1;
    }

    ret = 0;
    for (i = 0; i < size; i++) {
        if (X[i] != rep) {
            ret = -1;
            printf("Error verifying the results.\n");
        }
    }

    mcl_finit();
    mcl_verify(ret);

    free(X);
    free(V);

err:
    return ret;
}
