#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>

#include <minos.h>
#include "utils.h"

//#define VERBOSE

static inline int test_results(float* z, size_t n)
{
	int i;

	for(i=0; i<n; i++)
		if(z[i] != 2.0f){
			printf("Element %d: %f != %f!\n", i, z[i], 2.0f);
			return 1;
		}

	return 0;
}

int saxpy_seq(float a, float* x, float* y, float* z, size_t size)
{
	int i;

	for(i=0; i<size; i++)
		z[i] = a*x[i] + y[i];
			
	return 0;
}


#ifdef __TEST_OCL
int test_ocl(float a, float* x, float* y, float* z, size_t size)
{
	cl_device_id*    device;
	struct timespec  start, end;
	char*            src_code = NULL;
	size_t           src_size = 0;
        size_t           num_kernels = 0;
	cl_int           ret = 0;
	cl_context       context;
	cl_command_queue queue;
	cl_mem           x_o, y_o, z_o;
	cl_program       program = NULL;
	cl_kernel        kernel = NULL;
	size_t           global_item_size = size;
	size_t           local_item_size = 4;
	int              i;
	size_t           klen;
        char*            knames;
       char             src_path[1024];

        strcpy(src_path, XSTR(_MCL_TEST_PATH));
        strcat(src_path, "/saxpy.cl");
	printf("OpenCL Test...");

#ifdef VERBOSE
	printf("\nx = [ ");
	for(i=0; i<size; i++)
		printf("%f ", x[i]);
	printf("]\n");

	printf("y = [ ");
	for(i=0; i<size; i++)
		printf("%f ", y[i]);
	printf("]\n");
#endif

	if(mcl_load(src_path, &src_code)){
		printf("Error loading OpenCL kernel! Aborting.\n");
		goto err;
	}

#ifdef VERBOSE
	printf("\n\n %s \n\n", src_code);
#endif

	if(ocl_setup(&device, &context, &queue, flags, 0))
		goto err_src;
	
	clock_gettime(CLOCK_MONOTONIC,&start);
	for(i=0; i<rep; i++){
		x_o = clCreateBuffer(context, CL_MEM_READ_ONLY, size * sizeof(float), NULL, &ret);
		if(ret != CL_SUCCESS){
			printf("Error creating memory object for vector x! Aborting. (%d)\n", ret);
			goto err_setup;
		}

		y_o = clCreateBuffer(context, CL_MEM_READ_ONLY, size * sizeof(float), NULL, &ret);
		if(ret != CL_SUCCESS){
			printf("Error creating memory object for vector y! Aborting. (%d)\n", ret);
			goto err_x;
		}
		
		z_o = clCreateBuffer(context, CL_MEM_WRITE_ONLY, size * sizeof(float), NULL, &ret);
		if(ret != CL_SUCCESS){
			printf("Error creating memory object for vector z! Aborting. (%d)\n", ret);
			goto err_y;
		}
		
		ret = clEnqueueWriteBuffer(queue, x_o, CL_TRUE, 0, size * sizeof(float), x, 0, NULL, NULL);
		if(ret != CL_SUCCESS){
			printf("Error copying data to CL device buffer for vector x! Aborting. (%d)\n",
			       ret);
			goto err_z;
		}

		ret = clEnqueueWriteBuffer(queue, y_o, CL_TRUE, 0, size * sizeof(float), y, 0, NULL, NULL);
		if(ret != CL_SUCCESS){
			printf("Error copying data to CL device buffer for vector y! Aborting. (%d)\n",
			       ret);
			goto err_z;
		}
		clFlush(queue);
		
		src_size = strlen(src_code);		
		program = clCreateProgramWithSource(context, 1, (const char **)&src_code,
						    (const size_t *)&src_size, &ret);
		if(ret != CL_SUCCESS){
			printf("Error creating program! Aborting. (%d)\n", ret);
			goto err_z;
		}		

		ret = clBuildProgram(program, 1, &(device[0]), NULL, NULL, NULL);
		if(ret != CL_SUCCESS){
			printf("Error building program! Aborting. (%d)\n", ret);
			goto err_program;
		}
		
                ret = clGetProgramInfo(program, CL_PROGRAM_NUM_KERNELS, sizeof(size_t), &num_kernels, NULL);
                if(ret != CL_SUCCESS){
                        printf("Error quering number of kernels in program (%d)\n", ret);
                        goto err_program;
                }
                printf("Found %lu kernels in program\n", num_kernels);

                ret = clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, 0, NULL, &klen);
                if(ret != CL_SUCCESS){
                        printf("Error quering size of program names (%d)\n", ret);
                        goto err_program;
                }

                knames = (char*) malloc(klen);
                if(!knames){
                        printf("Error allocating memory\n");
                        goto err_program;
                }

                ret = clGetProgramInfo(program, CL_PROGRAM_KERNEL_NAMES, klen, knames, NULL);
                if(ret != CL_SUCCESS){
                        printf("Error quering kernel names (%d)\n", ret);
                        goto err_program;
                }

                printf("Kernels in program %s (size %lu)\n", knames, klen);

		kernel = clCreateKernel(program, "SAXPY", &ret);
		if(ret != CL_SUCCESS){
			printf("Error creating kernel! Aborting. (%d)\n", ret);
			goto err_program;
		}
				
		ret  = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&x_o);
		ret |= clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *)&y_o);
		ret |= clSetKernelArg(kernel, 2, sizeof(float),  (void *)&a);
		ret |= clSetKernelArg(kernel, 3, sizeof(cl_mem), (void *)&z_o);
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
		
		ret = clEnqueueReadBuffer(queue, z_o, CL_TRUE, 0, size * sizeof(float), z, 0, NULL, NULL);
		if(ret != CL_SUCCESS){
			printf("Error copying data back from device! (%d)\n", ret);
			goto err_kernel;
		}
		clFlush(queue);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
		
	printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);

	clFlush(queue);
	clFinish(queue);
	
	clReleaseKernel(kernel);
	clReleaseProgram(program);
	clReleaseMemObject(z_o);
	clReleaseMemObject(y_o);
	clReleaseMemObject(x_o);

	
	clReleaseCommandQueue(queue);
	clReleaseContext(context);

	free(src_code);
	
	return 0;

 err_kernel:
	clReleaseKernel(kernel);
 err_program:
	clReleaseProgram(program);
 err_z:
	clReleaseMemObject(z_o);
 err_y:
	clReleaseMemObject(y_o);
 err_x:
	clReleaseMemObject(x_o);
 err_setup:
	clReleaseCommandQueue(queue);
	clReleaseContext(context);
 err_src:
	free(src_code);
 err:
	return -1;
}
#endif

