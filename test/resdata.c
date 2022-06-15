#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>

#include <minos.h>
#include "utils.h"

static inline int test_results(FPTYPE* a, FPTYPE* b, size_t n)
{
	int i, j;

	for(i=0; i<n; i++)
		for(j=0; j<n; j++)
			if(b[i*n+j] < (1.0 - TOLERANCE)*a[i*n+j] ||
			   b[i*n+j] > (1.0 + TOLERANCE)*a[i*n+j]){
				printf("Element %d,%d: %f != %f!\n", i, j,
				       a[i*n+j], b[i*n+j]);
				return 1;
			}

	return 0;
}

int test_mcl(FPTYPE* A, FPTYPE* B, FPTYPE* C, size_t N, int test_type)
{
	struct timespec start, end;
	mcl_handle**    hdl = NULL;
	uint64_t        pes[MCL_DEV_DIMS] = {N, N, 1};
	const size_t    msize    = N * N * sizeof(FPTYPE);
	unsigned int    i;
	unsigned int    errs = 0;
	double          rtime;
	int             ret;
        unsigned long   arg_flags = MCL_ARG_INPUT|MCL_ARG_BUFFER;

	printf("Test %d (%s)...", test_type, XSTR(FPTYPE));
	hdl = (mcl_handle**) malloc(sizeof(mcl_handle*) * rep);
	if(!hdl){
		printf("Error allocating memmory. Aborting.\n");
		goto err;
	}

    clock_gettime(CLOCK_MONOTONIC,&start);
    switch(test_type){
        case 1:
            arg_flags |= MCL_ARG_RESIDENT;
            break;
        case 2:
            arg_flags |= MCL_ARG_INVALID;
            break;
        case 3:
            arg_flags |= MCL_ARG_RESIDENT;
            mcl_transfer* t = mcl_transfer_create(2, 1, 0x0);
            if(mcl_transfer_set_arg(t, 0, (void*) A, msize, 0, arg_flags)){
                printf("Error setting transfer argument.\n");
                return 1;
            }
            if(mcl_transfer_set_arg(t, 1, (void*) B, msize, 0, arg_flags)){
                printf("Error setting transfer argument.\n");
                return 1;
            }
            if(mcl_transfer_exec(t, flags)){
                printf("Error executing transfer.\n");
                return 1;
            }
            if(mcl_transfer_wait(t)){
                printf("Error executing transfer.\n");
                return 1;
            }
            mcl_transfer_free(t);
		default:
			break;
    }

	
	for(i=0; i<rep; i++){

		hdl[i] = mcl_task_create();
		if(!hdl[i]){
			printf("Error creating MCL task. Aborting.\n");
			continue;
		}
                
                if(mcl_task_set_kernel(hdl[i], "gemmN", 4)){
			printf("Error setting %s kernel. Aborting.\n", "gemmN");
			continue;
		}

		if(mcl_task_set_arg(hdl[i], 0, (void*) A, msize, arg_flags)){
			printf("Error setting up task input A. Aborting.\n");
			continue;
		}

		if(mcl_task_set_arg(hdl[i], 1, (void*) B, msize, arg_flags)){
			printf("Error setting up task input B. Aborting.\n");
			continue;
		}

		if(mcl_task_set_arg(hdl[i], 2, (void*) &N, sizeof(int), MCL_ARG_INPUT|MCL_ARG_SCALAR)){
			printf("Error setting up task input N. Aborting.\n");
			continue;
		}

		if(mcl_task_set_arg(hdl[i], 3, (void*) C, msize, MCL_ARG_OUTPUT|MCL_ARG_BUFFER)){
			printf("Error setting up task output. Aborting.\n");
			continue;
		}

		if((ret = mcl_exec(hdl[i], pes, NULL, flags))){
		  printf("Error submitting task (%d)! Aborting.\n", ret);
			continue;
		}

		if(synct)
			if(mcl_wait(hdl[i])){
				printf("Request timed out!\n");
			        continue;
			}
	}

	if(!synct)
		if(mcl_wait_all()){
			printf("Error waiting for requests to complete!\n");
			goto err_hdl;
		}
	clock_gettime(CLOCK_MONOTONIC, &end);

	for(i=0; i<rep; i++)
		if(hdl[i]->ret == MCL_RET_ERROR){
			printf("Error executing task %u!\n", i);
			errs++;
		}
	if(errs)
		printf("Detected %u errors!\n",errs);
	else{
		rtime = ((FPTYPE)tdiff(end,start))/BILLION;
		printf("Done.\n  Test time : %f seconds\n", rtime);
		printf("  Throughput: %f tasks/s\n", ((FPTYPE)rep)/rtime);
	}

	for(i=0; i<rep; i++)
		mcl_hdl_free(hdl[i]);
	free(hdl);


    if(test_type == 3){
        printf("Setting argument for transfer out...\n");
        arg_flags |= MCL_ARG_RESIDENT | MCL_ARG_DONE;
        mcl_transfer* t = mcl_transfer_create(2, 1, 0x0);
        if(!t){
            printf("Error creating transfer.\n");
            return 1;
        }
        printf("Successfully created transfer.\n");
        if(mcl_transfer_set_arg(t, 0, (void*) A, msize, 0, arg_flags)){
            printf("Error setting transfer argument.\n");
            return 1;
        }
        printf("Setup 1st argument\n");
        if(mcl_transfer_set_arg(t, 1, (void*) B, msize, 0, arg_flags)){
            printf("Error setting transfer argument.\n");
            return 1;
        }
        printf("Executing transfer out...");
        if(mcl_transfer_exec(t, flags)){
            printf("Error executing transfer.\n");
            return 1;
        }
        if(mcl_transfer_wait(t)){
            printf("Error executing transfer.\n");
            return 1;
        }
        printf("Finished transfer out\n");
        mcl_transfer_free(t);
    }

	return errs;

 err_hdl:
	free(hdl);
 err:
	return -1;
}

