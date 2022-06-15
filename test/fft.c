#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>

#include <minos.h>
#include "utils.h"

#define FORWARD_FFT_NAME    "fftRadix2Kernel"

#define ADD(cmp1, cmp2) (CMPTYPE){cmp1.x + cmp2.x, cmp1.y + cmp2.y}
#define MUL(cmp1, cmp2) (CMPTYPE){(cmp1.x*cmp2.x) - (cmp1.y * cmp2.y), (cmp1.x * cmp2.y) + (cmp1.y * cmp2.x)}

static inline CMPTYPE cmp_polar(FPTYPE r, FPTYPE theta){
    return (CMPTYPE){(FPTYPE)(r * cos((double)theta)), (FPTYPE)(r * sin((double)theta))};
}

static CMPTYPE cmp_pow(CMPTYPE c, FPTYPE e){
    FPTYPE r = sqrt((c.x * c.x) + (c.y * c.y));
    FPTYPE theta = tan(c.y / c.x);
    return cmp_polar(pow(r, e), theta * e);
}


int int_log2(int N)    /*function to calculate the log2(.) of int numbers*/
{
  int k = N, i = 0;
  while(k) {
    k >>= 1;
    i++;
  }
  return i - 1;
}

int reverse(int N, int n)    //calculating revers number
{
  int j, p = 0;
  for(j = 1; j <= int_log2(N); j++) {
    if(n & (1 << (int_log2(N) - j)))
      p |= 1 << (j - 1);
  }
  return p;
}

void ordina(CMPTYPE* f1, int N) //using the reverse order in the array
{
    CMPTYPE* f2 = malloc(sizeof(CMPTYPE) * N);
    for(int i = 0; i < N; i++)
        f2[i] = f1[reverse(N, i)];
    for(int j = 0; j < N; j++)
        f1[j] = f2[j];
    free(f2);
}

void fft_reference(CMPTYPE* f, size_t N)
{
  ordina(f, N);
  CMPTYPE *W;
  W = (CMPTYPE*)malloc(N / 2 * sizeof(CMPTYPE));
  W[1] = cmp_polar(1., -2. * M_PI / N);
  W[0] = (CMPTYPE){1, 0};
  for(int i = 2; i < N / 2; i++)
    W[i] = cmp_pow(W[1], i);
  int n = 1;
  int a = N / 2;
  for(int j = 0; j < int_log2(N); j++) {
    for(int i = 0; i < N; i++) {
      if(!(i & n)) {
        CMPTYPE temp = f[i];
        CMPTYPE Temp = MUL(W[(i * a) % (n * a)],f[i + n]);
        f[i] = ADD(temp,Temp);
        Temp = (CMPTYPE) {-Temp.x , -Temp.y};
        f[i + n] = ADD(temp, Temp);
      }
    }
    n *= 2;
    a = a / 2;
  }
  free(W);
}


