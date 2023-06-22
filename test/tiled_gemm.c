#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"
#include <minos.h>

#define DOUBLE_PRECISION

#ifdef VERBOSE
void print_matrix(double *M, size_t n) {
    int i, j;

    printf("M = [ \n");
    for (i = 0; i < n; i++) {
        printf("     ");
        for (j = 0; j < n; j++)
            printf("%.4f ", M[i * n + j]);
        printf("\n");
    }
    printf("    ]\n");

    return;
}
#endif

static inline int test_results(double *a, double *b, size_t n) {
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

int test_mcl(double *A, double *B, double *C, size_t N) {
    /** A true tiled matrix multiply would separate a matrix on both rows and columns
     * however, this creates a different memory pattern that can't be as easily expressed
     * with the simple subbuffer interafe that we have created thus far. So as a functionality
     * test, I am just breaking matrix A, and matrix C by row. For the best performance on GEMM
     * we still need to implement more functionality **/

    char src_path[1024];

    strcpy(src_path, XSTR(_MCL_TEST_PATH));
    strcat(src_path, "/gemmN.cl");

    struct timespec start, end;
    mcl_handle **hdl = NULL;
    const size_t msize = N * N * sizeof(double);
    const size_t row_size = N * sizeof(double);
    const size_t tile_size = 64 * row_size; /** TODO: This should be parametric **/
    const size_t num_tiles = msize / tile_size;
    uint64_t pes[MCL_DEV_DIMS] = {64, N, 1};
    uint64_t i, j;
    unsigned int errs = 0;
    float rtime;
    int ret;

    printf("tile_size: %ld\n", tile_size);

    printf("MCL Test...");

    hdl = (mcl_handle **)malloc(sizeof(mcl_handle *) * num_tiles * rep);
    if (!hdl) {
        printf("Error allocating memmory for MCL hanlders. Aborting.\n");
        goto err;
    }

#ifdef DOUBLE_PRECISION
    mcl_prg_load(src_path, "-DDOUBLE_PRECISION", MCL_PRG_SRC);
#else
    mcl_prg_load(src_path, "-DSINGLE_PRECISION", MCL_PRG_SRC);
#endif

    clock_gettime(CLOCK_MONOTONIC, &start);
    mcl_register_buffer(C, msize, MCL_ARG_RESIDENT | MCL_ARG_DYNAMIC | MCL_ARG_OUTPUT);
    for (i = 0; i < rep; i++) {
        for (j = 0; j < num_tiles; j++) {
            hdl[(i * num_tiles) + j] = mcl_task_create();
            if (!hdl[(i * num_tiles) + j]) {
                printf("Error creating MCL task. Aborting.\n");
                goto err_hdl;
            }

            if (mcl_task_set_kernel(hdl[(i * num_tiles) + j], "gemmN", 4)) {
                printf("Error setting %s kernel. Aborting.\n", "gemmN");
                goto err_hdl;
            }

            if (mcl_task_set_arg(hdl[(i * num_tiles) + j], 0, (void *)A + (tile_size * j), tile_size, MCL_ARG_INPUT | MCL_ARG_BUFFER | MCL_ARG_RESIDENT)) {
                printf("Error setting up task input A. Aborting.\n");
                goto err_hdl;
            }

            if (mcl_task_set_arg(hdl[(i * num_tiles) + j], 1, (void *)B, msize, MCL_ARG_INPUT | MCL_ARG_BUFFER | MCL_ARG_RESIDENT)) {
                printf("Error setting up task input B. Aborting.\n");
                goto err_hdl;
            }

            if (mcl_task_set_arg(hdl[(i * num_tiles) + j], 2, (void *)&N, sizeof(int), MCL_ARG_INPUT | MCL_ARG_SCALAR)) {
                printf("Error setting up task input N. Aborting.\n");
                goto err_hdl;
            }

            if (mcl_task_set_arg_buffer(hdl[(i * num_tiles) + j], 3, (void *)C, tile_size, tile_size * j, MCL_ARG_BUFFER | MCL_ARG_DYNAMIC | MCL_ARG_RESIDENT | MCL_ARG_OUTPUT)) {
                printf("Error setting up task output. Aborting.\n");
                goto err_hdl;
            }

            if ((ret = mcl_exec(hdl[(i * num_tiles) + j], pes, NULL, flags))) {
                printf("Error submitting task (%d)! Aborting.\n", ret);
                goto err_hdl;
            }
        }
        if (synct)
            for (j = 0; j < num_tiles; j++) {
                if (mcl_wait(hdl[(i * num_tiles) + j])) {
                    printf("Request timed out!\n");
                    goto err_hdl;
                }
            }
    }

    if (!synct) {
        printf("Waiting for all MCL tasks to complete...");
        fflush(stdout);
        if (mcl_wait_all()) {
            printf("Error waiting for requests to complete!\n");
            goto err_hdl;
        }
        printf("done!\n");
    }
    mcl_unregister_buffer(C);
    clock_gettime(CLOCK_MONOTONIC, &end);

    for (i = 0; i < rep; i++)
        if (hdl[i]->ret == MCL_RET_ERROR) {
            printf("Error executing task %" PRIu64 "!\n", i);
            errs++;
        }
    if (errs)
        printf("Detected %u errors!\n", errs);
    else {
        rtime = ((float)tdiff(end, start)) / BILLION;
        printf("Done.\n  Test time : %f seconds\n", rtime);
        printf("  Throughput: %f tasks/s\n", ((float)rep) / rtime);
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

int gemm_seq(double *A, double *B, double *C, size_t N) {
    int i, j, k;

    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            for (k = 0; k < N; ++k) {
                C[i * N + j] += A[i * N + k] * B[k * N + j];
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    double *A, *B, *C, *C_test;
    int i, j, ret = -1;
    struct timespec start, end;

    mcl_banner("GEMM N Test");

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

    A = (double *)malloc(size * size * sizeof(double));
    B = (double *)malloc(size * size * sizeof(double));
    C = (double *)malloc(size * size * sizeof(double));
    C_test = (double *)malloc(size * size * sizeof(double));

    if (!A || !B || !C || !C_test) {
        printf("Error allocating vectors. Aborting.");
        goto err;
    }

    srand48(13579862);
    for (i = 0; i < size; ++i) {
        for (j = 0; j < size; ++j) {
            A[i * size + j] = (double)(0.5 + drand48() * 1.5);
        }
    }

    for (i = 0; i < size; ++i) {
        for (j = 0; j < size; ++j) {
            B[i * size + j] = (double)(0.5 + drand48() * 1.5);
        }
    }

    for (i = 0; i < size; ++i) {
        for (j = 0; j < size; ++j) {
            C[i * size + j] = 0.0;
            C_test[i * size + j] = 0.0;
        }
    }

#ifdef VERBOSE
    print_matrix(A, size);
    printf("\n");
    print_matrix(B, size);
#endif

    if (verify) {
        printf("Sequential Test...");
        clock_gettime(CLOCK_MONOTONIC, &start);
        gemm_seq(A, B, C_test, size);
        clock_gettime(CLOCK_MONOTONIC, &end);

        printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end, start)) / BILLION);

#ifdef VERBOSE
        print_matrix(C_test, size);
#endif

        printf("\n");
    }

    ret = test_mcl(A, B, C, size);
    if (verify && !ret) {

#ifdef VERBOSE
        print_matrix(C, size);
#endif
        mcl_finit();
        mcl_verify(ret);

        free(A);
        free(B);
        free(C);
        free(C_test);
	}

err:
	return ret;
}
