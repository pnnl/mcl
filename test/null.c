#include <unistd.h>
#include <getopt.h>
#include <time.h>

#include <minos.h>
#include "utils.h"


int main(int argc, char** argv)
{
	uint64_t         i;
	mcl_handle**     hdls;
	struct timespec  start, end;
	int              ret = 0;
	
	mcl_banner("NULL Request Test");

	parse_global_opts(argc, argv);

	hdls = (mcl_handle**) malloc(rep * sizeof(mcl_handle*));
	if(!hdls){
		printf("Error allocating memory for MCL handles. Aborting.");
		goto err;
	}
	mcl_init(workers,0x0);

	printf("Synchronous Test...");
	clock_gettime(CLOCK_MONOTONIC,&start);
	for(i=0; i<rep; i++){
		hdls[i] = mcl_task_create();

		if(!hdls[i]){
			printf("Error creating task %" PRIu64 ".\n",i);
			continue;
		}
		
		if(mcl_null(hdls[i])){
			printf("Error submitting request %" PRIu64 ".\n", i);
			continue;
		}
		
		if(mcl_wait(hdls[i]))
			printf("Request %" PRIu64 " timed out!",i);
	}
		
	clock_gettime(CLOCK_MONOTONIC,&end);
	printf("Done. Execution time: %f seconds\n",((float)tdiff(end,start))/BILLION);

	for(i=0; i<rep; i++)
		if(hdls[i]->status != MCL_REQ_COMPLETED){
			printf("Request %"PRIu64" status=%"PRIx64"\n.",
			       i, hdls[i]->status);
			ret = 1;
		}
	
	for(i=0; i<rep; i++)
		mcl_hdl_free(hdls[i]);

	printf("Asynchronous Test...");
	clock_gettime(CLOCK_MONOTONIC,&start);
	for(i=0; i<rep; i++){
		hdls[i] = mcl_task_create();
		if(!hdls[i]){
			printf("Error creating task %" PRIu64 ".",i);
			continue;
		}

		if(mcl_null(hdls[i])){
			printf("Error submitting request %" PRIu64 ".\n", i);
			continue;
		}		
	}
	mcl_wait_all();
	clock_gettime(CLOCK_MONOTONIC,&end);
	printf("Done. Execution time: %f seconds\n",((float)tdiff(end,start))/BILLION);

	for(i=0; i<rep; i++)
		if(hdls[i]->status != MCL_REQ_COMPLETED){
			printf("Request %"PRIu64" status=%"PRIx64"\n.",
			       i, hdls[i]->status);
			ret = 1;
		}

	for(i=0; i<rep; i++)
		mcl_hdl_free(hdls[i]);
	
	mcl_finit();
	mcl_verify(ret);

	free(hdls);
	
	return 0;
 err:
	exit(EXIT_FAILURE);
}