int test_ocl(CMPTYPE** sources, CMPTYPE** results)
{
	cl_device_id*    device;
	struct timespec  start, end;
	char*            src_code = NULL;
	size_t           src_size = 0;
	cl_int           ret = 0;
	cl_context       context;
	cl_command_queue queue;
	cl_program       program = NULL;
	cl_kernel        ffft_kernel = NULL;
	cl_uint          numDimensions = 0;
	size_t*          maxWorkSizes;
	size_t           globalWorkSize[] = {size/2, 1, 1};
	float            rtime;
	int              i;
    uint64_t         bytes = size * sizeof(CMPTYPE);
    uint64_t         niters = (uint64_t)floor(log2((double)size));

#ifdef DOUBLE_PRECISION
    char* copts = "-DK_DOUBLE_PRECISION";
#else
    char* copts = "-DSINGLE_PRECISION";
#endif

	if(mcl_load("./fft.cl", &src_code)){
		printf("Error loading OpenCL kernel! Aborting.\n");
		goto err;
	}

    if(ocl_setup(&device, &context, &queue, flags, did))
		goto err_src;

	ret = clGetDeviceInfo (device[did], CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS,
			 sizeof(cl_uint), &numDimensions, NULL);
	if(ret != CL_SUCCESS){
		printf("Error quering device %"PRIu64" info. Aborting!\n",did);
		goto err_src;		
	}
	maxWorkSizes = (size_t*) malloc(numDimensions * sizeof(size_t));
	if(!maxWorkSizes){
		printf("Error allocating work sizes. Aborting!\n");
		goto err_src;		
	}
    clGetDeviceInfo (device[did], CL_DEVICE_MAX_WORK_ITEM_SIZES,
                       sizeof(size_t) * numDimensions, maxWorkSizes, NULL);

    src_size = strlen(src_code);
	program = clCreateProgramWithSource(context, 1, (const char **)&src_code,
					    (const size_t *)&src_size, &ret);
	if(ret != CL_SUCCESS){
		printf("Error creating program! Aborting. (%d)\n", ret);
		goto err_setup;
	}

    ret = clBuildProgram(program, 1, &(device[did]), copts, NULL, NULL);
    ffft_kernel = clCreateKernel(program, FORWARD_FFT_NAME, &ret);
    if(ret != CL_SUCCESS){
        printf("Error creating kernel! Aborting. (%d)\n", ret);
        goto err_program;
    }

    cl_ulong mem_avail;
    clGetDeviceInfo(device[did], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(cl_ulong), &mem_avail, NULL);
    mem_avail = mem_avail * 3 / 4; 
    size_t num_vecs = mem_avail/(2  * bytes);
    size_t concurrent_fft = rep < num_vecs ? rep : num_vecs;
    printf("Concurrent FFTs %"PRIu64"\n", rep);
    size_t devs = rep/concurrent_fft;
    printf("Number of devices to run all ffts concurrently %zu\n", devs);

    cl_mem* d_sources = malloc(sizeof(cl_mem) * concurrent_fft);
    cl_mem* d_results = malloc(sizeof(cl_mem) * concurrent_fft);
    cl_event* ievents = malloc(sizeof(cl_event) * concurrent_fft);
    cl_event* kevents = malloc(sizeof(cl_event) * concurrent_fft * niters);

    printf("OpenCL Test...");
    clock_gettime(CLOCK_MONOTONIC,&start);
    for(i = 0; i < concurrent_fft; i++){
        d_sources[i] = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &ret);
        if(ret != CL_SUCCESS){
            printf("Error creating memory object for input buffers! Aborting. (%d)\n", ret);
            goto err_setup;
        }
        d_results[i] = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &ret);
        if(ret != CL_SUCCESS){
            printf("Error creating memory object for output buffers! Aborting. (%d)\n", ret);
            goto err_setup;
        }
    }

    for(int j=0; j<rep; j += concurrent_fft){
        for(i=0; i<concurrent_fft; i++){
            ret = clEnqueueWriteBuffer(queue, d_sources[i], CL_FALSE, 0, bytes, sources[j+i], 0,
                        NULL, ievents+i);
            if(ret != CL_SUCCESS){
                printf("Error copying data to CL device buffer! Aborting. (%d)\n", ret);
                goto err_setup;
            }
            clFlush(queue);
    
            for(uint32_t k = 0; k < niters; k++){
                if(k % 2 == 0){
                    ret  = clSetKernelArg(ffft_kernel, 0, sizeof(cl_mem), (void*) &d_sources[i]);
                    if(ret != CL_SUCCESS){
                        printf("Error setting kernel parameters! Aborting. (%d)\n", ret);
                        goto err_kernel;
                    }

                    ret  = clSetKernelArg(ffft_kernel, 1, sizeof(cl_mem), (void*) &d_results[i]);
                    if(ret != CL_SUCCESS){
                        printf("Error setting kernel parameters! Aborting. (%d)\n", ret);
                        goto err_kernel;
                    }
                } else {
                    ret  = clSetKernelArg(ffft_kernel, 0, sizeof(cl_mem), (void*) &d_results[i]);
                    if(ret != CL_SUCCESS){
                        printf("Error setting kernel parameters! Aborting. (%d)\n", ret);
                        goto err_kernel;
                    }

                    ret  = clSetKernelArg(ffft_kernel, 1, sizeof(cl_mem), (void*) &d_sources[i]);
                    if(ret != CL_SUCCESS){
                        printf("Error setting kernel parameters! Aborting. (%d)\n", ret);
                        goto err_kernel;
                    }
                }
                
                uint32_t p = (1 << k);
                ret  = clSetKernelArg(ffft_kernel, 2, sizeof(uint32_t), (void*) &p);
                if(ret != CL_SUCCESS){
                    printf("Error setting kernel parameters! Aborting. (%d)\n", ret);
                    goto err_kernel;
                }
                
                if(k == 0){
                    ret = clEnqueueNDRangeKernel(queue, ffft_kernel, 3, NULL, globalWorkSize, NULL,
                                1, ievents+i, kevents + (i * concurrent_fft));
                } else {
                    ret = clEnqueueNDRangeKernel(queue, ffft_kernel, 3, NULL, globalWorkSize, NULL,
                                1, kevents + (i * concurrent_fft) + k - 1, kevents + (i * concurrent_fft) + k);
                }
                
                if(ret != CL_SUCCESS){
                    printf("Error enqueuing kernel! Aborting. (%d)\n", ret);
                    goto err_kernel;
                }
            }
            ret = clEnqueueReadBuffer(queue, d_results[i], CL_FALSE, 0, bytes, results[j+i], 1,
                        kevents + (i * concurrent_fft) + niters - 1, NULL);

            if(ret != CL_SUCCESS){
                printf("Error copying data back from device! (%d)\n", ret);
                goto err_kernel;
            }
            
            
            clFlush(queue);
        }
        clFinish(queue);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

    for(i=0; i<concurrent_fft; i++){
        clReleaseMemObject(d_sources[i]);
        clReleaseMemObject(d_results[i]);
    }

    free(d_sources);
    free(d_results);
    free(ievents);
    free(kevents);

	rtime = ((float)tdiff(end,start))/BILLION;
	printf("Done.\n  Test time : %f seconds\n", rtime);
	printf("  Throughput: %f tasks/s\n", ((float)rep)/rtime);

    clReleaseKernel(ffft_kernel);
	clReleaseProgram(program);	
	clReleaseCommandQueue(queue);
	clReleaseContext(context);

	free(src_code);
	
	return 0;

 err_kernel:
	clReleaseKernel(ffft_kernel);
 err_program:
	clReleaseProgram(program);
 err_setup:
	clReleaseCommandQueue(queue);
	clReleaseContext(context);
 err_src:
	free(src_code);
 err:
	return -1;
}