int gemm_seq(FPTYPE* A, FPTYPE* B, FPTYPE* C, size_t N)
{
	int i, j, k;

	for (i = 0; i < N; i++){
		for (j = 0; j < N; j++){
			for (k = 0; k < N; ++k){
				C[i*N + j] += A[i*N + k] * B[k*N + j];
			}
		}
	}

	return 0;
}


int main(int argc, char** argv)
{
	FPTYPE          *A, *B, *C, *C_test;
	int             i, j, ret = 0;


	mcl_banner("Resident Data Test");

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

        mcl_init(workers,0x0);

	A = (FPTYPE*) malloc(size * size * sizeof(FPTYPE));
	B = (FPTYPE*) malloc(size * size * sizeof(FPTYPE));
	C = (FPTYPE*) malloc(size * size * sizeof(FPTYPE));
	C_test = (FPTYPE*) malloc(size * size * sizeof(FPTYPE));

	if(!A || !B || !C || !C_test){
		printf("Error allocating vectors. Aborting.");
		goto err;
	}

	srand48(13579862);
	for(i=0; i<size; ++i){
		for(j=0; j<size; ++j){
			A[i*size+j] = (FPTYPE)(0.5 + drand48()*1.5);
            B[i*size+j] = (FPTYPE)(0.5 + drand48()*1.5);
		}
	}
        memset(C, 0, size * size * sizeof(FPTYPE));
        memset(C_test, 0, size * size * sizeof(FPTYPE));

#ifdef DOUBLE_PRECISION
        mcl_prg_load("./gemmN.cl", "-DDOUBLE_PRECISION", MCL_PRG_SRC);
#else
        mcl_prg_load("./gemmN.cl", "-DSINGLE_PRECISION", MCL_PRG_SRC);
#endif

        gemm_seq(A, B, C_test, size);

        // Test 0
	ret = test_mcl(A,B,C,size, 0);
	if(ret){
                printf("Error performing computation (%d). Aborting.\n", ret);
                ret = -1;
                goto out;
        }
	ret = test_results(C, C_test, size);
        if(ret){
                printf("Error verifying computation. Aborting.\n");
                ret = -1;
                goto out;
        }

        // Test 1
	for(i=0; i<size; ++i){
		for(j=0; j<size; ++j){
			A[i*size+j] = (FPTYPE)(0.5 + drand48()*1.5);
            B[i*size+j] = (FPTYPE)(0.5 + drand48()*1.5);
		}
	}
        memset(C, 0, size * size * sizeof(FPTYPE));
        memset(C_test, 0, size * size * sizeof(FPTYPE));

        gemm_seq(A, B, C_test, size);  

	ret = test_mcl(A,B,C,size, 1);
	if(ret){
                printf("Error performing computation (%d). Aborting.\n", ret);
                ret = -1;
                goto out;
        }
	ret = test_results(C, C_test, size);
        if(ret){
                printf("Error verifying computation. Aborting.\n");
                ret = -1;
                goto out;
        }

        // Test 2
	for(i=0; i<size; ++i){
		for(j=0; j<size; ++j){
			A[i*size+j] = (FPTYPE)(0.5 + drand48()*1.5);
            B[i*size+j] = (FPTYPE)(0.5 + drand48()*1.5);
		}
	}
    memset(C, 0, size * size * sizeof(FPTYPE));
    memset(C_test, 0, size * size * sizeof(FPTYPE));

    gemm_seq(A, B, C_test, size);  

	ret = test_mcl(A,B,C,size, 2);
	if(ret){
        printf("Error performing computation (%d). Aborting.\n", ret);
        ret = -1;
        goto out;
    }
	ret = test_results(C, C_test, size);
    if(ret){
        printf("Error verifying computation. Aborting.\n");
        ret = -1;
        goto out;
    }

    // Test 3
	for(i=0; i<size; ++i){
		for(j=0; j<size; ++j){
			A[i*size+j] = (FPTYPE)(0.5 + drand48()*1.5);
            B[i*size+j] = (FPTYPE)(0.5 + drand48()*1.5);
		}
	}
    
    memset(C, 0, size * size * sizeof(FPTYPE));
    memset(C_test, 0, size * size * sizeof(FPTYPE));

    gemm_seq(A, B, C_test, size);  

    ret = test_mcl(A,B,C,size, 3);
	if(ret){
        printf("Error performing computation (%d). Aborting.\n", ret);
        ret = -1;
        goto out;
    }
        ret = test_results(C, C_test, size);
    if(ret){
        printf("Error verifying computation. Aborting.\n");
        ret = -1;
        goto out;
    }

out:
	mcl_finit();
	mcl_verify(ret);

	free(A);
	free(B);
	free(C);
	free(C_test);
 err:
	return ret;
}
