#include <stdio.h>
#include <time.h>
#include <config.h>
#include <inttypes.h>

#include "utils.h"

//#define VERBOSE

uint64_t workers = DFT_WORKERS;
size_t   size    = DFT_SIZE;
uint64_t type    = DFT_TYPE;
uint64_t synct   = DFT_SYNC;
uint64_t rep     = DFT_REP;
uint64_t verify  = DFT_VERIFY;
uint64_t did     = DFT_DID;
uint64_t flags   = 0x0;
struct timespec start, end;

const struct option global_lopts[] = {
#ifdef __TEST_MCL
        {"workers",     0, NULL, 'w'},
	{"sync",        0, NULL, 'x'},
#endif
#ifdef __TEST_OCL
        {"device",      0, NULL, 'd'},
#endif
	{"rep",         0, NULL, 'r'},
        {"size",        0, NULL, 's'},
	{"type",        0, NULL, 't'},
	{"verify",      0, NULL, 'v'},
        {"help",        0, NULL, 'h'},
        {NULL,          0, NULL, 0}
};

void print_global_help(char* prog)
{
        printf("Usage: %s <options>\n", prog);
#ifdef __TEST_MCL
        printf("\t -w, --workers   <n> Number of workers (default = %lu)\n", DFT_WORKERS);
	printf("\t -t, --type      <n> Type of resource  (default = %lu, 0=CPU, 1=GPU, 2=ANY)\n",
	    	DFT_TYPE);
	printf("\t -x, --sync      Synchronous test      (default = %lu, 0=ASYNC, 1=SYNC)\n",
	    	DFT_SYNC);
#endif
#ifdef __TEST_OCL
	printf("\t -t, --type      <n> Type of resource  (default = %lu, 0=CPU, 1=GPU)\n",
	    	DFT_TYPE);
	printf("\t -d, --device    <n> Device ID  (default = %lu)\n", DFT_DID);
#endif	
        printf("\t -s, --size      <n> Size of matrix    (default = %lu)\n", DFT_SIZE);
	printf("\t -r, --rep       <n> Repetitions       (default = %lu)\n", DFT_REP);
	printf("\t -v, --verify    Verify execution      (default = %lu)\n", DFT_VERIFY);
	printf("\t -h, --help      Show help\n");

        exit(EXIT_FAILURE);
}

void parse_global_opts(int argc, char** argv)
{
        int opt;
        char* end;

        do{
#ifdef __TEST_MCL
                opt = getopt_long(argc, argv, "w:s:t:r:xvh", global_lopts, NULL);
#endif
#ifdef __TEST_OCL
                opt = getopt_long(argc, argv, "s:t:r:d:vh", global_lopts, NULL);
#endif
				
                switch(opt){
                case 'h':
                        print_global_help(argv[0]);
                        exit(EXIT_FAILURE);
#ifdef __TEST_MCL
                case 'w':
                        workers = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as workers.\n",optarg);
                                exit(EXIT_FAILURE);
                        }
                        break;
		case 'x':
			synct = 1UL;
                        break;
#endif
#ifdef __TEST_OCL
                case 'd':
                        did = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as device ID.\n",optarg);
                                exit(EXIT_FAILURE);
                        }
                        break;
#endif
                case 's':
                        size = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as vector size.\n",optarg);
                                exit(EXIT_FAILURE);
                        }
                        break;
		case 'r':
                        rep = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as number of repetitions.\n",optarg);
                                exit(EXIT_FAILURE);
                        }
                        break;
                case 't':
                        type = strtol(optarg, &end, 10);
                        if(*end){
                                printf("Error converting %s as PE type.\n",optarg);
                                exit(EXIT_FAILURE);
                        }
                        break;
		case 'v':
			verify = 1UL;
			synct  = 1UL;
                        break;
                case -1:
                        break;
                default:
                        printf("Option %c not regognized!. Aborting.\n",opt);
                        print_global_help(argv[0]);
                        exit(EXIT_FAILURE);
                }
        }while(opt != -1);

        printf("Parsed options: \n");
#ifdef __TEST_MCL
        printf("\t Number of workers     = %"PRIu64"\n", workers);
	printf("\t Type of test          = %s\n", synct?"Sync":"Async");
#endif
#ifdef __TEST_OCL
        printf("\t Device ID             = %"PRIu64"\n", did);
#endif
        printf("\t Matrix size           = %ld\n"        , size);
	printf("\t Number of repetitions = %"PRIu64"\n"  , rep);
	printf("\t Type of PEs           = %"PRIu64"\n"  , type);
	printf("\t Verify test           = %s\n"         , synct?"Yes":"No");
}

