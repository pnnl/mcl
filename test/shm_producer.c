#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <signal.h>

#include <minos.h>
#include "utils.h"

#define MEM_NAME_LEN    64

mcl_handle* start_iteration(void* buffer, size_t size, off_t offset)
{
    unsigned long   arg_flags = MCL_ARG_INPUT|MCL_ARG_BUFFER|MCL_ARG_RESIDENT|MCL_ARG_DYNAMIC|MCL_ARG_SHARED;
    uint64_t        pes[MCL_DEV_DIMS] = {size, 1, 1};

    int* a = (int*) malloc(sizeof(int) * size);
    int* b = (int*) malloc(sizeof(int) * size);
    for(size_t i = 0; i < size; i++) {
        a[i] = 1;
        b[i] = 2;
    }
    
    mcl_handle* hdl = mcl_task_create_with_props(MCL_HDL_SHARED);
    if(!hdl) {
        fprintf(stderr, "Error creating fft handle. Aborting.\n");
        goto err;
    }
    if(mcl_task_set_kernel(hdl, "VADD", 3)){
        fprintf(stderr, "Error setting fft kernel. Aborting.\n");
        goto err;
    }

    if(mcl_task_set_arg(hdl, 0, a, size * sizeof(int), MCL_ARG_INPUT | MCL_ARG_BUFFER )){
        fprintf(stderr, "Error setting argument for vadd. Aborting.\n");
        goto err;
    }

    if(mcl_task_set_arg(hdl, 1, b, size * sizeof(int), MCL_ARG_INPUT | MCL_ARG_BUFFER )){
        fprintf(stderr, "Error setting argument for vadd. Aborting.\n");
        goto err;
    }

    if(mcl_task_set_arg_buffer(hdl, 2, buffer, size * sizeof(int), offset * sizeof(int), arg_flags)){
        fprintf(stderr, "Error setting argument for vadd. Aborting.\n");
        goto err;
    }

    if(mcl_exec(hdl, pes, NULL, flags)){
        fprintf(stderr, "Error executing fft kernel. Aborting.\n");
        goto err;
    }

    return hdl;

err:
    mcl_hdl_free(hdl);
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
    size_t bytes = size * sizeof(int);

    mcl_init(workers, 0x0);
    mcl_handle** hdls = malloc(sizeof(mcl_handle*) * rep);
    int* completed = malloc(sizeof(int) * rep);
    memset(completed, 0, sizeof(int) * rep);

    mcl_prg_load("./vadd.cl", "", MCL_PRG_SRC);

    pid_t pid = getpid();
    printf("%d\n", pid);
    fflush(stdout);

    char* name = "mcl_shm_test";
    void* data = mcl_get_shared_buffer(name, bytes * rep, MCL_SHARED_MEM_NEW | MCL_SHARED_MEM_DEL_OLD | MCL_ARG_DYNAMIC);

    for(i = 0; i < rep; i++){
        hdls[i] = start_iteration(data, size, i * size);
    }

    int total_completed = 0;
    i = 0;
    while(total_completed != rep){
        if(!completed[i] && mcl_test(hdls[i]) == MCL_REQ_COMPLETED){
            total_completed += 1;
            completed[i] = 1;
        }
        i = (i+1) % rep;
    }
    mcl_free_shared_buffer(data);

    close(STDOUT_FILENO);
    fprintf(stderr, "Calling mcl finit.");
    sleep(3);
    mcl_finit();

    free(hdls);
    free(completed);
    fprintf(stderr, "Exiting.\n");
}