#ifdef __TEST_MCL
int test_mcl(float a, float* x, float* y, float* z, size_t size)
{
	struct timespec start, end;
	mcl_handle**    hdl = NULL;
	uint64_t        pes[MCL_DEV_DIMS] = {size,1,1};
	uint64_t        i;
	unsigned int    errs = 0;
	char            src_path[1024];

        strcpy(src_path, XSTR(_MCL_TEST_PATH));
        strcat(src_path, "/saxpy.cl");

	printf("MCL Test...");

#ifdef VERBOSE
	printf("\nx = [ ");
	for(i=0; i<size; i++)
		printf("%f ", x[i]);
	printf("]\n");

	printf("y = [ ");
	for(i=0; i<size; i++)
		printf("%f ", y[i]);
	printf("]\n");
#endif
	hdl = (mcl_handle**) malloc(sizeof(mcl_handle*) * rep);
	if(!hdl){
		printf("Error allocating memmory for MCL hanlders. Aborting.\n");
		goto err;
	}

	if(mcl_prg_load(src_path, "", MCL_PRG_SRC)){
                printf("Error loading program. Aborting.\n");
                goto err;
        }

	clock_gettime(CLOCK_MONOTONIC,&start);
	for(i=0; i<rep; i++){
		hdl[i] = mcl_task_create();
		if(!hdl[i]){
			printf("Error creating MCL task. Aborting.");
			continue;
		}
		
		if(mcl_task_set_kernel(hdl[i], "SAXPY", 4)){
			printf("Error setting %s kernel. Aborting.", "SAXPY");
			continue;
		}

		if(mcl_task_set_arg(hdl[i], 0, (void*) x, size * sizeof(float),
				    MCL_ARG_INPUT | MCL_ARG_BUFFER)){
			printf("Error setting up task input. Aborting.");
			continue;
		}
		
		if(mcl_task_set_arg(hdl[i], 1, (void*) y, size * sizeof(float),
				    MCL_ARG_INPUT | MCL_ARG_BUFFER)){
			printf("Error setting up task input. Aborting.");
			continue;
		}
		
		if(mcl_task_set_arg(hdl[i], 2, (void*) &a, sizeof(float),
				    MCL_ARG_INPUT | MCL_ARG_SCALAR)){
			printf("Error setting up task input. Aborting.");
			continue;
		}

		if(mcl_task_set_arg(hdl[i], 3, (void*) z, size * sizeof(float),
				    MCL_ARG_OUTPUT | MCL_ARG_BUFFER)){
			printf("Error setting up task output. Aborting.");
			continue;
		}

		if(mcl_exec(hdl[i], pes, NULL, flags)){
			printf("Error submitting task! Aborting.");
			continue;
		}

		if(synct)
			if(mcl_wait(hdl[i])){
				printf("Request timed out!\n");
				continue;
			}
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	if(!synct)
		if(mcl_wait_all()){
			printf("Error waiting for requests to complete!\n");
			goto err_hdl;		
		}
	
	for(i=0; i<rep; i++)
		if(hdl[i]->ret == MCL_RET_ERROR){
			printf("Error executing task %"PRIu64"!\n", i);
			errs++;
		}
	if(errs)
		printf("Detected %u errors!\n",errs);
	else
		printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);

	for(i=0; i<rep; i++)
		mcl_hdl_free(hdl[i]);

	free(hdl);
	
	return 0;

 err_hdl:
	free(hdl);
 err:
	return -1;
}
#endif



