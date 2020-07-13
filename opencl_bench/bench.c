#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

// -----------
// use static data size
// #define DATA_SIZE (1024)
// source size of opencl kernel
#define MAX_SOURCE_SIZE 0x100000

// -----------

int DATA_SIZE = 4096;
int TEST_ITER = 5000;

int main(int argc, char **argv)
{
	int err;

	// read kernel code
	FILE *fp;
	char fileName[] = "./bench.cl";
	char *KernelSource;
	size_t source_size;
	fp = fopen(fileName, "r");
	if (!fp)
	{
		exit(1);
	}
	KernelSource = (char *)malloc(MAX_SOURCE_SIZE);
	source_size = fread(KernelSource, 1, MAX_SOURCE_SIZE, fp);
	fclose(fp);

	// generate data for testing //

	// create random data with float
	int i = 0;
	unsigned int count = DATA_SIZE;
	float data[DATA_SIZE];
	for (i = 0; i < count; i++)
		data[i] = rand() / (float)RAND_MAX;
	data[i] = (float)i;

	int data_int[DATA_SIZE];
	for (i = 0; i < count; i++)
	{
		data_int[i] = (int)i;
	}

	// create random data with int
	long int data_long_int[DATA_SIZE];
	for (i = 0; i < count; i++)
	{
		// data_int[i] = (long int)(rand() / 1000);
		data_int[i] = (long int)i;
	}

	// Connect to a compute device
	int gpu = 1;
	cl_device_id device_id;
	err = clGetDeviceIDs(NULL, gpu ? CL_DEVICE_TYPE_GPU : CL_DEVICE_TYPE_CPU, 1, &device_id, NULL);
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to create a device group!\n");
		return EXIT_FAILURE;
	}

	// Create a compute context
	cl_context context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
	if (!context)
	{
		printf("Error: Failed to create a compute context!\n");
		return EXIT_FAILURE;
	}

	// create command queue
	cl_command_queue commands = clCreateCommandQueue(context, device_id, 0, &err);
	if (!commands)
	{
		printf("Error: Failed to create a command commands!\n");
		return EXIT_FAILURE;
	}

	cl_program program = clCreateProgramWithSource(context, 1, (const char **)&KernelSource, NULL, &err);
	if (!program)
	{
		printf("Error: Failed to create compute program!\n");
		return EXIT_FAILURE;
	}

	// build program executable
	err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (err != CL_SUCCESS)
	{
		size_t len;
		char buffer[2048];

		printf("Error: Failed to build program executable!\n");
		clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
		printf("%s\n", buffer);
		exit(1);
	}

	clock_t start_init, end_init;
	start_init = clock();

	// Create the compute kernel in the program we wish to run
	//
	cl_kernel kernel_square = clCreateKernel(program, "square", &err);
	cl_kernel kernel_vector_add = clCreateKernel(program, "vector_add", &err);
	cl_kernel kernel_logistic_map_int = clCreateKernel(program, "logistic_map_10", &err);
	cl_kernel kernel_logistic_map_long_int = clCreateKernel(program, "logistic_map_long_int", &err);
	if (!kernel_square || !kernel_vector_add || err != CL_SUCCESS)
	{
		printf("Error: Failed to create compute kernel!\n");
		exit(1);
	}

	// Create the input and output arrays in device memory for our calculation
	cl_mem input = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float) * count, NULL, NULL);
	cl_mem output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float) * count, NULL, NULL);
	cl_mem output_vector_add = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float) * count, NULL, NULL);

	cl_mem input_int = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(int) * count, NULL, NULL);
	cl_mem input_long_int = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(long int) * count, NULL, NULL);
	cl_mem output_logistic_map_int = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(int) * count, NULL, NULL);
	cl_mem output_logistic_map_long_int = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(long int) * count, NULL, NULL);

	if (!input || !output)
	{
		printf("Error: Failed to allocate device memory!\n");
		exit(1);
	}

	// Write our data set into the input array in device memory
	err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float) * count, data, 0, NULL, NULL);
	err = clEnqueueWriteBuffer(commands, input_int, CL_TRUE, 0, sizeof(int) * count, data_int, 0, NULL, NULL);
	err = clEnqueueWriteBuffer(commands, input_long_int, CL_TRUE, 0, sizeof(long int) * count, data_long_int, 0, NULL, NULL);
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to write to source array!\n");
		exit(1);
	}

	end_init = clock();
	printf("initialization is %f(s)\n", (double)(end_init - start_init) / CLOCKS_PER_SEC);

	// ==================

	// Get the maximum work group size for executing the kernel on the device
	size_t local;
	err = clGetKernelWorkGroupInfo(kernel_square, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to retrieve kernel work group info! %d\n", err);
		exit(1);
	}

	// Get the maximum work group size for executing the kernel on the device
	size_t local_logistic_map;
	err = clGetKernelWorkGroupInfo(kernel_square, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local_logistic_map), &local_logistic_map, NULL);
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to retrieve kernel work group info! %d\n", err);
		exit(1);
	}

	size_t global = count;

	// ======================== //
	//  Compute Execution Time  //
	// ======================== //

	// run test on square
	double square_time;
	for (i = 0; i < TEST_ITER; i++)
	{
		clock_t start, end;
		start = clock();

		// Set the arguments to our compute kernel_square
		err = 0;
		err = clSetKernelArg(kernel_square, 0, sizeof(cl_mem), &input);
		err |= clSetKernelArg(kernel_square, 1, sizeof(cl_mem), &output);
		err |= clSetKernelArg(kernel_square, 2, sizeof(unsigned int), &count);
		if (err != CL_SUCCESS)
		{
			printf("Error: Failed to set kernel_square arguments! %d\n", err);
			exit(1);
		}

		err = clEnqueueNDRangeKernel(commands, kernel_square, 1, NULL, &global, &local, 0, NULL, NULL);
		clFinish(commands);
		end = clock();
		square_time += (end - start);
	}
	square_time /= TEST_ITER;
	printf("square_time is %f\n", (double)square_time / CLOCKS_PER_SEC);

	double vector_add_time;
	for (i = 0; i < TEST_ITER; i++)
	{
		clock_t start, end;
		start = clock();

		// Set the arguments to our compute kernel_vector_add
		err = 0;
		err = clSetKernelArg(kernel_vector_add, 0, sizeof(cl_mem), &input);
		err |= clSetKernelArg(kernel_vector_add, 1, sizeof(cl_mem), &input);
		err |= clSetKernelArg(kernel_vector_add, 2, sizeof(cl_mem), &output_vector_add);
		if (err != CL_SUCCESS)
		{
			printf("Error: Failed to set kernel_vector_add arguments! %d\n", err);
			exit(1);
		}

		err = clEnqueueNDRangeKernel(commands, kernel_vector_add, 1, NULL, &global, &local, 0, NULL, NULL);

		// Wait for the command commands to get serviced before reading back results
		clFinish(commands);
		end = clock();
		vector_add_time += end - start;
	}
	vector_add_time /= TEST_ITER;
	printf("vector_add_time is %f\n", (double)vector_add_time / CLOCKS_PER_SEC);

	double logistic_map_int_time;
	for (i = 0; i < TEST_ITER; i++)
	{
		clock_t start, end;
		start = clock();

		// set arguments for logistic_map
		err = 0;
		err |= clSetKernelArg(kernel_logistic_map_int, 0, sizeof(cl_mem), &input_int);
		err |= clSetKernelArg(kernel_logistic_map_int, 1, sizeof(cl_mem), &output_logistic_map_int);
		if (err != CL_SUCCESS)
		{
			printf("Error: Failed to set kernel_logistic_map arguments! %d\n", err);
			exit(1);
		}

		err = clEnqueueNDRangeKernel(commands, kernel_logistic_map_int, 1, NULL, &global, &local_logistic_map, 0, NULL, NULL);

		// Wait for the command commands to get serviced before reading back results
		clFinish(commands);
		end = clock();
		logistic_map_int_time += end - start;
	}
	logistic_map_int_time /= TEST_ITER;
	printf("logistic_map_int_time is %f\n", (double)logistic_map_int_time / CLOCKS_PER_SEC);

	double logistic_map_long_int_time;
	for (i = 0; i < TEST_ITER; i++)
	{
		clock_t start, end;
		start = clock();

		// set arguments for logistic_map
		err = 0;
		err |= clSetKernelArg(kernel_logistic_map_long_int, 0, sizeof(cl_mem), &input_long_int);
		err |= clSetKernelArg(kernel_logistic_map_long_int, 1, sizeof(cl_mem), &output_logistic_map_long_int);
		if (err != CL_SUCCESS)
		{
			printf("Error: Failed to set kernel_logistic_map arguments! %d\n", err);
			exit(1);
		}

		err = clEnqueueNDRangeKernel(commands, kernel_logistic_map_long_int, 1, NULL, &global, &local_logistic_map, 0, NULL, NULL);
		// Wait for the command commands to get serviced before reading back results
		clFinish(commands);
		end = clock();
		logistic_map_long_int_time += end - start;
	}
	logistic_map_long_int_time /= TEST_ITER;
	printf("logistic_map_long_int_time is %f\n", (double)logistic_map_long_int_time / CLOCKS_PER_SEC);

	// ================================== //
	//	Check if the results are correct  //
	// ================================== //

	err = 0;
	// set arguments for vector_add
	err = clSetKernelArg(kernel_square, 0, sizeof(cl_mem), &input);
	err |= clSetKernelArg(kernel_square, 1, sizeof(cl_mem), &output);
	err |= clSetKernelArg(kernel_square, 2, sizeof(unsigned int), &count);

	// set arguments for vector_add
	err = clSetKernelArg(kernel_vector_add, 0, sizeof(cl_mem), &input);
	err |= clSetKernelArg(kernel_vector_add, 1, sizeof(cl_mem), &input);
	err |= clSetKernelArg(kernel_vector_add, 2, sizeof(cl_mem), &output_vector_add);

	// set arguments for logistic_map_int
	err |= clSetKernelArg(kernel_logistic_map_int, 0, sizeof(cl_mem), &input_int);
	err |= clSetKernelArg(kernel_logistic_map_int, 1, sizeof(cl_mem), &output_logistic_map_int);

	// set arguments for logistic_map_long_int
	err |= clSetKernelArg(kernel_logistic_map_long_int, 0, sizeof(cl_mem), &input_long_int);
	err |= clSetKernelArg(kernel_logistic_map_long_int, 1, sizeof(cl_mem), &output_logistic_map_long_int);

	err = clEnqueueNDRangeKernel(commands, kernel_square, 1, NULL, &global, &local, 0, NULL, NULL);
	err = clEnqueueNDRangeKernel(commands, kernel_vector_add, 1, NULL, &global, &local, 0, NULL, NULL);
	err = clEnqueueNDRangeKernel(commands, kernel_logistic_map_int, 1, NULL, &global, &local_logistic_map, 0, NULL, NULL);
	err = clEnqueueNDRangeKernel(commands, kernel_logistic_map_long_int, 1, NULL, &global, &local_logistic_map, 0, NULL, NULL);

	// Read back the results from the device to verify the output
	//
	int results_square[DATA_SIZE];					   // results returned from device
	float results_vector_add[DATA_SIZE];			   // results returned from device
	int results_logistic_map_int[DATA_SIZE];		   // results returned from device
	long int results_logistic_map_long_int[DATA_SIZE]; // results returned from device
	err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float) * count, results_square, 0, NULL, NULL);
	err = clEnqueueReadBuffer(commands, output_vector_add, CL_TRUE, 0, sizeof(float) * count, results_vector_add, 0, NULL, NULL);
	err = clEnqueueReadBuffer(commands, output_logistic_map_int, CL_TRUE, 0, sizeof(int) * count, results_logistic_map_int, 0, NULL, NULL);
	err = clEnqueueReadBuffer(commands, output_logistic_map_long_int, CL_TRUE, 0, sizeof(long int) * count, results_logistic_map_long_int, 0, NULL, NULL);
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to read output array! %d\n", err);
		exit(1);
	}

	// Validate our results
	//
	unsigned int correct = 0;
	for (i = 0; i < count; i++)
	{
		if (results_square[i] == data_int[i] * data_int[i])
			correct++;
	}

	unsigned int correct_vector_add = 0;
	for (i = 0; i < count; i++)
	{
		if (results_vector_add[i] == data[i] + data[i])
		{
			correct_vector_add++;
		}
	}

	unsigned int correct_logistic_map_int = 0;
	for (i = 0; i < count; i++)
	{
		int correct_ans_i = (22 * data_int[i] * (data_int[i] + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		if (results_logistic_map_int[i] == correct_ans_i)
		{
			correct_logistic_map_int++;
		}
		else if (i < 30)
		{
			printf("input: %d, expected %d, but got %d\n", data_int[i], correct_ans_i, results_logistic_map_int[i]);
		}
	}

	unsigned int correct_logistic_map_long_int = 0;
	for (i = 0; i < count; i++)
	{
		long int correct_ans_i = (22 * data_long_int[i] * (data_long_int[i] + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		correct_ans_i = (22 * correct_ans_i * (correct_ans_i + 1)) % 6700417;
		if (results_logistic_map_long_int[i] == correct_ans_i)
		{
			correct_logistic_map_long_int++;
		}
		else if (i < 30)
		{
			printf("input: %ld, expected %ld, but got %ld\n", data_long_int[i], correct_ans_i, results_logistic_map_long_int[i]);
		}
	}

	printf("\n=== result summary ===\n");
	printf("Computed '%d/%d' correct values for square!\n", correct, count);
	printf("Computed '%d/%d' correct values for vector_add!\n", correct_vector_add, count);
	printf("Computed '%d/%d' correct values for logistic_map_int!\n", correct_logistic_map_long_int, count);
	printf("Computed '%d/%d' correct values for logistic_map_long_int!\n", correct_logistic_map_long_int, count);

	// Shutdown and cleanup
	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseMemObject(output_vector_add);
	clReleaseMemObject(output_logistic_map_int);
	clReleaseMemObject(output_logistic_map_long_int);
	clReleaseProgram(program);
	clReleaseKernel(kernel_square);
	clReleaseKernel(kernel_vector_add);
	clReleaseKernel(kernel_logistic_map_int);
	clReleaseKernel(kernel_logistic_map_long_int);
	clReleaseCommandQueue(commands);
	clReleaseContext(context);

	return 0;
}