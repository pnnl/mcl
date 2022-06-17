#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <string.h>

#include <minos.h>
#include "utils.h"

/*
 * Use the pointer to store the actual values, rather than
 * the address at which the value is stored.
 */
int fact_seq_native(uint64_t n, uint64_t* out)
{
	uint64_t f = 1;
	uint64_t c;

	for (c = 1; c <= n; c++)
		f = f * c;
	
	*out = f;

	return 0;
}

/*
 * Use the pointer to store the actual values, rather than
 * the address at which the value is stored.
 */
int fact_seq(void* in, size_t isize, void** out, size_t* osize)
{
	uint64_t n = (uint64_t) in;	
	uint64_t f = 1;
	uint64_t c;

	for (c = 1; c <= n; c++)
		f = f * c;
	
	*out = (void*) f;
	*osize = sizeof(uint64_t);

	return 0;
}

int main(int argc, char** argv)
{
	uint64_t            i;
	mcl_handle**        hdls;
	struct timespec     start, end;
	uint64_t            *in, *out, *out_test;
	int                 ret = 0;
	uint64_t            pes[MCL_DEV_DIMS] = {1,1,1};
	unsigned int        errs, submitted;
	char             src_path[1024];

        strcpy(src_path, XSTR(_MCL_TEST_PATH));
        strcat(src_path, "/exec.cl");

	mcl_banner("EXEC Request Test");

	parse_global_opts(argc, argv);

	hdls     = (mcl_handle**) malloc(rep * sizeof(mcl_handle*));
	in       = (uint64_t*) malloc(rep * sizeof(uint64_t));
	out      = (uint64_t*) malloc(rep * sizeof(uint64_t));
	out_test = (uint64_t*) malloc(rep * sizeof(uint64_t));
	
	if(!in || !out || !out_test){
		printf("Error allocating memory. Aborting.\n");
		goto err;
	}
	
	mcl_init(workers,0x0);
	
	//Use the pointer to input to contain data, ensure 0 doesn't come up from rand()
	for(i=0; i<rep; i++)
		in[i] = (rand() % BASE) + 1; 

       	printf("Native Test...");
	clock_gettime(CLOCK_MONOTONIC,&start);
	for(i=0; i<rep; i++){
		fact_seq_native(in[i], &out_test[i]);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);
#ifdef VERBOSE
	for(i=0; i<rep; i++)
		printf("  %"PRIu64" Factorial of %"PRIu64" = %"PRIu64"\n", i, in[i],
		       out_test[i]);
#endif

       	printf("Synchronous Test...");
	clock_gettime(CLOCK_MONOTONIC,&start);
        if(mcl_prg_load(src_path, "", MCL_PRG_SRC)){
                printf("Error loading program.\n");
                goto err;
        }
                
	for(i=0, errs=0, submitted=0; i<rep; i++){		
		hdls[i] = mcl_task_create();
		
		if(!hdls[i]){
			printf("Error creating task %" PRIu64 ".\n",i);
			continue;
		}

		if(mcl_task_set_kernel(hdls[i], "FACT", 2)){
			printf("Error setting task kernel %s for request %"PRIu64".\n","FACT", i);
			continue;
		}

		if(mcl_task_set_arg(hdls[i], 0, (void*) &(in[i]), sizeof(uint64_t),
				    MCL_ARG_INPUT| MCL_ARG_SCALAR)){
			printf("Error setting argument for task %"PRIu64".\n",i);
			continue;
		}		

		if(mcl_task_set_arg(hdls[i], 1, (void*) &(out[i]), sizeof(uint64_t),
				    MCL_ARG_OUTPUT|MCL_ARG_BUFFER)){
			printf("Error setting output for task %"PRIu64".\n",i);
			continue;
		}		

		if(mcl_exec(hdls[i], pes, NULL, MCL_TASK_CPU)){
			printf("Error executing task %"PRIu64".", i);
			continue;
		}
		
		submitted++;

		if(mcl_wait(hdls[i]))
			printf("Request %" PRIu64 " timed out!",i);

	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	
	for(i=0; i<rep; i++)
		if(hdls[i]->status != MCL_REQ_COMPLETED || out_test[i] != out[i]){
			printf("Request %"PRIu64" ref value=%"PRIu64" comp value=%"PRIu64
			       " status=%"PRIx64" retrun=%x"PRIx64"\n",
			       i, out_test[i], out[i], hdls[i]->status, hdls[i]->ret);
			ret = 1;
			errs++;
		}
	
	if(!errs)
		printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);
	else
		printf("Detected %u errors!\n",errs);

	for(i=0; i<rep; i++)
		mcl_hdl_free(hdls[i]);

       	printf("Asynchronous Test...");
	clock_gettime(CLOCK_MONOTONIC,&start);
	for(i=0, errs=0, submitted=0; i<rep; i++){
	       		
		hdls[i] = mcl_task_create();
		if(!hdls[i]){
			printf("Error creating task%" PRIu64 ".",i);
			continue;
		}

		if(mcl_task_set_kernel(hdls[i], "FACT", 2)){
			printf("Error setting task kernel %s for request %"PRIu64".\n", "FACT", i);
			continue;
		}
		
		if(mcl_task_set_arg(hdls[i], 0, (void*) &(in[i]), sizeof(uint64_t),
				    MCL_ARG_INPUT| MCL_ARG_SCALAR)){
			printf("Error setting argument for task %"PRIu64".\n",i);
			continue;
		}		
		
		if(mcl_task_set_arg(hdls[i], 1, (void*) &(out[i]), sizeof(uint64_t),
				    MCL_ARG_OUTPUT| MCL_ARG_BUFFER)){
			printf("Error setting output for task %"PRIu64".\n",i);
			continue;
		}

		if(mcl_exec(hdls[i], pes, NULL, MCL_TASK_CPU)){
			printf("Error executing task %"PRIu64".", i);
			continue;
		}

		submitted++;
	}

	if(submitted == rep)
	  mcl_wait_all();
	else
	  printf("Not all requests have been successfully submitted! (%u/%"PRIu64")\n", submitted, rep);
	
	clock_gettime(CLOCK_MONOTONIC, &end);

	for(i=0; i<rep; i++)
		if(hdls[i]->status != MCL_REQ_COMPLETED || out_test[i] != out[i]){
			printf("Request %"PRIu64" ref value=%"PRIu64" comp value=%"PRIu64
			       " status=%"PRIx64" retrun=%x"PRIx64"\n",
			       i, out_test[i], out[i], hdls[i]->status, hdls[i]->ret);
			ret = 1;
			errs++;
		}
	
	if(!errs)
		printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);
	else
		printf("Detected %u errors!\n",errs);
		
	for(i=0; i<rep; i++)
		mcl_hdl_free(hdls[i]);

	mcl_finit();
	mcl_verify(ret);
	
	free(out);
	free(in);
	free(hdls);
	
	return 0;
	
 err:
	exit(EXIT_FAILURE);
}
