//general parts
#include <stdio.h>
#include <vector>
#include <memory>
#include <string.h>
#include <chrono>
#include <thread>
#include <iostream>

//ROCM parts
#include "hip/hip_runtime.h"
#include <hipfft.h>

#define GROUP 1


void launch_benchmark_rocFFT_single(bool file_output, FILE* output)
{
	
	const int num_runs = 3;
	if (file_output)
		fprintf(output, "0 - rocFFT FFT + iFFT C2C benchmark 1D batched in single precision\n");
	printf("0 - rocFFT FFT + iFFT C2C benchmark 1D batched in single precision\n");
	double benchmark_result[2] = { 0,0 };//averaged result = sum(system_size/iteration_time)/num_benchmark_samples
	hipfftComplex* inputC = (hipfftComplex*)malloc((uint64_t)sizeof(hipfftComplex)*pow(2, 27));
	for (uint64_t i = 0; i < pow(2, 27); i++) {
		inputC[i].x = 2 * ((float)rand()) / RAND_MAX - 1.0;
		inputC[i].y = 2 * ((float)rand()) / RAND_MAX - 1.0;
	}
	for (int n = 0; n < 26; n++) {
		double run_time[num_runs][2];
		for (int r = 0; r < num_runs; r++) {
			hipfftHandle planC2C;
			hipfftComplex* dataC;

			uint32_t dims[3];
			dims[0] = 4 * pow(2, n); //Multidimensional FFT dimensions sizes (default 1). For best performance (and stability), order dimensions in descendant size order as: x>y>z.   
			if (n == 0) dims[0] = 4096; 
			dims[1] = 64* 32 * pow(2, 16)/dims[0];
			//dims[1] = (dims[1] > 32768) ? 32768 : dims[1];
			if (dims[1] == 0) dims[1] = 1;
			dims[2] = 1;
			
			hipMalloc((void**)&dataC, sizeof(hipfftComplex) * dims[0] * dims[1] * dims[2]);

			hipMemcpy(dataC, inputC, sizeof(hipfftComplex) * dims[0] * dims[1] * dims[2], hipMemcpyHostToDevice);
			if (hipGetLastError() != hipSuccess) {
				fprintf(stderr, "ROCM error: Failed to allocate\n");
				return;
			}
			uint64_t sizeROCM;
			switch (1) {
			case 1:
				hipfftPlan1d(&planC2C, dims[0], HIPFFT_C2C, dims[1]);
				hipfftGetSize1d(planC2C, dims[0], HIPFFT_C2C, dims[1], &sizeROCM);
				break;
			case 2:
				hipfftPlan2d(&planC2C, dims[1], dims[0], HIPFFT_C2C);
				break;
			case 3:
				hipfftPlan3d(&planC2C, dims[2], dims[1], dims[0], HIPFFT_C2C);
				break;
			}

			float totTime = 0;
			uint32_t rocBufferSize = sizeof(float) * 2 * dims[0] * dims[1] * dims[2];
			uint32_t batch = ((3*4096 * 1024.0 * 1024.0) / rocBufferSize > 1000) ? 1000 : (3*4096 * 1024.0 * 1024.0) / rocBufferSize;
			if (batch == 0) batch = 1;
			
			auto timeSubmit = std::chrono::steady_clock::now();
			for (int i = 0; i < batch; i++) {

				hipfftExecC2C(planC2C, dataC, dataC, -1);
				hipfftExecC2C(planC2C, dataC, dataC, 1);
			}
			hipDeviceSynchronize();
			auto timeEnd = std::chrono::steady_clock::now();
			totTime = (std::chrono::duration_cast<std::chrono::microseconds>(timeEnd - timeSubmit).count() * 0.001) / batch;
			run_time[r][0] = totTime;
			if (n > 0) {
				if (r == num_runs - 1) {
					double std_error = 0;
					double avg_time = 0;
					for (uint32_t t = 0; t < num_runs; t++) {
						avg_time += run_time[t][0];
					}
					avg_time /= num_runs;
					for (uint32_t t = 0; t < num_runs; t++) {
						std_error += (run_time[t][0] - avg_time) * (run_time[t][0] - avg_time);
					}
					std_error = sqrt(std_error / num_runs);
					if (file_output)
						fprintf(output, "rocFFT System: %d %dx%d Buffer: %d MB avg_time_per_step: %0.3f ms std_error: %0.3f batch: %d benchmark: %d\n", (int)log2(dims[0]), dims[0], dims[1], rocBufferSize / 1024 / 1024, avg_time, std_error, batch, (int)(((double)rocBufferSize / 1024) / avg_time));

					printf("rocFFT System: %d %dx%d Buffer: %d MB avg_time_per_step: %0.3f ms std_error: %0.3f batch: %d benchmark: %d\n", (int)log2(dims[0]), dims[0], dims[1], rocBufferSize / 1024 / 1024, avg_time, std_error, batch, (int)(((double)rocBufferSize / 1024) / avg_time));
					benchmark_result[0] += ((double)rocBufferSize / 1024) / avg_time;
				}

			}
			hipfftDestroy(planC2C);
			hipFree(dataC);
			hipDeviceSynchronize();
			//hipfftComplex* output_rocFFT = (hipfftComplex*)(malloc(sizeof(hipfftComplex) * dims[0] * dims[1] * dims[2]));
			//hipMemcpy(output_rocFFT, dataC, sizeof(hipfftComplex) * dims[0] * dims[1] * dims[2], hipMemcpyDeviceToHost);
			//hipDeviceSynchronize();
			

		}
	}
	free(inputC);
	benchmark_result[0] /= (26 - 1);
	if (file_output)
		fprintf(output, "Benchmark score rocFFT: %d\n", (int)(benchmark_result[0]));
	printf("Benchmark score rocFFT: %d\n", (int)(benchmark_result[0]));

}