int test_mcl(CMPTYPE** sources, CMPTYPE** results)
{
    struct timespec start, end;
    mcl_handle**    hdl = NULL;
    uint64_t*       completed = NULL;
    uint64_t        pes[MCL_DEV_DIMS] = {size/2, 1, 1};
    uint64_t        niters = (uint64_t) floor(log2((double)size));
    unsigned int    i;
    unsigned int    errs = 0;
    double          rtime;
    int32_t         p = 1;
    unsigned long   arg_flags = MCL_ARG_INPUT|MCL_ARG_BUFFER|MCL_ARG_RESIDENT|MCL_ARG_DYNAMIC;
    uint64_t        bytes = size * sizeof(CMPTYPE);

#ifdef DOUBLE_PRECISION
    char* copts = "-DK_DOUBLE_PRECISION";
#else
    char* copts = "-DSINGLE_PRECISION";
#endif

    hdl = (mcl_handle**) malloc(sizeof(mcl_handle*) * rep);
    if(!hdl){
        printf("Error allocating memmory. Aborting.\n");
        goto err;
    }

    completed = (uint64_t*) malloc(sizeof(uint64_t) * rep);
    memset(completed, 0, sizeof(uint64_t) * rep);
    if(!completed){
        printf("Error allocating memmory. Aborting.\n");
        goto err;
    }

        mcl_prg_load("./fft.cl", copts, MCL_PRG_SRC);

    clock_gettime(CLOCK_MONOTONIC,&start);
    
    for(i=0; i<rep; i++){

        hdl[i] = mcl_task_create();
        if(!hdl[i]) {
            printf("Error creating fft handle. Aborting.\n");
            goto err;
        }
        
        if(mcl_task_set_kernel(hdl[i], FORWARD_FFT_NAME, 3)){
            printf("Error setting fft kernel. Aborting.\n");
            goto err;
        }

        if(mcl_task_set_arg(hdl[i], 0, (void*) sources[i], bytes, arg_flags)){
            printf("Error setting argument 1 for fft. Aborting.\n");
            goto err;
        }
        if(mcl_task_set_arg(hdl[i], 1, (void*) results[i], bytes, arg_flags)){
            printf("Error setting argument 2 for fft. Aborting.\n");
            goto err;
        }
        if(mcl_task_set_arg(hdl[i], 2, (void*) &p, sizeof(int), MCL_ARG_SCALAR)){
            printf("Error setting argument 3 for fft. Aborting.\n");
            goto err;
        }
        if(mcl_exec(hdl[i], pes, NULL, flags)){
            printf("Error executing fft kernel. Aborting.\n");
            goto err;
        }
        if(synct){
            printf("Waiting for request to complete.\n");
            if(mcl_wait(hdl[i])){
                printf("Request timed out!\n");
                    continue;
            }
        }
    }

    int ncompleted = 0;
    i = 0;
    while(ncompleted < rep){
        if(completed[i] < niters && mcl_test(hdl[i]) == MCL_REQ_COMPLETED){
            if(hdl[i]->ret == MCL_RET_ERROR){
                printf("Error executing task %u!\n", i);
                errs++;
            }

            mcl_hdl_free(hdl[i]);
            completed[i] += 1;
            if(completed[i] == niters){
                ncompleted += 1;
                continue;
            }

            hdl[i] = mcl_task_create();
            if(!hdl[i]) {
                printf("Error creating handle for ifft. Aborting.\n");
                goto err;
            }
            if(mcl_task_set_kernel(hdl[i], FORWARD_FFT_NAME, 3)){
                printf("Error setting kernel for ifft. Aborting.\n");
                goto err;
            }

            uint64_t arg_flags_temp_input = arg_flags;
            uint64_t arg_flags_temp_output = arg_flags;
            if(completed[i] == niters - 1) {
                arg_flags_temp_input |= MCL_ARG_DONE;
                arg_flags_temp_output |= MCL_ARG_OUTPUT | MCL_ARG_DONE;
            }

            if(completed[i] % 2 == 0){
                if(mcl_task_set_arg(hdl[i], 0, (void*) sources[i], bytes, arg_flags_temp_input)){
                    printf("Error setting argument 1 for fft. Aborting.\n");
                    goto err;
                }
                if(mcl_task_set_arg(hdl[i], 1, (void*) results[i], bytes, arg_flags_temp_output)){
                    printf("Error setting argument 2 for fft. Aborting.\n");
                    goto err;
                }
            } else {
                if(mcl_task_set_arg(hdl[i], 0, (void*) results[i], bytes, arg_flags_temp_input)){
                    printf("Error setting argument 1 for fft. Aborting.\n");
                    goto err;
                }
                if(mcl_task_set_arg(hdl[i], 1, (void*) sources[i], bytes, arg_flags_temp_output)){
                    printf("Error setting argument 2 for fft. Aborting.\n");
                    goto err;
                }
            }
            p = 1 << completed[i];

            if(mcl_task_set_arg(hdl[i], 2, (void*) &p, sizeof(int), MCL_ARG_SCALAR)){
                printf("Error setting argument 3 for fft. Aborting.\n");
                goto err;
            }
            if(mcl_exec(hdl[i], pes, NULL, flags)){
                printf("Error executing ifft kernel. Aborting.");
                goto err;
            };
            if(synct){
                printf("Waiting for request to complete.\n");
                if(mcl_wait(hdl[i])){
                    printf("Request timed out!\n");
                        continue;
                }
            }
            
        }
        i = (i+1)%rep;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

	if(errs)
		printf("Detected %u errors!\n",errs);
	else{
		rtime = ((FPTYPE)tdiff(end,start))/BILLION;
		printf("Done.\n  Test time : %f seconds\n", rtime);
		printf("  Throughput: %f tasks/s\n", ((FPTYPE)rep)/rtime);
	}

    if(niters % 2 == 0){
        CMPTYPE** temp = (CMPTYPE**)malloc(sizeof(CMPTYPE*) * rep);
        memcpy(temp, results, sizeof(CMPTYPE*) * rep);
        memcpy(results, sources, sizeof(CMPTYPE*) * rep);
        memcpy(sources, temp, sizeof(CMPTYPE*) * rep);
        free(temp);
    }
    free(hdl);
    free(completed);
    return 0;

err:
    free(hdl);
    free(completed);
    return 1;
}

int test_results(CMPTYPE* result, CMPTYPE* reference){
    
    int ret = 0;
    for(size_t i = 0; i < size; i++){
        if(result[i].x - reference[i].x > .5 || result[i].y - reference[i].y > .5){
            printf("At position %lu, result: %f + %fi, reference: %f + %fi\n", i, result[i].x, result[i].y, reference[i].x, reference[i].y);
            ret = -1;
        }
    }
    return ret;
}

int main(int argc, char** argv)
{
    CMPTYPE         **sources, **results, *reference=NULL;
    int             i, ret = 0;

    parse_global_opts(argc, argv);

    switch(type){
#ifdef __TEST_MCL
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
#elif __TEST_OCL
        case 0:{
                flags = CL_DEVICE_TYPE_CPU;
                break;
        }
        case 1:{
                flags = CL_DEVICE_TYPE_GPU;
                break;
        }
#endif
        default:{
            printf("Unrecognized resource type (%"PRIu64"). Aborting.\n",type);
            return -1;
        }
    }

    if((size == 0) || ((size & (size - 1)) != 0)){
        printf("Invalid option. MCL FFT test only works on a power of 2.\n");
        return -1;
    }

    
#ifdef __TEST_MCL
    mcl_init(workers,0x0);
#endif
    
    uint64_t bytes = size * sizeof(CMPTYPE);

    sources = (CMPTYPE**) malloc(rep * sizeof(CMPTYPE*));
    results = (CMPTYPE**) malloc(rep * sizeof(CMPTYPE*));
    if(!sources || !results){
        printf("Error allocating vectors. Aborting.");
        goto err;
    }
    for(i = 0; i < rep; i++){
        sources[i] = (CMPTYPE*)malloc(bytes);
        results[i] = (CMPTYPE*)malloc(bytes);
    }

    srand48(13579862);
    for (i = 0; i < size / 2; i++) {
        sources[0][i].x = (rand()/(float)RAND_MAX)*2-1;
        sources[0][i].y = (rand()/(float)RAND_MAX)*2-1;
        sources[0][i+size/2].x = sources[0][i].x;
        sources[0][i+size/2].y = sources[0][i].y;
    }
    for(i = 1; i < rep; i++) {
        memcpy(sources[i], sources[0], bytes);
    }

    if(verify){
        printf("Calculating reference result. Depending on the size, this might take a while.\n");
        reference = (CMPTYPE*) malloc(bytes);
        memcpy(reference, sources[0], bytes);
        fft_reference(reference, size);
    }

    mcl_banner("FFT Locality Test");
#ifdef __TEST_MCL
    ret = test_mcl(sources, results);
#endif
#ifdef __TEST_OCL
    ret = test_ocl(sources, results);
#endif
    if(ret){
        printf("Error performing computation (%d). Aborting.\n", ret);
        ret = -1;
        goto out;
    }

    if(verify){
        // For conveience and times sake, we only verify one result
        ret = test_results(results[0], reference);
        if(ret){
            ret = -1;
            goto out;
        }
    }

out:
#ifdef __TEST_MCL
    mcl_finit();
    mcl_verify(ret);
#endif

    for(i = 0; i < rep; i++){
        free(sources[i]);
        free(results[i]);
    }

    free(sources);
    free(results);
err:
    return ret;
}