void mcl_banner(char* name)
{
	time_t t;

	printf("\n\n");
	printf("===========================================\n");
	printf("    %s\n", PACKAGE_NAME);
	printf("    %s\n",name);
	printf("===========================================\n");
	printf("Version:    %s\n", PACKAGE_VERSION);
	t = time(NULL);
	printf("Start time: %s",ctime(&t));
	printf("-------------------------------------------\n");
	clock_gettime(CLOCK_MONOTONIC, &start);
}

void mcl_verify(int res)
{
	time_t t;

	clock_gettime(CLOCK_MONOTONIC, &end);
	t = time(NULL);
	printf("-------------------------------------------\n");
	printf("End time:       %s", ctime(&t));
	printf("Result:         %s\n",res? TEST_FAILED: TEST_SUCCESS);
	printf("Execution time: %f seconds\n", ((float)tdiff(end,start))/BILLION);
	printf("===========================================\n");
}

int mcl_load(char* file, char** src)
{
	int fd;
	size_t n;
	
	fd = open(file, O_RDONLY);
	if(fd == -1){
		printf("\n Error opening OpenCL code. Aborting.\n");
		goto err;
	}
	n = lseek(fd, 0, SEEK_END);

	*src = (char*) malloc(n+1);
	if(!src){
		printf("Error allocating memory to store OpenCL code. Aborting.\n");
		goto err_file;
	}

	lseek(fd, 0, SEEK_SET);
	if(read(fd, (void*) *src, n) != n){
		printf("Error loading OpenCL code. Aborting.\n");
		goto err_src;
	}

	(*src)[n] = '\0';
#ifdef VERBOSE
	printf("Loaded source, size %lu\n ", n);
#endif
	return 0;

 err_src:
	free(*src);
 err_file:
	close(fd);
 err:
	return -1;
}

int ocl_setup(cl_device_id** dev, cl_context* ctxt, cl_command_queue* q, unsigned long type, unsigned long id)
{
	cl_int          ret = 0;
	cl_platform_id* platforms = NULL;
	cl_uint         nplatforms, ndevices, i;
	
	ret = clGetPlatformIDs(0, NULL, &nplatforms);
	if(ret != CL_SUCCESS){
		printf("Error querying CL platforms! Aborting. (%d)\n", ret);
		goto err;
	}
#ifdef VERBOSE
	printf("Found %d platforms.\n", nplatforms);
#endif
	platforms = (cl_platform_id*) malloc(sizeof(cl_platform_id) * nplatforms);
	if(!platforms){
		printf("Error allocating mameory for CL platforms. Aborting.");
		goto err;
	}
	
	ret = clGetPlatformIDs(nplatforms, platforms, NULL);
	if(ret != CL_SUCCESS){
	    printf("Error querying CL platforms! Aborting. (%d)\n", ret);
	    goto err_plt;
        }

	ret = CL_DEVICE_NOT_FOUND;
	for(i=0; i<nplatforms; i++){
	     ret = clGetDeviceIDs(platforms[i], type, 0, NULL, &ndevices);
	     if(ret == CL_SUCCESS && ndevices){
		     *dev = (cl_device_id*) malloc(sizeof(cl_device_id) *  ndevices);
		     if(*dev == NULL){
			     printf("Error allocating mameory for CL devices. Aborting.");
			     goto err_plt;
		     }
		     ret = clGetDeviceIDs(platforms[i], type, ndevices, *dev, NULL);
		     break;
	     }	     
        }

	if(ret != CL_SUCCESS){
		printf("Error querying CL devices for type 0x%lx. (%d)\n", type, ret);
		goto err_plt;
	}
	
#ifdef VERBOSE
	printf("Found %d compatible devices.\n", ndevices);
#endif
	
	*ctxt = clCreateContext( NULL, ndevices, *dev, NULL, NULL, &ret);
	if(ret != CL_SUCCESS){
		printf("Error creating context! Aborting. (%d)\n", ret);
		goto err_plt;
	}
	
#ifdef OPENCL2
    cl_command_queue_properties props[3] = {CL_QUEUE_PROPERTIES, 0, 0};
	*q = clCreateCommandQueueWithProperties(*ctxt, *(*dev + id), props, &ret);
#else
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    *q = clCreateCommandQueue(*ctxt, *(*dev + id), NULL, &ret);
#pragma GCC diagnostic pop
#endif
    if(ret != CL_SUCCESS){
		printf("Error creating command queue! Aborting. (%d)\n", ret);
		goto err_plt;
	}

 err_plt:
	free(platforms);
 err:
	return ret;
}
