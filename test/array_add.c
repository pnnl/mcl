#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <string.h>

#include <minos.h>
#include "utils.h"
#define VERBOSE
#define OCL_LOCAL_SIZE 16

#define INPUT_VALUE  1
#define OUTPUT_VALUE 2

#define VERBOSE
int verify_result(unsigned int* ref, size_t n)
{
	int ret = 0;
	int i;
	
	for(i=0; i < n; i++){
		if(ref[i] != OUTPUT_VALUE){
			ret = 1;
			break;
		}
	}

	return ret;
}
int seq_add(unsigned int* a, unsigned int* b, size_t n, unsigned int* out)
{
	unsigned int i;
	
	for(i=0; i<n; i++)
		out[i] = a[i] + b[i];
	
	return 0;
}

#ifdef __TEST_OCL
int ocl_add(unsigned int* a, unsigned int* b, size_t n, unsigned int* out)
{
	char*            src_code = NULL;
	size_t           src_size = 0;
	int              ret = 0;
	cl_device_id*    device;
	cl_context       context;
	cl_command_queue queue;
	cl_mem           a_o, b_o, out_o;
	cl_program       program;
	cl_kernel        kernel;

	size_t           global_item_size = size;
	size_t           local_item_size  = OCL_LOCAL_SIZE;
	size_t           kgroup;
	struct timespec  start, end;

	if(mcl_load("./vadd.cl", &src_code)){
		printf("Error loading OpenCL kernel! Aborting.\n");
		goto err;
	}

#ifdef VERBOSE
	printf("\n\n %s \n\n", src_code);
#endif

	if(ocl_setup(&device, &context, &queue, flags, 0))
		goto err_src;

	clock_gettime(CLOCK_MONOTONIC,&start);

	a_o = clCreateBuffer(context, CL_MEM_READ_ONLY, size * sizeof(unsigned int), NULL, &ret);
	if(ret != CL_SUCCESS){
		printf("Error creating memory object for vector a! Aborting (%d)\n", ret);
		goto err_src;
	}

	b_o = clCreateBuffer(context, CL_MEM_READ_ONLY, size * sizeof(unsigned int), NULL, &ret);
	if(ret != CL_SUCCESS){
		printf("Error creating memory object for vector a! Aborting (%d)\n", ret);
		goto err_a;
	}

	out_o = clCreateBuffer(context, CL_MEM_WRITE_ONLY, size * sizeof(unsigned int),
			       NULL, &ret);
	if(ret != CL_SUCCESS){
		printf("Error creating memory object for vector a! Aborting (%d)\n", ret);
		goto err_b;
	}

	ret = clEnqueueWriteBuffer(queue, a_o, CL_TRUE, 0, size * sizeof(unsigned int), (void*)a,
				   0, NULL, NULL);
	if(ret != CL_SUCCESS){
		printf("Error copying data to CL device buffer for vector a! Aborting. (%d)\n",
		       ret);
		goto err_out;
	}

	ret = clEnqueueWriteBuffer(queue, b_o, CL_TRUE, 0, size * sizeof(unsigned int), (void*)b,
				   0, NULL, NULL);
	if(ret != CL_SUCCESS){
		printf("Error copying data to CL device buffer for vector b! Aborting. (%d)\n",
		       ret);
		goto err_out;
	}
	src_size = strlen(src_code);

	program = clCreateProgramWithSource(context, 1, (const char **)&src_code,
					    (const size_t *)&src_size, &ret);
	if(ret != CL_SUCCESS){
		printf("Error creating program! Aborting. (%d)\n", ret);
		goto err_out;
	}
	
	ret = clBuildProgram(program, 1, &(device[0]), NULL, NULL, NULL);
	if(ret != CL_SUCCESS){
		printf("Error building program! Aborting. (%d)\n", ret);
		goto err_program;
	}
	
	kernel = clCreateKernel(program, "VADD", &ret);
	if(ret != CL_SUCCESS){
		printf("Error creating kernel! Aborting. (%d)\n", ret);
		goto err_program;
	}

	ret = clGetKernelWorkGroupInfo(kernel, device[0], CL_KERNEL_WORK_GROUP_SIZE,
				       sizeof(size_t), &kgroup, NULL);
	if(ret != CL_SUCCESS){
		printf("Error querying the kernel work group size! Aborting. (%d)\n", ret);
		goto err_program;
	}
			
	ret  = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&a_o);
	ret |= clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *)&b_o);
	ret |= clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *)&out_o);
	if(ret != CL_SUCCESS){
		printf("Error setting kernel parameters! Aborting. (%d)\n", ret);
		goto err_kernel;
	}

	ret = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_item_size, &local_item_size,
				     0, NULL, NULL);
	if(ret != CL_SUCCESS){
		printf("Error enqueuing kernel! Aborting. (%d)\n", ret);
		goto err_kernel;
	}

	ret = clEnqueueReadBuffer(queue, out_o, CL_TRUE, 0, size * sizeof(unsigned int), (void*)out,
				  0, NULL, NULL);
	if(ret != CL_SUCCESS){
		printf("Error copying data from CL device buffer for vector out! Aborting. (%d)\n",
		       ret);
		goto err_out;
	}	
	clock_gettime(CLOCK_MONOTONIC,&end);
	printf("Done. \n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);

	clFlush(queue);
	clFinish(queue);
	
	clReleaseKernel(kernel);
	clReleaseProgram(program);
	clReleaseMemObject(out_o);
	clReleaseMemObject(b_o);
	clReleaseMemObject(a_o);

	free(src_code);

	return 0;

 err_kernel:
	clReleaseKernel(kernel);
 err_program:
	clReleaseProgram(program);
 err_out:
	clReleaseMemObject(out_o);
 err_b:
	clReleaseMemObject(b_o);
 err_a:
	clReleaseMemObject(a_o);
 err_src:
	free(src_code);
 err:
	return -1;
}
#endif

