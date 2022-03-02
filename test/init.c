#include <unistd.h>
#include <getopt.h>

#include <minos.h>
#include "utils.h"

#define DFT_WTIME  1UL

uint64_t wtime = DFT_WTIME;

static const struct option lopts[] = {
        {"workers",   0, NULL, 't'},
        {"help",      0, NULL, 'h'},
        {NULL,        0, NULL, 0}
};

void print_help(char* prog)
{
        printf("Usage: %s <options>\n", prog);
        printf("\t -t, --time <n>     Waiting time (default = %lu)\n", DFT_WTIME);
        printf("\t -h, --help         Show help\n");

        exit(EXIT_FAILURE);
}

void parse_opts(int argc, char** argv)
{
        int opt;
        char* end;

        do{
                opt = getopt_long(argc, argv, "t:h", lopts, NULL);

                switch(opt){
                case 'h':
                        print_help(argv[0]);
                        exit(EXIT_FAILURE);
                case 't':
                        wtime = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as waiting time.\n",optarg);
                                exit(EXIT_FAILURE);
                        }
                        break;
                case -1:
                        break;
                default:
                        printf("Option %c not regognized!. Aborting.\n",opt);
                        print_help(argv[0]);
                        exit(EXIT_FAILURE);
                }
        }while(opt != -1);

        printf("Parsed options: \n");
        printf("\t Waiting time = %" PRIu64 " secs\n",wtime);
}

int main(int argc, char** argv)
{
       	mcl_banner("Init Test");
	parse_opts(argc, argv);
	
	if(mcl_init(1,MCL_NULL)){
		printf("Error initializing Minos Computing Library. Aborting.\n");
		return -1;
	}

	printf("Waiting....");
	sleep(wtime);
	printf("Done!\n");
	
	mcl_finit();
	mcl_verify(0);

	return 0;
}
