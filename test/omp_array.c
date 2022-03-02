#include <unistd.h>
#include <getopt.h>
#include <time.h>

#include <minos.h>
#include "utils.h"

#define DFT_WORKERS  1UL
#define DFT_N        10UL
#define DFT_THREADS  1UL

#define BASE         10
//#define VERBOSE

uint64_t workers  = DFT_WORKERS;
uint64_t size     = DFT_N;
uint64_t nthreads = DFT_THREADS;

static const struct option lopts[] = {
        {"workers",   0, NULL, 'w'},
        {"size",      0, NULL, 's'},
	{"threads",   0, NULL, 'n'},
        {"help",      0, NULL, 'h'},
        {NULL,        0, NULL, 0}
};

void print_help(char* prog)
{
        printf("Usage: %s <options>\n", prog);
        printf("\t -w, --workers  <n>  Number of workers        (default = %lu)\n", DFT_WORKERS);
        printf("\t -s, --size     <n>  Size of array            (default = %lu)\n", DFT_N);
        printf("\t -n, --threads  <n>  Number of OpemMP threads (default = %lu)\n", DFT_THREADS);
        printf("\t -h, --help          Show help\n");

        exit(EXIT_FAILURE);
}

void parse_opts(int argc, char** argv)
{
        int opt;
        char* end;

        do{
                opt = getopt_long(argc, argv, "w:s:n:h", lopts, NULL);

                switch(opt){
                case 'h':
                        print_help(argv[0]);
                        exit(EXIT_FAILURE);
                case 'w':
                        workers = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as workers.\n",optarg);
                                exit(EXIT_FAILURE);
                        }
                        break;
                case 's':
                        size = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as size.\n",optarg);
                                exit(EXIT_FAILURE);
                        }
                        break;
                case 'n':
                        nthreads = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as OpenMP threads.\n",optarg);
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
        printf("\t Number of workers   = %" PRIu64 "\n",workers);
        printf("\t Array size          = %" PRIu64 "\n",size);
        printf("\t OpenMP threads      = %" PRIu64 "\n",nthreads);
}

int array_add(void* in, size_t ilen, void** out, size_t* olen)
{
	uint64_t  i;
	uint64_t* lin;
	double*   lout;
	uint64_t  n = ilen / sizeof(uint64_t);
		
	lout = malloc(ilen * sizeof(uint64_t));
	if(lout == NULL)
		goto err;

	lin  = (uint64_t*) in;
	
#pragma omp parallel for num_threads(nthreads)
	for(i=0; i<n; i++)
		lout[i] = lin[i]/2.0 + i;

	*out  = (void*) lout;
	*olen = ilen;
	
	return 0;

 err:
	*out  = NULL;
	*olen = 0;

	return -1;
}

int main(int argc, char** argv)
{
	uint64_t            i;
	struct mcl_handle*  hdl;
	struct timespec     start, end;
	mcl_task            task;
	uint64_t*           in;
	double*             out;
	int                 ret = 0;
	
	mcl_banner("EXEC OpenMP Vector Test");

	parse_opts(argc, argv);

	in = (uint64_t*) malloc(size * sizeof(uint64_t));
		
	if(!in){
		printf("Error allocating memory. Aborting.\n");
		goto err;
	}
	
	mcl_init(workers,0x0);

	//Use the pointer to input to contain data, ensure 0 doesn't come up from rand()
	for(i=0; i<size; i++)
		in[i] = (rand() % BASE) + 1; 

	task.func    = array_add;
	task.in_len  = size * sizeof(uint64_t);
	task.in      = (void*) in;
	task.out     = NULL;
	task.flags   = MCL_TASK_CPU;
	task.pes     = nthreads;
	
	clock_gettime(CLOCK_MONOTONIC,&start);
	hdl = mcl_exec(&task, 0x0);
	
	if(!hdl)
		printf("Error submitting requests %" PRIu64 ".",i);
	
	if(mcl_wait(hdl))
		printf("Request %" PRIu64 " timed out!",i);
	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);

	out = (double*) task.out;
	
#ifdef VERBOSE
	for(i=0; i<size; i++)
		printf("In[%"PRIu64"] = %"PRIu64" Out[%"PRIu64"] = %f\n", i, in[i], i, out[i]);
#endif	

	for(i=0; i<size; i++)
		if(out[i] != (in[i]/2.0 + i)){
			printf("In[%"PRIu64"] = %"PRIu64" != Out[%"PRIu64"] = %f\n",
			       i, in[i], i, out[i]);
			ret = -1;
		}

	mcl_hdl_free(hdl);
	mcl_finit();
	mcl_verify(ret);
	
	free(in);
	
	return 0;
 err:
	exit(EXIT_FAILURE);
}