#if __TEST_MCL
int mcl_add(unsigned int* a, unsigned int* b, size_t size, unsigned int* out)
{
	mcl_handle*     hdl;
	struct timespec start, end;
	uint64_t        pes[MCL_DEV_DIMS] = {size,1,1};

	hdl = mcl_task_create();
	if(!hdl){
		printf("Error creating MCL task. Aborting.");
		goto err;
	}
	
	if(mcl_task_set_kernel(hdl, "vadd.cl", "VADD", 3, "", 0x0)){
		printf("Error setting %s kernel. Aborting.", "VADD");
		goto err_hdl;
	}

	if(mcl_task_set_arg(hdl, 0, (void*) a, size * sizeof(unsigned int),
		       MCL_ARG_INPUT|MCL_ARG_BUFFER)){
		printf("Error setting up task input a. Aborting.");
		goto err_hdl;
	}

	if(mcl_task_set_arg(hdl, 1, (void*) b, size * sizeof(unsigned int),
		       MCL_ARG_INPUT|MCL_ARG_BUFFER)){
		printf("Error setting up task input b. Aborting.");
		goto err_hdl;
	}
	
	if(mcl_task_set_arg(hdl, 2, (void*) out, size * sizeof(unsigned int),
			    MCL_ARG_OUTPUT|MCL_ARG_BUFFER)){
		printf("Error setting up task output. Aborting.");
		goto err_hdl;
	}

	clock_gettime(CLOCK_MONOTONIC,&start);
	if(mcl_exec(hdl, pes, NULL, flags)){
		printf("Error submitting task! Aborting.");
		goto err_hdl;
	}

	if(mcl_wait(hdl)){
		printf("Request timed out!\n");
		goto err_hdl;
	}	

	clock_gettime(CLOCK_MONOTONIC,&end);

	if(hdl->ret == MCL_RET_SUCCESS)
		printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);
	else
		printf("Error!\n");

	mcl_hdl_free(hdl);
	
	return 0;
	
 err_hdl:
	mcl_hdl_free(hdl);
 err:
	return -1;
}
#endif

int main(int argc, char** argv)
{
	unsigned int              *a, *b, *out;
	int                ret = 1;
	uint64_t           i;

	mcl_banner("EXEC Vector Add Test");
	
	parse_global_opts(argc, argv);

	switch(type){
	case 0:
#ifdef __TEST_MCL
		flags = MCL_TASK_CPU;
#elif __TEST_OCL
		flags = CL_DEVICE_TYPE_CPU;
#endif
		break;
	case 1:
#ifdef __TEST_MCL
		flags = MCL_TASK_GPU;
#elif __TEST_OCL
		flags = CL_DEVICE_TYPE_GPU;
#endif
		break;
#ifdef __TEST_MCL
        case 2:
                flags = MCL_TASK_ANY;
                break;
#endif
	default:
		printf("Unrecognized resource type (%"PRIu64"). Aborting.\n",type);
		return -1;
	}

	a        = (unsigned int*) malloc(size * sizeof(unsigned int));
	b        = (unsigned int*) malloc(size * sizeof(unsigned int));
	out      = (unsigned int*) malloc(size * sizeof(unsigned int));

	if(!a || !b || !out){
		printf("Error allocating memory. Aborting.\n");
		goto err;
	}

#ifdef __TEST_MCL
	mcl_init(workers, 0x0);
#endif
	
	for(i=0; i < size; i++)
		a[i] = INPUT_VALUE;
	for(i=0; i < size; i++)
		b[i] = INPUT_VALUE;

#ifdef VERBOSE
	printf("a = [ ");
	for(i=0; i < size; i++)
		printf("%u ", a[i]);
	printf("]\n");

	printf("b = [ ");
	for(i=0; i < size; i++)
		printf("%u ", b[i]);
	printf("]\n");
#endif

#ifdef __TEST_OCL
	printf("OpenCL test...");
	ocl_add(a, b, size, out);
#endif

#ifdef __TEST_MCL
	printf("MCL test...");
	mcl_add(a, b, size, out);

	mcl_finit();
#endif

#ifdef VERBOSE
	printf("out = [ ");
	for(i=0; i < size; i++)
		printf("%u ", out[i]);
	printf("]\n");
#endif
	
	ret = verify_result(out, size);
	mcl_verify(ret);
	
   	free(a);
	free(b);
	free(out);

	return ret;
 err:
	exit(EXIT_FAILURE);
}
