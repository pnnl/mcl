#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>

#include <minos.h>
#include "utils.h"

#define MEM_NAME_LEN    64

uint64_t half_n_ffts;
uint64_t half_n_cmplx;
uint64_t N;
uint64_t bytes;

mcl_handle* start_iteration(void* buffer, void* out, size_t size, off_t offset)
{
    printf("Starting Iteration.\n");
    unsigned long   arg_flags = MCL_ARG_BUFFER|MCL_ARG_RESIDENT|MCL_ARG_DYNAMIC|MCL_ARG_SHARED;
    uint64_t        pes[MCL_DEV_DIMS] = {size, 1, 1};

    int* b = (int*) malloc(sizeof(int) * size);
    for(size_t i = 0; i < size; i++) {
        ((int*)out)[i] = 0;
        b[i] = 2;
    }

    printf("Getting shared buffer from MCL.\n");
    mcl_handle* hdl = mcl_task_create();
    if(!hdl) {
        fprintf(stderr, "Error creating vadd handle. Aborting.\n");
        goto err;
    }
    printf("Setting Kernel.\n");
    if(mcl_task_set_kernel(hdl, "VADD", 3)){
        fprintf(stderr, "Error setting vadd kernel. Aborting.\n");
        goto err;
    }

    printf("Setting argument.\n");
    if(mcl_task_set_arg_buffer(hdl, 0, (void*)buffer, size * sizeof(int), offset * sizeof(int), arg_flags)){
        fprintf(stderr, "Error setting argument for fft. Aborting.\n");
        goto err;
    }

    if(mcl_task_set_arg(hdl, 1, b, size * sizeof(int), MCL_ARG_INPUT | MCL_ARG_BUFFER )){
        fprintf(stderr, "Error setting argument for vadd. Aborting.\n");
        goto err;
    }

    if(mcl_task_set_arg(hdl, 2, out, size * sizeof(int), MCL_ARG_INPUT | MCL_ARG_OUTPUT | MCL_ARG_BUFFER )){
        fprintf(stderr, "Error setting argument for vadd. Aborting.\n");
        goto err;
    }

    printf("Executing Kernel.\n");
    if(mcl_exec(hdl, pes, NULL, flags)){
        fprintf(stderr, "Error executing fft kernel. Aborting.\n");
        goto err;
    }

    return hdl;

err:
    mcl_hdl_free(hdl);
    mcl_free_shared_buffer(buffer);
    buffer = NULL;
    return NULL;
}


int main(int argc, char** argv)
{
    int i;

    parse_global_opts(argc, argv);
    switch(type){
        case 0:{
            flags = MCL_TASK_CPU;
            break;
        }
        case 1:{
            flags = MCL_TASK_GPU;
            break;
        }
        case 2:{
            flags = MCL_TASK_ANY;
            break;
        }
        default:{
            printf("Unrecognized resource type (%"PRIu64"). Aborting.\n",type);
            return -1;
        }
    }
    bytes = size * sizeof(int);

    /** TODO: Read the data from stdin and prcocess it**/
    printf("Initializing Client.\n");
    mcl_init(workers, 0x0);
    mcl_prg_load("./vadd.cl", "", MCL_PRG_SRC);

    mcl_handle** hdls = calloc(rep, sizeof(mcl_handle*));
    int* completed = malloc(sizeof(int) * rep);
    memset(completed, 0, sizeof(int) * rep);

    size_t len;
    char* temp;
    for(int j = 0; j < 7; j++)
    {
        len = 0;
        temp = NULL;
        int r = getline(&temp, &len, stdin);
        if(r == -1) {
            fprintf(stderr, "Could not read line.\n");
            exit(-1);
        }
        free(temp);
    }
    pid_t producer;
    int err = scanf("%d\n", &producer);
    if(err == -1) {
        fprintf(stderr, "Could not read producer.\n");
        exit(-1);
    }
    fprintf(stdout, "Read producer as: %d\n", producer);

    char* name = "mcl_shm_test";
    void* data = mcl_get_shared_buffer(name, bytes * rep, MCL_ARG_DYNAMIC);
    int* out = (int*) malloc(bytes);

    i = 0;
    int started = 0;
    int ret;
    while(started < rep){
        fprintf(stdout, "Testing handle: %d\n", i);
        if(!hdls[i] && (ret = mcl_test_shared_hdl(producer, (uint32_t)i))  == MCL_REQ_COMPLETED){
            started += 1;
            fprintf(stdout, "Starting iteration: %d.\n", i);
            hdls[i] = start_iteration(data, out, size, i*size);
        } else {
            sleep(1);
        }
        i = (i+1) % rep;
    }

    int total_completed = 0;
    i = 0;
    while(total_completed != rep){
        if(!completed[i] && mcl_test(hdls[i]) == MCL_REQ_COMPLETED){
            total_completed += 1;
            completed[i] = 1;
            printf("Completed buffer %d\n", i);
            mcl_hdl_free(hdls[i]);
        }
        i = (i+1) % rep;
    }

    if(verify) {
        printf("Verifying results...");
        for(size_t j = 0; j < size; j++){
            if(out[j] != 5){
                fprintf(stdout, "Failed to verify results from Shared Mem.\n");
                fprintf(stdout, "Idx: %ld, Expected: 5, Output: %d.\n", j, out[j]);
                break;
            }
        }
        printf("Done.\n");
    }
    mcl_free_shared_buffer(data);

    free(hdls);
    free(completed);

    mcl_finit();
    return 0;
}