int main(int argc, char** argv)
{
	float           *x, *y, *z, a, *z_test;
	int             i, ret = -1;
	struct timespec start, end;
	
	mcl_banner("SAXPY Test");

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

#ifdef __TEST_MCL
	mcl_init(workers,0x0);
#endif
	
	x = (float*) malloc(size * sizeof(float));
 	y = (float*) malloc(size * sizeof(float));
	z = (float*) malloc(size * sizeof(float));
	z_test = (float*) malloc(size * sizeof(float));
	
	if(!x || !y || !z || !z_test){
		printf("Error allocating vectors. Aborting.");
		goto err;
	}

	a = 1.0f;
	for(i=0; i<size; i++)
		x[i] = 1.0f;
	for(i=0; i<size; i++)
		y[i] = 1.0f;

#ifdef VERBOSE
	printf("a = %f\n", a);
	
	printf("x = [ ");
	for(i=0; i<size; i++)
		printf("%f ", x[i]);
	printf("]\n");

	printf("y = [ ");
	for(i=0; i<size; i++)
		printf("%f ", y[i]);
	printf("]\n");
#endif

	printf("Sequential Test...");
	clock_gettime(CLOCK_MONOTONIC,&start);
	saxpy_seq(a,x,y,z_test,size);
	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("Done.\n  Test time: %f seconds\n", ((float)tdiff(end,start))/BILLION);

#ifdef VERBOSE
	printf("z = [ ");
	for(i=0; i<size; i++)
		printf("%f ", z_test[i]);
	printf("]\n");
#endif

	printf("\n");
	
#ifdef __TEST_OCL
	test_ocl(a,x,y,z,size);
#else
	test_mcl(a,x,y,z,size);
#endif

#ifdef VERBOSE
	printf("z = [ ");
	for(i=0; i<size; i++)
		printf("%f ", z[i]);
	printf("]\n");
#endif

       	ret = test_results(z, size);

#ifdef __TEST_MCL
	mcl_finit();
#endif
	mcl_verify(ret);

	free(z_test);
	free(z);
	free(y);
	free(x);

 err:
	return ret;
}
