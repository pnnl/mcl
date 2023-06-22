#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"
#include <minos.h>

// #define VERBOSE
#ifdef DOUBLE_PRECISION
#define FTYPE double
#else
#define FTYPE float
#endif

#ifdef VERBOSE
void print_matrix(FTYPE *M, size_t n) {
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

static inline int test_results(FTYPE *a, size_t n) {
    int i, j;

    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            if (a[i * n + j] != (FTYPE)n) {
                printf("Element %d,%d: %f != %f!\n", i, j,
                       a[i * n + j], (FTYPE)n);
                return 1;
            }

    return 0;
}

#ifdef __TEST_MCL
int test_mcl(FTYPE *A, FTYPE *B, FTYPE *C, size_t N) {
    struct timespec start, end;
    mcl_handle **hdl = NULL;
    uint64_t pes[MCL_DEV_DIMS] = {N, N, 1};
    const size_t msize = N * N * sizeof(FTYPE);
    uint64_t i;
    unsigned int errs = 0;
    float rtime;
    int ret;
    char src_path[1024];

    strcpy(src_path, XSTR(_MCL_TEST_PATH));
    strcat(src_path, "/gemmN.cl");

    printf("MCL Test...");

    hdl = (mcl_handle **)malloc(sizeof(mcl_handle *) * rep);
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
    for (i = 0; i < rep; i++) {

        hdl[i] = mcl_task_create();
        if (!hdl[i]) {
            printf("Error creating MCL task. Aborting.\n");
            goto err_hdl;
        }

        if (mcl_task_set_kernel(hdl[i], "gemmN", 4)) {
            printf("Error setting %s kernel. Aborting.\n", "gemmN");
            goto err_hdl;
        }

        if (mcl_task_set_arg(hdl[i], 0, (void *)A, msize, MCL_ARG_INPUT | MCL_ARG_BUFFER | MCL_ARG_RESIDENT)) {
            printf("Error setting up task input A. Aborting.\n");
            goto err_hdl;
        }

        if (mcl_task_set_arg(hdl[i], 1, (void *)B, msize, MCL_ARG_INPUT | MCL_ARG_BUFFER | MCL_ARG_RESIDENT)) {
            printf("Error setting up task input B. Aborting.\n");
            goto err_hdl;
        }

        if (mcl_task_set_arg(hdl[i], 2, (void *)&N, sizeof(int), MCL_ARG_INPUT | MCL_ARG_SCALAR)) {
            printf("Error setting up task input N. Aborting.\n");
            goto err_hdl;
        }

        if (mcl_task_set_arg(hdl[i], 3, (void *)C, msize, MCL_ARG_OUTPUT | MCL_ARG_BUFFER)) {
            printf("Error setting up task output. Aborting.\n");
            goto err_hdl;
        }

        if ((ret = mcl_exec(hdl[i], pes, NULL, flags))) {
            printf("Error submitting task (%d)! Aborting.\n", ret);
            goto err_hdl;
        }

        if (synct)
            if (mcl_wait(hdl[i])) {
                printf("Request timed out!\n");
                goto err_hdl;
            }
    }

    if (!synct)
        if (mcl_wait_all()) {
            printf("Error waiting for requests to complete!\n");
            goto err_hdl;
        }
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
#endif

int gemm_seq(FTYPE *A, FTYPE *B, FTYPE *C, size_t N) {
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

#ifdef __TEST_OCL
int test_ocl(FTYPE *A, FTYPE *B, FTYPE *C, size_t N) {
    cl_device_id *device;
    struct timespec start, end;
    char *src_code = NULL;
    size_t src_size = 0;
    cl_int ret = 0;
    cl_context context;
    cl_command_queue queue;
    cl_mem A_o, B_o, C_o;
    cl_program program = NULL;
    cl_kernel kernel = NULL;
    cl_uint numDimensions = 0;
    size_t *maxWorkSizes;
    size_t globalWorkSize[] = {N, N, 1};
    size_t localWorkSize[] = {16, 16, 1};
    size_t kwgroup;
    float rtime;
    int i;

    printf("OpenCL Test...");

    if (mcl_load("./gemmN.cl", &src_code)) {
        printf("Error loading OpenCL kernel! Aborting.\n");
        goto err;
    }

#ifdef VERBOSE
    printf("\n\n %s \n\n", src_code);
#endif

    if (ocl_setup(&device, &context, &queue, flags, did))
        goto err_src;

    ret = clGetDeviceInfo(device[did], CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS,
                          sizeof(cl_uint), &numDimensions, NULL);
    if (ret != CL_SUCCESS) {
        printf("Error quering device %" PRIu64 " info. Aborting!\n", did);
        goto err_src;
    }
    maxWorkSizes = (size_t *)malloc(numDimensions * sizeof(size_t));
    if (!maxWorkSizes) {
        printf("Error allocating work sizes. Aborting!\n");
        goto err_src;
    }
    clGetDeviceInfo(device[did], CL_DEVICE_MAX_WORK_ITEM_SIZES,
                    sizeof(size_t) * numDimensions, maxWorkSizes, NULL);

    if (numDimensions < 2) {
        printf("SGEMM needs a 2-dimensional work group size.\n");
        goto err_src;
    }

    src_size = strlen(src_code);
    program = clCreateProgramWithSource(context, 1, (const char **)&src_code,
                                        (const size_t *)&src_size, &ret);
    if (ret != CL_SUCCESS) {
        printf("Error creating program! Aborting. (%d)\n", ret);
        goto err_C;
    }
#ifdef DOUBLE_PRECISION
    ret = clBuildProgram(program, 1, &(device[did]), "-DDOUBLE_PRECISION", NULL, NULL);
#else
    ret = clBuildProgram(program, 1, &(device[did]), "-DSINGLE_PRECISION", NULL, NULL);
#endif
    if (ret != CL_SUCCESS) {
        printf("Error building program! Aborting. (%d)\n", ret);
        goto err_program;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (i = 0; i < rep; i++) {
        A_o = clCreateBuffer(context, CL_MEM_READ_ONLY, N * N * sizeof(FTYPE), NULL, &ret);
        if (ret != CL_SUCCESS) {
            printf("Error creating memory object for matrix A! Aborting. (%d)\n", ret);
            goto err_setup;
        }

        B_o = clCreateBuffer(context, CL_MEM_READ_ONLY, N * N * sizeof(FTYPE), NULL, &ret);

        if (ret != CL_SUCCESS) {
            printf("Error creating memory object for matrix B! Aborting. (%d)\n", ret);
            goto err_A;
        }

        C_o = clCreateBuffer(context, CL_MEM_READ_WRITE, N * N * sizeof(FTYPE), NULL, &ret);
        if (ret != CL_SUCCESS) {
            printf("Error creating memory object for matrix C! Aborting. (%d)\n", ret);
            goto err_B;
        }

        ret = clEnqueueWriteBuffer(queue, A_o, CL_TRUE, 0, N * N * sizeof(FTYPE), A, 0,
                                   NULL, NULL);
        if (ret != CL_SUCCESS) {
            printf("Error copying data to CL device buffer for matrix A! Aborting. (%d)\n",
                   ret);
            goto err_C;
        }

        ret = clEnqueueWriteBuffer(queue, B_o, CL_TRUE, 0, N * N * sizeof(FTYPE), B, 0,
                                   NULL, NULL);
        if (ret != CL_SUCCESS) {
            printf("Error copying data to CL device buffer for matrix B! Aborting. (%d)\n",
                   ret);
            goto err_C;
        }
        clFlush(queue);

        kernel = clCreateKernel(program, "gemmN", &ret);
        if (ret != CL_SUCCESS) {
            printf("Error creating kernel! Aborting. (%d)\n", ret);
            goto err_program;
        }

        ret = clGetKernelWorkGroupInfo(kernel, device[did], CL_KERNEL_WORK_GROUP_SIZE,
                                       sizeof(size_t), &kwgroup, NULL);

        if (ret != CL_SUCCESS) {
            printf("Error querying the kernel work group size! Aborting. (%d)\n", ret);
            goto err_program;
        }
        localWorkSize[0] = localWorkSize[1] = (size_t)sqrt((double)kwgroup);

        ret = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&A_o);
        ret |= clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *)&B_o);
        ret |= clSetKernelArg(kernel, 2, sizeof(int), (void *)&N);
        ret |= clSetKernelArg(kernel, 3, sizeof(cl_mem), (void *)&C_o);

        if (ret != CL_SUCCESS) {
            printf("Error setting kernel parameters! Aborting. (%d)\n", ret);
            goto err_kernel;
        }

        ret = clEnqueueNDRangeKernel(queue, kernel, 3, NULL, globalWorkSize, localWorkSize,
                                     0, NULL, NULL);
        if (ret != CL_SUCCESS) {
            printf("Error enqueuing kernel! Aborting. (%d)\n", ret);
            goto err_kernel;
        }
        ret = clEnqueueReadBuffer(queue, C_o, CL_TRUE, 0, N * N * sizeof(FTYPE), C, 0, NULL, NULL);
        if (ret != CL_SUCCESS) {
            printf("Error copying data back from device! (%d)\n", ret);
            goto err_kernel;
        }

        clFlush(queue);
        clReleaseKernel(kernel);
        clReleaseMemObject(A_o);
        clReleaseMemObject(B_o);
        clReleaseMemObject(C_o);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    rtime = ((float)tdiff(end, start)) / BILLION;
    printf("Done.\n  Test time : %f seconds\n", rtime);
    printf("  Throughput: %f tasks/s\n", ((float)rep) / rtime);

    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);

    free(src_code);

    return 0;

err_kernel:
    clReleaseKernel(kernel);
err_program:
    clReleaseProgram(program);
err_C:
    clReleaseMemObject(C_o);
err_B:
    clReleaseMemObject(B_o);
err_A:
    clReleaseMemObject(A_o);
err_setup:
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
err_src:
    free(src_code);
err:
    return -1;
}
#endif

int main(int argc, char **argv) {
    FTYPE *A, *B, *C, *C_test;
    int i, j, ret = -1;
    struct timespec start, end;

    mcl_banner("GEMM N Test");

    parse_global_opts(argc, argv);

    switch (type) {
#ifdef __TEST_MCL
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
#elif __TEST_OCL
    case 0: {
        flags = CL_DEVICE_TYPE_CPU;
        break;
    }
    case 1: {
        flags = CL_DEVICE_TYPE_GPU;
        break;
    }
#endif
    default: {
        printf("Unrecognized resource type (%" PRIu64 "). Aborting.\n", type);
        return -1;
    }
    }

#ifdef __TEST_MCL
    mcl_init(workers, 0x0);
#endif

    A = (FTYPE *)malloc(size * size * sizeof(FTYPE));
    B = (FTYPE *)malloc(size * size * sizeof(FTYPE));
    C = (FTYPE *)malloc(size * size * sizeof(FTYPE));
    C_test = (FTYPE *)malloc(size * size * sizeof(FTYPE));

    if (!A || !B || !C || !C_test) {
        printf("Error allocating vectors. Aborting.");
        goto err;
    }

    for (i = 0; i < size; ++i) {
        for (j = 0; j < size; ++j) {
            A[i * size + j] = (FTYPE)(1.0);
            B[i * size + j] = (FTYPE)(1.0);
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

#ifdef __TEST_OCL
    ret = test_ocl(A, B, C, size);
#else
    ret = test_mcl(A, B, C, size);
#endif

    if (!ret) {
#ifdef VERBOSE
        print_matrix(C, size);
#endif
        ret = test_results(C, size);
    }

#ifdef __TEST_MCL
    mcl_finit();
#endif
    mcl_verify(ret);

    free(A);
    free(B);
    free(C);
    free(C_test);
err:
    return ret;
}
