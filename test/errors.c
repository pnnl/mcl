#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <string.h>

#include <minos.h>
#include "utils.h"


//#define VERBOSE

static const struct option lopts[] = {
        {"help",      0, NULL, 'h'},
        {NULL,        0, NULL, 0}
};

void print_help(char* prog)
{
        printf("Usage: %s <options>\n", prog);
        printf("\t -h, --help            Show help\n");

        exit(EXIT_FAILURE);
}

void parse_opts(int argc, char** argv)
{
        int opt;

        do{
                opt = getopt_long(argc, argv, "h", lopts, NULL);

                switch(opt){
                case 'h':
                        print_help(argv[0]);
                        exit(EXIT_FAILURE);
                case -1:
                        break;
                default:
                        printf("Option %c not regognized!. Aborting.\n",opt);
                        print_help(argv[0]);
                        exit(EXIT_FAILURE);
                }
        }while(opt != -1);
}

static inline void failed(void)
{
	printf("%s FAILED %s\n", KRED, KNRM);
}

static inline void detected(void)
{
	printf("%s DETECTED %s\n", KGRN, KNRM);
}

static inline void correct(void)
{
	printf("%s CORRECT %s\n", KGRN, KNRM);
}

int main(int argc, char** argv)
{
	mcl_handle* hdl      = NULL;
	uint64_t    in       = 10;
	uint64_t    out      = 0;
	uint64_t    pes[3]   = {0, 0, 0};
        int ret = 0;
	mcl_banner("ERROR Test");

	parse_opts(argc, argv);

	if(mcl_init(1, 0x0)){
		printf("Error initializing MCL. Aborting.\n");
		goto err;
	}
	
	printf("\n");
	mcl_prg_load("./exec.cl", "", MCL_PRG_SRC);

	printf("%-40s", "Checking NULL handle error...");
	if(mcl_exec(hdl, pes, NULL, 0x0))
		detected();
	else
		failed();
	
	hdl = mcl_task_create();
	if(!hdl){
		printf("Error creating task. Aborting.\n");
		goto err_init;
	}
	
	printf("%-40s", "Checking NULL PE array ...");
	if(mcl_exec(hdl, NULL, NULL, 0) == -MCL_ERR_INVARG)
		detected();	
	else
		failed();

	mcl_hdl_free(hdl);
	
	printf("%-40s", "Checking PE[0] = 0 ...");
	hdl = mcl_task_create();
	if(!hdl){
		printf("Error creating task. Aborting.\n");
		goto err_init;
	}

	if((ret = mcl_exec(hdl, pes, NULL, 0)) == -MCL_ERR_INVPES){
		detected();
	} else {
		failed();
		fprintf(stderr, "Return code: %d\n", ret);
        }

	mcl_hdl_free(hdl);
	
	printf("%-40s", "Checking program source...");
	hdl = mcl_task_create();
	if(!hdl){
		printf("Error creating task. Aborting.\n");
		goto err_init;
	}
	
	pes[0] = pes[1] = pes[2] = 1;
	
	if((ret = mcl_exec(hdl, pes, NULL, MCL_TASK_ANY)) == -MCL_ERR_INVKER) {
		detected();
        } else {
                failed();
                fprintf(stderr, "Return code: %d\n", ret);
        }
	mcl_hdl_free(hdl);
	
	printf("%-40s", "Checking task execution...");
	hdl = mcl_task_create();
	if(!hdl){
		printf("Error creating task. Aborting.\n");
		goto err_init;
	}
	
	if(mcl_task_set_kernel(hdl, "FACT", 2)){
		printf("Error setting up task kernel. Aborting.\n");
		goto err_hdl;
	}
	
	if(mcl_task_set_arg(hdl, 0, (void*) &in, sizeof(uint64_t),
			    MCL_ARG_INPUT| MCL_ARG_SCALAR)){
		printf("Error setting task input. Aborting.\n");
		goto err_hdl;
	}		
	
	if(mcl_task_set_arg(hdl, 1, (void*) &out, sizeof(uint64_t),
			    MCL_ARG_OUTPUT|MCL_ARG_BUFFER)){
		printf("Error setting task output. Aboring.\n");
		goto err_hdl;
	}		
	
	if(mcl_exec(hdl, pes, NULL, MCL_TASK_ANY)){
		printf("Error executing task! Aborting.\n");
		goto err_hdl;
	}
	
	if(mcl_wait(hdl)){
		printf("Request timed out! Aborting.");
		goto err_hdl;
	}
	
	if(hdl->status == MCL_REQ_COMPLETED && hdl->ret == MCL_RET_SUCCESS)
		detected();
	else
		failed();
	
	mcl_hdl_free(hdl);
	
	if(mcl_finit()){
		printf("Error finalizing MCL. Aborting.\n");
		goto err;
	}
	
	printf("\n");

	mcl_verify(0);
	return 0;
	
 err_hdl:
	mcl_hdl_free(hdl);
 err_init:
	mcl_finit();
 err:
	return -1;
}
