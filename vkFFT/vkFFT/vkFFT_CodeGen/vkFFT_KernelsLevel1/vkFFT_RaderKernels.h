// This file is part of VkFFT
//
// Copyright (C) 2021 - present Dmitrii Tolmachev <dtolm96@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#ifndef VKFFT_RADERKERNELS_H
#define VKFFT_RADERKERNELS_H

#include "vkFFT/vkFFT_Structs/vkFFT_Structs.h"
#include "vkFFT/vkFFT_CodeGen/vkFFT_StringManagement/vkFFT_StringManager.h"
#include "vkFFT/vkFFT_CodeGen/vkFFT_MathUtils/vkFFT_MathUtils.h"
#include "vkFFT/vkFFT_CodeGen/vkFFT_KernelsLevel0/vkFFT_MemoryManagement/vkFFT_MemoryTransfers/vkFFT_Transfers.h"

static inline void appendFFTRaderStage(VkFFTSpecializationConstantsLayout* sc, VkContainer* stageSize, VkContainer* stageSizeSum, VkContainer* stageAngle, VkContainer* stageRadix, int stageID) {
	if (sc->res != VKFFT_SUCCESS) return;

	VkContainer temp_double;
	temp_double.type = 32;
	VkContainer temp_int;
	temp_int.type = 31;
	VkContainer temp_int1;
	temp_int1.type = 31;

	VkContainer stageNormalization;
	stageNormalization.type = 32;
	VkContainer normalizationValue;
	normalizationValue.type = 31;
	normalizationValue.data.i = 1;

	if ((((sc->actualInverse) && (sc->normalize)) || (sc->convolutionStep && (stageAngle->data.d > 0))) && (stageSize->data.i == 1) && (sc->axis_upload_id == 0) && (!(sc->useBluesteinFFT && (stageAngle->data.d < 0)))) {
		if ((sc->performDCT) && (sc->actualInverse)) {
			if (sc->performDCT == 1)
				normalizationValue.data.i = (sc->sourceFFTSize.data.i - 1) * 2;
			else
				normalizationValue.data.i = sc->sourceFFTSize.data.i * 2;
		}
		else
			normalizationValue.data.i = sc->sourceFFTSize.data.i;
	}
	if (sc->useBluesteinFFT && (stageAngle->data.d > 0) && (stageSize->data.i == 1) && (sc->axis_upload_id == 0)) {
		normalizationValue.data.i *= sc->fft_dim_full.data.i;
	}
	if (normalizationValue.data.i != 1) {
		stageNormalization.data.d = 1.0 / (long double)(normalizationValue.data.i);
	}

	sc->useCoalescedLUTUploadToSM = 0;
	/*char convolutionInverse[10] = "";
	if (sc->convolutionStep) {
		if (stageAngle < 0)
			sprintf(convolutionInverse, ", 0");
		else
			sprintf(convolutionInverse, ", 1");
	}*/
	appendBarrierVkFFT(sc);
	


	

	if (sc->useDisableThreads) {
		temp_int.data.i = 0;
		VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
	}
	//rotate the stage

	VkContainer* localInvocationID = VKFFT_ZERO_INIT;

	if (sc->stridedSharedLayout) {
		localInvocationID = &sc->gl_LocalInvocationID_y;
	}
	else {
		localInvocationID = &sc->gl_LocalInvocationID_x;
	}

	if (stageSize->data.i > 1) {
		VkContainer num_logical_subgroups; 
		num_logical_subgroups.type = 31;
		num_logical_subgroups.data.i = (sc->stridedSharedLayout) ? sc->localSize[1].data.i : sc->localSize[0].data.i;
		VkContainer num_logical_groups;
		num_logical_groups.type = 31;
		VkDivCeil(sc, &num_logical_groups, &sc->fftDim, &num_logical_subgroups);

		for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
			if (((1 + t) * num_logical_subgroups.data.i) > sc->fftDim.data.i) {
				VkContainer current_group_cut;
				current_group_cut.type = 31;
				current_group_cut.data.i = sc->fftDim.data.i - t * num_logical_subgroups.data.i;
				VkIf_lt_start(sc, localInvocationID, &current_group_cut);
			}
			temp_int.data.i = t * num_logical_subgroups.data.i;
			VkAdd(sc, &sc->sdataID, localInvocationID, &temp_int);
			VkMod(sc, &sc->stageInvocationID, &sc->sdataID, stageSize);
			
			if (sc->LUT) {
				VkMul(sc, &sc->LUTId, &sc->stageInvocationID, stageRadix, 0);
				VkAdd(sc, &sc->LUTId, &sc->LUTId, stageSizeSum);
				temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
				VkDiv(sc, &sc->tempInt, &sc->sdataID, &temp_int);
				VkAdd(sc, &sc->LUTId, &sc->LUTId, &sc->tempInt);
				appendGlobalToRegisters(sc, &sc->w, &sc->LUTStruct, &sc->LUTId);
				if (!sc->inverse) {
					VkConjugate(sc, &sc->w, &sc->w);
				}
			}
			else {
				temp_double.data.d = stageAngle->data.d * 2.0 / (stageRadix->data.i);
				VkMul(sc, &sc->angle, &sc->stageInvocationID, &temp_double, 0);
				temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
				VkDiv(sc, &sc->tempInt, &sc->sdataID, &temp_int);
				VkMul(sc, &sc->angle, &sc->angle, &sc->tempInt, 0);
				VkSinCos(sc, &sc->w, &sc->angle);
			}
			
			//sc->tempLen = sprintf(sc->tempStr, "	printf(\"%%d %%f %%f \\n \", %s, %s.x, %s.y);\n\n", sc->gl_LocalInvocationID_x, sc->w, sc->w);
			//VkAppendLine(sc);
			//

			if (sc->resolveBankConflictFirstStages == 1) {
				temp_int.data.i = sc->numSharedBanks / 2;
				VkDiv(sc, &sc->tempInt, &sc->sdataID, &temp_int);
				temp_int.data.i = sc->numSharedBanks / 2 + 1;
				VkMul(sc, &sc->tempInt, &sc->tempInt, &temp_int, 0);
				temp_int.data.i = sc->numSharedBanks / 2;
				VkMod(sc, &sc->sdataID, &sc->sdataID, &temp_int);
				VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
			}
			
			if (sc->stridedSharedLayout) {
				VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
				VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
			}
			else {
				if (sc->localSize[1].data.i > 1) {
					VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
					VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
				}
			}
			appendSharedToRegisters(sc, &sc->regIDs[0], &sc->sdataID);		

			VkMul(sc, &sc->temp, &sc->regIDs[0], &sc->w, 0);
			
			appendRegistersToShared(sc, &sc->sdataID, &sc->temp);

			if (((1 + t) * num_logical_subgroups.data.i) > sc->fftDim.data.i) {
				VkIf_end(sc);				
			}
		}
		if (sc->useDisableThreads) {
			VkIf_end(sc);
		}
		
		
		appendBarrierVkFFT(sc);
		
		
		
		if (sc->useDisableThreads) {
			temp_int.data.i = 0;
			VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
		}		
	}
	int raderTranspose = ((sc->currentRaderContainer->containerFFTNum < 8) || (sc->currentRaderContainer->numStages == 1) || (sc->stridedSharedLayout)) ? 0 : 1;

	// read x0 - to be used in the end
	{
		int locStageRadix = sc->currentRaderContainer->stageRadix[0];
		int logicalStoragePerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix] * sc->registerBoost;// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread * sc->registerBoost : sc->min_registers_per_thread * sc->registerBoost;
		//uint64_t logicalRegistersPerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix];// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread : sc->min_registers_per_thread;
		int locFFTDim = sc->currentRaderContainer->containerFFTDim; //different length due to all -1 cutoffs
		//uint64_t locFFTsCombined = sc->currentRaderContainer->containerFFTNum * locFFTDim;
		//uint64_t logicalGroupSize = (uint64_t)ceil(locFFTsCombined / (double)logicalStoragePerThread);
		VkContainer subLogicalGroupSize;
		subLogicalGroupSize.type = 31;
		temp_int.data.i = locFFTDim;
		temp_int1.data.i = logicalStoragePerThread;
		VkDivCeil(sc, &subLogicalGroupSize, &temp_int, &temp_int1);

		if (!raderTranspose) {
			VkMod(sc, &sc->raderIDx, localInvocationID, &subLogicalGroupSize);
			VkDiv(sc, &sc->raderIDx2, localInvocationID, &subLogicalGroupSize);
		}
		else {
			temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
			VkDiv(sc, &sc->raderIDx, localInvocationID, &temp_int);
			VkMod(sc, &sc->raderIDx2, localInvocationID, &temp_int);
		}
		if (!raderTranspose) {
			temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
			VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
		}
		else {
			VkIf_lt_start(sc, &sc->raderIDx, &subLogicalGroupSize);
		}
		VkMov(sc, &sc->sdataID, &sc->raderIDx2);
		
		if (sc->stridedSharedLayout) {
			VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
			VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
		}
		else {
			if (sc->localSize[1].data.i > 1) {
				VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
				VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);				
			}
		}
		appendSharedToRegisters(sc, &sc->x0[0], &sc->sdataID);

		VkIf_end(sc);
	}
	// read x0 for x0+x1 - 0-element
	{
		int locStageRadix = sc->currentRaderContainer->stageRadix[sc->currentRaderContainer->numStages - 1];
		int logicalStoragePerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix] * sc->registerBoost;// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread * sc->registerBoost : sc->min_registers_per_thread * sc->registerBoost;
		//uint64_t logicalRegistersPerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix];// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread : sc->min_registers_per_thread;
		int locFFTDim = sc->currentRaderContainer->containerFFTDim; //different length due to all -1 cutoffs
		//uint64_t locFFTsCombined = sc->currentRaderContainer->containerFFTNum * locFFTDim;
		//uint64_t logicalGroupSize = (uint64_t)ceil(locFFTsCombined / (double)logicalStoragePerThread);

		VkContainer subLogicalGroupSize;
		subLogicalGroupSize.type = 31;
		temp_int.data.i = locFFTDim;
		temp_int1.data.i = logicalStoragePerThread;
		VkDivCeil(sc, &subLogicalGroupSize, &temp_int, &temp_int1);
		
		if (!raderTranspose) {
			VkMod(sc, &sc->raderIDx, localInvocationID, &subLogicalGroupSize);
			VkDiv(sc, &sc->raderIDx2, localInvocationID, &subLogicalGroupSize);
		}
		else {
			temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
			VkDiv(sc, &sc->raderIDx, localInvocationID, &temp_int);
			VkMod(sc, &sc->raderIDx2, localInvocationID, &temp_int);			
		}
		if (!raderTranspose) {
			temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
			VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
		}
		else {
			VkIf_lt_start(sc, &sc->raderIDx, &subLogicalGroupSize);
		}

		temp_int.data.i = 0;
		VkIf_eq_start(sc, &sc->raderIDx, &temp_int);

		VkMov(sc, &sc->sdataID, &sc->raderIDx2);
		
		if (sc->stridedSharedLayout) {
			VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
			VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
		}
		else {
			if (sc->localSize[1].data.i > 1) {
				VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
				VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
			}
		}
		appendSharedToRegisters(sc, &sc->x0[1], &sc->sdataID);
		
		VkIf_end(sc);
		VkIf_end(sc);
	}
	if (sc->currentRaderContainer->numStages == 1) {
		
		if (sc->useDisableThreads) {
			VkIf_end(sc);
		}

		
		
		appendBarrierVkFFT(sc);
		
		
		
		if (sc->useDisableThreads) {
			temp_int.data.i = 0;
			VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
		}
	}
	int64_t locStageSize = 1;
	int64_t locStageSizeSum = 0;
	long double locStageAngle = -sc->double_PI;
	int64_t shift = 0;
	for (int rader_stage = 0; rader_stage < sc->currentRaderContainer->numStages; rader_stage++) {
		int64_t locStageRadix = sc->currentRaderContainer->stageRadix[rader_stage];
		int64_t logicalStoragePerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix] * sc->registerBoost;// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread * sc->registerBoost : sc->min_registers_per_thread * sc->registerBoost;
		int64_t logicalRegistersPerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix];// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread : sc->min_registers_per_thread;
		int64_t locFFTDim = sc->currentRaderContainer->containerFFTDim; //different length due to all -1 cutoffs
		int64_t locFFTsCombined = sc->currentRaderContainer->containerFFTNum * locFFTDim;
		//uint64_t logicalGroupSize = (uint64_t)ceil(locFFTsCombined / (double)logicalStoragePerThread);

		VkContainer subLogicalGroupSize;
		subLogicalGroupSize.type = 31;
		temp_int.data.i = locFFTDim;
		temp_int1.data.i = logicalStoragePerThread;
		VkDivCeil(sc, &subLogicalGroupSize, &temp_int, &temp_int1);

		int64_t locFFTDimStride = locFFTDim;
		if (shift <= sc->sharedShiftRaderFFT.data.i) locFFTDimStride = locFFTDim + shift;
		//local radix
		if ((rader_stage == 0) || (!raderTranspose)) {
			VkMod(sc, &sc->raderIDx, localInvocationID, &subLogicalGroupSize);
			VkDiv(sc, &sc->raderIDx2, localInvocationID, &subLogicalGroupSize);
		}
		else {
			temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
			VkDiv(sc, &sc->raderIDx, localInvocationID, &temp_int);
			VkMod(sc, &sc->raderIDx2, localInvocationID, &temp_int);
		}

		for (uint64_t k = 0; k < sc->registerBoost; k++) {
			if ((rader_stage == 0) || (!raderTranspose)) {
				temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
				VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);				
			}
			else {
				VkIf_lt_start(sc, &sc->raderIDx, &subLogicalGroupSize);				
			}
			for (uint64_t j = 0; j < logicalRegistersPerThread / locStageRadix; j++) {
				if (subLogicalGroupSize.data.i * ((j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) continue;
				if (subLogicalGroupSize.data.i * ((1 + j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) {
					temp_int.data.i = locFFTDim / locStageRadix - (j + k * logicalRegistersPerThread / locStageRadix) * subLogicalGroupSize.data.i;
					VkIf_lt_start(sc, &sc->raderIDx, &temp_int);
				}

				temp_int.data.i = (j + k * logicalRegistersPerThread / locStageRadix) * subLogicalGroupSize.data.i;
				VkAdd(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
				temp_int.data.i = locStageSize;
				VkMod(sc, &sc->stageInvocationID, &sc->tempInt, &temp_int);
				
				if (sc->LUT) {
					temp_int.data.i = locStageSizeSum + sc->currentRaderContainer->RaderRadixOffsetLUT;
					VkAdd(sc, &sc->LUTId, &sc->stageInvocationID, &temp_int);
				}
				else {
					temp_double.data.d = locStageAngle;
					VkMul(sc, &sc->angle, &sc->stageInvocationID, &temp_double, 0);
				}
				for (int i = 0; i < locStageRadix; i++) {
					int g = sc->currentRaderContainer->generator;
					if (rader_stage == 0) {
						if (sc->inline_rader_g_pow == 1) {
							temp_int.data.i = j * subLogicalGroupSize.data.i + i * locFFTDim / locStageRadix;
							VkAdd(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
							appendConstantToRegisters(sc, &sc->sdataID, &sc->currentRaderContainer->g_powConstantStruct, &sc->tempInt);
						}
						else if (sc->inline_rader_g_pow == 2) {
							temp_int.data.i = j * subLogicalGroupSize.data.i + i * locFFTDim / locStageRadix + sc->currentRaderContainer->raderUintLUToffset;
							VkAdd(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
							appendGlobalToRegisters(sc, &sc->sdataID, &sc->g_powStruct, &sc->tempInt);
						}
						else {
							/*sc->tempLen = sprintf(sc->tempStr, "\
			%s= (%s + %" PRIu64 ");\n\
			%s=1;\n\
			while (%s != 0)\n\
			{\n\
				%s = (%s * %" PRIu64 ") %% %" PRIu64 ";\n\
				%s--;\n\
			}\n", sc->inoutID, sc->raderIDx, j * subLogicalGroupSize + i * locFFTDim / locStageRadix, sc->sdataID, sc->inoutID, sc->sdataID, sc->sdataID, g, stageRadix, sc->inoutID);
							VkAppendLine(sc);
							*/
						}
						temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
						VkMul(sc, &sc->tempInt, &sc->sdataID, &temp_int, 0);
						VkAdd(sc, &sc->sdataID, &sc->raderIDx2, &sc->tempInt);
					}
					else {
						if (!raderTranspose) {
							temp_int.data.i = j * subLogicalGroupSize.data.i + i * locFFTDim / locStageRadix + sc->fftDim.data.i / stageRadix->data.i;
							VkAdd(sc, &sc->sdataID, &sc->raderIDx, &temp_int);
							temp_int.data.i = locFFTDimStride;
							VkMul(sc, &sc->tempInt, &sc->raderIDx2, &temp_int, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
						}
						else {
							temp_int.data.i = j * subLogicalGroupSize.data.i + i * locFFTDim / locStageRadix;
							VkAdd(sc, &sc->sdataID, &sc->raderIDx, &temp_int);
							temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
							VkMul(sc, &sc->sdataID, &sc->sdataID, &temp_int, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->raderIDx2);
							temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);
						}
					}

					int64_t id = j + i * logicalRegistersPerThread / locStageRadix;
					id = (id / logicalRegistersPerThread) * sc->registers_per_thread + id % logicalRegistersPerThread;
					if (!sc->stridedSharedLayout) {
						if (sc->resolveBankConflictFirstStages == 1) {
							temp_int.data.i = sc->numSharedBanks / 2;
							VkDiv(sc, &sc->tempInt, &sc->sdataID, &temp_int);
							temp_int.data.i = sc->numSharedBanks / 2 + 1;
							VkMul(sc, &sc->tempInt, &sc->tempInt, &temp_int, 0);
							temp_int.data.i = sc->numSharedBanks / 2;
							VkMod(sc, &sc->sdataID, &sc->sdataID, &temp_int);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
						}
					}
					if (sc->stridedSharedLayout) {
						VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
					}
					else {
						if (sc->localSize[1].data.i > 1) {
							VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
						}
					}
					appendSharedToRegisters(sc, &sc->regIDs[id], &sc->sdataID);
				}

				VkContainer* regID = (VkContainer*)calloc(locStageRadix, sizeof(VkContainer));
				if (regID) {
					for (uint64_t i = 0; i < locStageRadix; i++) {
						VkContainer id;
						id.type = 31;
						id.data.i = j + k * logicalRegistersPerThread / locStageRadix + i * logicalStoragePerThread / locStageRadix;
						id.data.i = (id.data.i / logicalRegistersPerThread) * sc->registers_per_thread + id.data.i % logicalRegistersPerThread;
						VkAllocateContainerFlexible(sc, &regID[i], 50);
						regID[i].type = sc->regIDs[id.data.i].type;
						VkCopyContainer(sc, &regID[i], &sc->regIDs[id.data.i]);
					}
					inlineRadixKernelVkFFT(sc, locStageRadix, locStageSize, locStageSizeSum, locStageAngle, regID);

					for (uint64_t i = 0; i < locStageRadix; i++) {
						VkContainer id;
						id.type = 31;
						id.data.i = j + k * logicalRegistersPerThread / locStageRadix + i * logicalStoragePerThread / locStageRadix;
						id.data.i = (id.data.i / logicalRegistersPerThread) * sc->registers_per_thread + id.data.i % logicalRegistersPerThread;
						VkCopyContainer(sc, &sc->regIDs[id.data.i], &regID[i]);
						VkDeallocateContainer(sc, &regID[i]);
					}
					free(regID);
					regID = 0;
				}
				else
				{
					sc->res = VKFFT_ERROR_MALLOC_FAILED;
					return;
				}
				if (subLogicalGroupSize.data.i * ((1 + j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) {
					VkIf_end(sc);
				}
			}
			VkIf_end(sc);
		}
		if (rader_stage != sc->currentRaderContainer->numStages - 1) {
			if (sc->useDisableThreads) {
				VkIf_end(sc);
			}

			
			
			appendBarrierVkFFT(sc);
			
			
			
			if (sc->useDisableThreads) {
				temp_int.data.i = 0;
				VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
			}
			
		}
		//local shuffle
		VkContainer* tempID;
		tempID = (VkContainer*)calloc(sc->registers_per_thread * sc->registerBoost, sizeof(VkContainer));
		if (tempID) {
			for (uint64_t i = 0; i < sc->registers_per_thread * sc->registerBoost; i++) {
				VkAllocateContainerFlexible(sc, &tempID[i], 50);
				tempID[i].type = sc->regIDs[0].type;
			}
			for (uint64_t k = 0; k < sc->registerBoost; ++k) {
				uint64_t t = 0;

				if ((rader_stage == 0) || (!raderTranspose)) {
					temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
				}
				else {
					VkIf_lt_start(sc, &sc->raderIDx, &subLogicalGroupSize);
				}
				//last stage - save x1
				if (rader_stage == sc->currentRaderContainer->numStages - 1) {
					temp_int.data.i = 0;
					VkIf_eq_start(sc, &sc->raderIDx, &temp_int);
					
					VkAdd(sc, &sc->x0[1], &sc->x0[1], &sc->regIDs[0]);
					
					VkIf_end(sc);
				}
				if (!sc->stridedSharedLayout) {
					if (rader_stage != 0) {
						shift = (subLogicalGroupSize.data.i > (locFFTDim % (sc->numSharedBanks / 2))) ? subLogicalGroupSize.data.i - locFFTDim % (sc->numSharedBanks / 2) : 0;
						if (shift <= sc->sharedShiftRaderFFT.data.i) locFFTDimStride = locFFTDim + shift;
					}
					else {
						if (sc->sharedShiftRaderFFT.data.i > 0) {
							VkIf_end(sc);
							
							VkMov(sc, &sc->sharedStride, &sc->sharedStrideRaderFFT);
							
							if ((rader_stage == 0) || (!raderTranspose)) {
								temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
								VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
							}
							else {
								VkIf_lt_start(sc, &sc->raderIDx, &subLogicalGroupSize);
							}
						}
						shift = ((locFFTDim % (sc->numSharedBanks / 2))) ? 0 : 1;
						if (shift <= sc->sharedShiftRaderFFT.data.i) locFFTDimStride = locFFTDim + shift;
					}
				}
				for (int64_t j = 0; j < logicalRegistersPerThread / locStageRadix; j++) {
					if (subLogicalGroupSize.data.i * ((j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) <= locFFTDim) {
						if (subLogicalGroupSize.data.i * ((1 + j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) {
							temp_int.data.i = locFFTDim / locStageRadix - (j + k * logicalRegistersPerThread / locStageRadix) * subLogicalGroupSize.data.i;
							VkIf_lt_start(sc, &sc->raderIDx, &temp_int);
						}
						temp_int.data.i = j * subLogicalGroupSize.data.i;
						VkAdd(sc, &sc->stageInvocationID, &sc->raderIDx, &temp_int);
						
						VkMov(sc, &sc->blockInvocationID, &sc->stageInvocationID);
						
						temp_int.data.i = locStageSize;
						VkMod(sc, &sc->stageInvocationID, &sc->stageInvocationID, &temp_int);
						
						VkSub(sc, &sc->blockInvocationID, &sc->blockInvocationID, &sc->stageInvocationID);
						
						temp_int.data.i = locStageRadix;
						VkMul(sc, &sc->inoutID, &sc->blockInvocationID, &temp_int, 0);
						
						VkAdd(sc, &sc->inoutID, &sc->inoutID, &sc->stageInvocationID);
						
					}
					/*sc->tempLen = sprintf(sc->tempStr, "\
	stageInvocationID = (gl_LocalInvocationID.x + %" PRIu64 ") %% (%" PRIu64 ");\n\
	blockInvocationID = (gl_LocalInvocationID.x + %" PRIu64 ") - stageInvocationID;\n\
	inoutID = stageInvocationID + blockInvocationID * %" PRIu64 ";\n", j * logicalGroupSize, stageSize, j * logicalGroupSize, stageRadix);*/

					for (uint64_t i = 0; i < locStageRadix; i++) {
						VkContainer id;
						id.type = 31;
						id.data.i = j + k * logicalRegistersPerThread / locStageRadix + i * logicalStoragePerThread / locStageRadix;
						id.data.i = (id.data.i / logicalRegistersPerThread) * sc->registers_per_thread + id.data.i % logicalRegistersPerThread;
						VkCopyContainer(sc, &tempID[t + k * sc->registers_per_thread], &sc->regIDs[id.data.i]);
						t++;
						if (subLogicalGroupSize.data.i * ((j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) <= locFFTDim) {
							temp_int.data.i = i * locStageSize;
							VkAdd(sc, &sc->combinedID, &sc->inoutID, &temp_int);
							//last stage - mult rader kernel
							if (rader_stage == sc->currentRaderContainer->numStages - 1) {
								if (sc->inline_rader_kernel) {
									appendConstantToRegisters_x(sc, &sc->w, &sc->currentRaderContainer->r_rader_kernelConstantStruct, &sc->combinedID);
									appendConstantToRegisters_y(sc, &sc->w, &sc->currentRaderContainer->i_rader_kernelConstantStruct, &sc->combinedID);
								}
								else {
									temp_int.data.i = sc->currentRaderContainer->RaderKernelOffsetLUT;
									VkAdd(sc, &sc->tempInt, &sc->combinedID, &temp_int);
									appendGlobalToRegisters(sc, &sc->w, &sc->LUTStruct, &sc->tempInt);
								}
								/*sc->tempLen = sprintf(sc->tempStr, "\
		printf(\"%%f %%f - %%f %%f\\n\", %s.x, %s.y, %s.x, %s.y);\n", sc->regIDs[id], sc->regIDs[id], sc->w, sc->w);
					VkAppendLine(sc);
								*/
								VkMul(sc, &sc->regIDs[id.data.i], &sc->regIDs[id.data.i], &sc->w, &sc->temp);
								
							}
							if (rader_stage != sc->currentRaderContainer->numStages - 1) {
								if (!raderTranspose) {
									temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
									VkAdd(sc, &sc->sdataID, &sc->combinedID, &temp_int);
									
									temp_int.data.i = locFFTDimStride;
									VkMul(sc, &sc->tempInt, &sc->raderIDx2, &temp_int, 0);
									VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
									
								}
								else {
									temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
									VkMul(sc, &sc->sdataID, &sc->combinedID, &temp_int, 0);
									
									temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
									VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);

									VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->raderIDx2);
								}
								if (!sc->stridedSharedLayout) {
									if (0 && (locStageSize <= sc->numSharedBanks / 2) && (locFFTsCombined > sc->numSharedBanks / 2) && (sc->sharedStrideBankConflictFirstStages.data.i != locFFTDim / sc->registerBoost) && ((locFFTDim & (locFFTDim - 1)) == 0) && (locStageSize * locStageRadix != locFFTDim)) {
										if (sc->resolveBankConflictFirstStages == 0) {
											sc->resolveBankConflictFirstStages = 1;
											VkMov(sc, &sc->sharedStride, &sc->sharedStrideBankConflictFirstStages);
										}
										temp_int.data.i = sc->numSharedBanks / 2;
										VkDiv(sc, &sc->tempInt, &sc->sdataID, &temp_int);
										temp_int.data.i = sc->numSharedBanks / 2 + 1;
										VkMul(sc, &sc->tempInt, &sc->tempInt, &temp_int, 0);
										temp_int.data.i = sc->numSharedBanks / 2;
										VkMod(sc, &sc->sdataID, &sc->sdataID, &temp_int);
										VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);

									}
									else {
										if (sc->resolveBankConflictFirstStages == 1) {
											sc->resolveBankConflictFirstStages = 0;
											VkMov(sc, &sc->sharedStride, &sc->sharedStrideReadWriteConflict);
										}
									}
								}
								if (sc->stridedSharedLayout) {
									VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
									
									VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
									
								}
								else {
									if (sc->localSize[1].data.i > 1) {
										VkMul(sc, &sc->combinedID, &sc->gl_LocalInvocationID_y, &sc->sharedStride, 0);
										
										VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->combinedID);
										
									}
								}
								//sprintf(sc->sdataID, "sharedStride * gl_LocalInvocationID.y + inoutID + %" PRIu64 "", i * stageSize);
								appendRegistersToShared(sc, &sc->sdataID, &sc->regIDs[id.data.i]);
								
							}
						}
						/*sc->tempLen = sprintf(sc->tempStr, "\
sdata[sharedStride * gl_LocalInvocationID.y + inoutID + %" PRIu64 "] = temp%s%s;\n", i * stageSize, sc->regIDs[id], stageNormalization);*/
					}
					if (subLogicalGroupSize.data.i * ((j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) <= locFFTDim) {
						if (subLogicalGroupSize.data.i * ((1 + j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) {
							VkIf_end(sc);
						}
					}
				}
				VkIf_end(sc);
				
				for (uint64_t j = logicalRegistersPerThread; j < sc->registers_per_thread; j++) {
					VkCopyContainer(sc, &tempID[t + k * sc->registers_per_thread], &sc->regIDs[t + k * sc->registers_per_thread]);
					t++;
				}
				t = 0;
			}
			if (rader_stage != sc->currentRaderContainer->numStages - 1) {
				for (uint64_t i = 0; i < sc->registers_per_thread * sc->registerBoost; i++) {
					VkCopyContainer(sc, &sc->regIDs[i], &tempID[i]);
				}
				for (uint64_t i = 0; i < sc->registers_per_thread * sc->registerBoost; i++) {
					VkDeallocateContainer(sc, &tempID[i]);
				}
			}
			
			free(tempID);
			tempID = 0;
		}
		else
			sc->res = VKFFT_ERROR_MALLOC_FAILED;

		if (rader_stage > 0) {
			switch (locStageRadix) {
			case 2:
				locStageSizeSum += locStageSize;
				break;
			case 3:
				locStageSizeSum += locStageSize * 2;
				break;
			case 4:
				locStageSizeSum += locStageSize * 2;
				break;
			case 5:
				locStageSizeSum += locStageSize * 4;
				break;
			case 6:
				locStageSizeSum += locStageSize * 5;
				break;
			case 7:
				locStageSizeSum += locStageSize * 6;
				break;
			case 8:
				locStageSizeSum += locStageSize * 3;
				break;
			case 9:
				locStageSizeSum += locStageSize * 8;
				break;
			case 10:
				locStageSizeSum += locStageSize * 9;
				break;
			case 11:
				locStageSizeSum += locStageSize * 10;
				break;
			case 12:
				locStageSizeSum += locStageSize * 11;
				break;
			case 13:
				locStageSizeSum += locStageSize * 12;
				break;
			case 14:
				locStageSizeSum += locStageSize * 13;
				break;
			case 15:
				locStageSizeSum += locStageSize * 14;
				break;
			case 16:
				locStageSizeSum += locStageSize * 4;
				break;
			case 32:
				locStageSizeSum += locStageSize * 5;
				break;
			default:
				locStageSizeSum += locStageSize * (locStageRadix);
				break;
			}
		}
		locStageSize *= locStageRadix;
		locStageAngle /= locStageRadix;

		if (rader_stage != sc->currentRaderContainer->numStages - 1) {
			if (sc->useDisableThreads) {
				VkIf_end(sc);
			}

			
			
			appendBarrierVkFFT(sc);
			
			
			
			if (sc->useDisableThreads) {
				temp_int.data.i = 0;
				VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
			}
			
		}
	}

	//iFFT
	locStageSize = 1;
	locStageAngle = sc->double_PI;
	locStageSizeSum = 0;
	for (int64_t rader_stage = sc->currentRaderContainer->numStages - 1; rader_stage >= 0; rader_stage--) {
		int64_t locStageRadix = sc->currentRaderContainer->stageRadix[rader_stage];
		int64_t logicalStoragePerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix] * sc->registerBoost;// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread * sc->registerBoost : sc->min_registers_per_thread * sc->registerBoost;
		int64_t logicalRegistersPerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix];// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread : sc->min_registers_per_thread;
		int64_t locFFTDim = sc->currentRaderContainer->containerFFTDim; //different length due to all -1 cutoffs
		int64_t locFFTsCombined = sc->currentRaderContainer->containerFFTNum * locFFTDim;
		//uint64_t logicalGroupSize = (uint64_t)ceil(locFFTsCombined / (double)logicalStoragePerThread);
		VkContainer subLogicalGroupSize;
		subLogicalGroupSize.type = 31;
		temp_int.data.i = locFFTDim;
		temp_int1.data.i = logicalStoragePerThread;
		VkDivCeil(sc, &subLogicalGroupSize, &temp_int, &temp_int1);
		int64_t locFFTDimStride = locFFTDim; //different length due to all -1 cutoffs
		if (shift <= sc->sharedShiftRaderFFT.data.i) locFFTDimStride = locFFTDim + shift;
		//local radix
		if (!raderTranspose) {
			VkMod(sc, &sc->raderIDx, localInvocationID, &subLogicalGroupSize);
			VkDiv(sc, &sc->raderIDx2, localInvocationID, &subLogicalGroupSize);
		}
		else {
			temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
			VkDiv(sc, &sc->raderIDx, localInvocationID, &temp_int);
			VkMod(sc, &sc->raderIDx2, localInvocationID, &temp_int);
		}
		for (uint64_t k = 0; k < sc->registerBoost; k++) {
			if (!raderTranspose) {
				temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
				VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
			}
			else {
				VkIf_lt_start(sc, &sc->raderIDx, &subLogicalGroupSize);
			}
			for (uint64_t j = 0; j < logicalRegistersPerThread / locStageRadix; j++) {
				if (subLogicalGroupSize.data.i * ((j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) continue;
				if (subLogicalGroupSize.data.i * ((1 + j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) {
					temp_int.data.i = locFFTDim / locStageRadix - (j + k * logicalRegistersPerThread / locStageRadix) * subLogicalGroupSize.data.i;
					VkIf_lt_start(sc, &sc->raderIDx, &temp_int);				
				}

				temp_int.data.i = (j + k * logicalRegistersPerThread / locStageRadix) * subLogicalGroupSize.data.i;
				VkAdd(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
				temp_int.data.i = locStageSize;
				VkMod(sc, &sc->stageInvocationID, &sc->tempInt, &temp_int);


				if (sc->LUT) {
					temp_int.data.i = locStageSizeSum + sc->currentRaderContainer->RaderRadixOffsetLUTiFFT;
					VkAdd(sc, &sc->LUTId, &sc->stageInvocationID, &temp_int);
				}
				else {
					temp_double.data.d = locStageAngle;
					VkMul(sc, &sc->angle, &sc->stageInvocationID, &temp_double, 0);
				}

				if (rader_stage != (int64_t)sc->currentRaderContainer->numStages - 1) {
					for (uint64_t i = 0; i < locStageRadix; i++) {
						uint64_t id = j + i * logicalRegistersPerThread / locStageRadix;
						id = (id / logicalRegistersPerThread) * sc->registers_per_thread + id % logicalRegistersPerThread;
						if (!raderTranspose) {
							temp_int.data.i = j * subLogicalGroupSize.data.i + i * locFFTDim / locStageRadix + sc->fftDim.data.i / stageRadix->data.i;
							VkAdd(sc, &sc->sdataID, &sc->raderIDx, &temp_int);
							temp_int.data.i = locFFTDimStride;
							VkMul(sc, &sc->tempInt, &sc->raderIDx2, &temp_int, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
						}
						else {
							temp_int.data.i = j * subLogicalGroupSize.data.i + i * locFFTDim / locStageRadix;
							VkAdd(sc, &sc->sdataID, &sc->raderIDx, &temp_int);
							temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
							VkMul(sc, &sc->sdataID, &sc->sdataID, &temp_int, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->raderIDx2);
							temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);
						}
						if (!sc->stridedSharedLayout) {
							if (sc->resolveBankConflictFirstStages == 1) {
								temp_int.data.i = sc->numSharedBanks / 2;
								VkDiv(sc, &sc->tempInt, &sc->sdataID, &temp_int);
								temp_int.data.i = sc->numSharedBanks / 2 + 1;
								VkMul(sc, &sc->tempInt, &sc->tempInt, &temp_int, 0);
								temp_int.data.i = sc->numSharedBanks / 2;
								VkMod(sc, &sc->sdataID, &sc->sdataID, &temp_int);
								VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
							}
						}
						if (sc->stridedSharedLayout) {
							VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
						}
						else {
							if (sc->localSize[1].data.i > 1) {
								VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
								VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
							}
						}
						appendSharedToRegisters(sc, &sc->regIDs[id], &sc->sdataID);
					}
				}
				VkContainer* regID = (VkContainer*)calloc(locStageRadix, sizeof(VkContainer));
				if (regID) {
					for (uint64_t i = 0; i < locStageRadix; i++) {
						VkContainer id;
						id.type = 31;
						id.data.i = j + k * logicalRegistersPerThread / locStageRadix + i * logicalStoragePerThread / locStageRadix;
						id.data.i = (id.data.i / logicalRegistersPerThread) * sc->registers_per_thread + id.data.i % logicalRegistersPerThread;
						VkAllocateContainerFlexible(sc, &regID[i], 50);
						regID[i].type = sc->regIDs[id.data.i].type;
						VkCopyContainer(sc, &regID[i], &sc->regIDs[id.data.i]);
					}
					inlineRadixKernelVkFFT(sc, locStageRadix, locStageSize, locStageSizeSum, locStageAngle, regID);

					for (uint64_t i = 0; i < locStageRadix; i++) {
						VkContainer id;
						id.type = 31;
						id.data.i = j + k * logicalRegistersPerThread / locStageRadix + i * logicalStoragePerThread / locStageRadix;
						id.data.i = (id.data.i / logicalRegistersPerThread) * sc->registers_per_thread + id.data.i % logicalRegistersPerThread;
						VkCopyContainer(sc, &sc->regIDs[id.data.i], &regID[i]);
						VkDeallocateContainer(sc, &regID[i]);
					}
					free(regID);
					regID = 0;
				}
				else
				{
					sc->res = VKFFT_ERROR_MALLOC_FAILED;
					return;
				}
				if (subLogicalGroupSize.data.i * ((1 + j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) {
					VkIf_end(sc);
				}
			}
			VkIf_end(sc);
		}
		if (sc->useDisableThreads) {
			VkIf_end(sc);
		}

		
		
		appendBarrierVkFFT(sc);
		
		if (!sc->stridedSharedLayout) {
			if (rader_stage == 0) {
				if (sc->sharedStrideRaderFFT.data.i > 0) {
					VkMov(sc, &sc->sharedStride, &sc->fftDim);
				}
			}
		}
		
		
		if (sc->useDisableThreads) {
			temp_int.data.i = 0;
			VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
		}

		//local shuffle
		VkContainer* tempID;
		tempID = (VkContainer*)calloc(sc->registers_per_thread * sc->registerBoost, sizeof(VkContainer));
		if (tempID) {
			for (uint64_t i = 0; i < sc->registers_per_thread * sc->registerBoost; i++) {
				VkAllocateContainerFlexible(sc, &tempID[i], 50);
				tempID[i].type = sc->regIDs[0].type;
			}
			for (uint64_t k = 0; k < sc->registerBoost; ++k) {
				uint64_t t = 0;
				if (!raderTranspose) {
					temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
				}
				else {
					VkIf_lt_start(sc, &sc->raderIDx, &subLogicalGroupSize);
				}
				
				if (rader_stage == 0) {
					VkMod(sc, &sc->stageInvocationID, &sc->raderIDx2, stageSize);
					
					VkSub(sc, &sc->blockInvocationID, &sc->raderIDx2, &sc->stageInvocationID);
					
					VkMul(sc, &sc->raderIDx2, &sc->blockInvocationID, stageRadix, 0);
					
					VkAdd(sc, &sc->raderIDx2, &sc->raderIDx2, &sc->stageInvocationID);
					
				}
				if (!sc->stridedSharedLayout) {
					if (rader_stage != (int64_t)sc->currentRaderContainer->numStages - 1) {
						shift = (subLogicalGroupSize.data.i > (locFFTDim % (sc->numSharedBanks / 2))) ? subLogicalGroupSize.data.i - locFFTDim % (sc->numSharedBanks / 2) : 0;
						if (shift <= sc->sharedShiftRaderFFT.data.i) locFFTDimStride = locFFTDim + shift;
					}
					else {
						shift = ((locFFTDim % (sc->numSharedBanks / 2))) ? 0 : 1;
						if (shift <= sc->sharedShiftRaderFFT.data.i) locFFTDimStride = locFFTDim + shift;
					}
				}
				for (int64_t j = 0; j < logicalRegistersPerThread / locStageRadix; j++) {
					if (subLogicalGroupSize.data.i * ((j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) <= locFFTDim) {
						if (subLogicalGroupSize.data.i * ((1 + j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) {
							temp_int.data.i = locFFTDim / locStageRadix - (j + k * logicalRegistersPerThread / locStageRadix) * subLogicalGroupSize.data.i;
							VkIf_lt_start(sc, &sc->raderIDx, &temp_int);							
						}
						temp_int.data.i = j * subLogicalGroupSize.data.i;
						VkAdd(sc, &sc->stageInvocationID, &sc->raderIDx, &temp_int);
						
						VkMov(sc, &sc->blockInvocationID, &sc->stageInvocationID);
						
						temp_int.data.i = locStageSize;
						VkMod(sc, &sc->stageInvocationID, &sc->stageInvocationID, &temp_int);
						
						VkSub(sc, &sc->blockInvocationID, &sc->blockInvocationID, &sc->stageInvocationID);
						
						temp_int.data.i = locStageRadix;
						VkMul(sc, &sc->inoutID, &sc->blockInvocationID, &temp_int, 0);
						
						VkAdd(sc, &sc->inoutID, &sc->inoutID, &sc->stageInvocationID);
					}
					/*sc->tempLen = sprintf(sc->tempStr, "\
	stageInvocationID = (gl_LocalInvocationID.x + %" PRIu64 ") %% (%" PRIu64 ");\n\
	blockInvocationID = (gl_LocalInvocationID.x + %" PRIu64 ") - stageInvocationID;\n\
	inoutID = stageInvocationID + blockInvocationID * %" PRIu64 ";\n", j * logicalGroupSize, stageSize, j * logicalGroupSize, stageRadix);*/

					for (uint64_t i = 0; i < locStageRadix; i++) {
						VkContainer id;
						id.type = 31;
						id.data.i = j + k * logicalRegistersPerThread / locStageRadix + i * logicalStoragePerThread / locStageRadix;
						id.data.i = (id.data.i / logicalRegistersPerThread) * sc->registers_per_thread + id.data.i % logicalRegistersPerThread;
						VkCopyContainer(sc, &tempID[t + k * sc->registers_per_thread], &sc->regIDs[id.data.i]);
						t++;
						if (subLogicalGroupSize.data.i * ((j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) <= locFFTDim) {
							temp_int.data.i = i * locStageSize;
							VkAdd(sc, &sc->combinedID, &sc->inoutID, &temp_int);
							
							if (rader_stage == 0) {
								locFFTDimStride = locFFTDim;
								//last stage - add x0

								int g = sc->currentRaderContainer->generator;
								if (sc->inline_rader_g_pow == 1) {
									temp_int.data.i = stageRadix->data.i - 1;
									VkSub(sc, &sc->tempInt, &temp_int, &sc->combinedID);
									appendConstantToRegisters(sc, &sc->combinedID, &sc->currentRaderContainer->g_powConstantStruct, &sc->tempInt);
								}
								else if (sc->inline_rader_g_pow == 2) {
									temp_int.data.i = stageRadix->data.i - 1 + sc->currentRaderContainer->raderUintLUToffset;
									VkSub(sc, &sc->tempInt, &temp_int, &sc->combinedID);
									appendGlobalToRegisters(sc, &sc->combinedID, &sc->g_powStruct, &sc->tempInt);
								}
								else {
									/*sc->tempLen = sprintf(sc->tempStr, "\
			%s= (%" PRIu64 "-%s);\n\
			%s=1;\n\
			while (%s != 0)\n\
			{\n\
				%s = (%s * %" PRIu64 ") %% %" PRIu64 ";\n\
				%s--;\n\
			}\n", sc->inoutID, stageRadix - 1, sc->combinedID, sc->sdataID, sc->inoutID, sc->combinedID, sc->combinedID, g, stageRadix, sc->inoutID);
									VkAppendLine(sc);
									*/
								}
								if (sc->inverse) {
									VkSub(sc, &sc->tempInt, stageRadix, &sc->combinedID);
									VkMul(sc, &sc->tempInt, &sc->tempInt, stageSize, 0);
								}
								else {
									VkMul(sc, &sc->tempInt, &sc->combinedID, stageSize, 0);
								}
								VkAdd(sc, &sc->sdataID, &sc->raderIDx2, &sc->tempInt);
								
								//normalization is in kernel
								/*sprintf(tempNum, "%.17e%s", 1.0 / locFFTDim, LFending);
								VkMulComplexNumber(sc, sc->regIDs[id], sc->regIDs[id], tempNum);
								*/
								VkAdd(sc, &sc->regIDs[id.data.i], &sc->regIDs[id.data.i], &sc->x0[0]);
							}
							else {
								if (!raderTranspose) {
									temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
									VkAdd(sc, &sc->sdataID, &sc->combinedID, &temp_int);
									temp_int.data.i = locFFTDimStride;
									VkMul(sc, &sc->tempInt, &sc->raderIDx2, &temp_int, 0);

									VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
									
								}
								else {
									temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
									VkMul(sc, &sc->sdataID, &sc->combinedID, &temp_int, 0);
									
									temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
									VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);

									VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->raderIDx2);
									
								}
							}
							if (!sc->stridedSharedLayout) {
								if (0 && (locStageSize <= sc->numSharedBanks / 2) && (locFFTsCombined > sc->numSharedBanks / 2) && (sc->sharedStrideBankConflictFirstStages.data.i != locFFTDim / sc->registerBoost) && ((locFFTDim & (locFFTDim - 1)) == 0) && (locStageSize * locStageRadix != locFFTDim)) {
									if (sc->resolveBankConflictFirstStages == 0) {
										sc->resolveBankConflictFirstStages = 1;
										VkMov(sc, &sc->sharedStride, &sc->sharedStrideBankConflictFirstStages);
									}
									temp_int.data.i = sc->numSharedBanks / 2;
									VkDiv(sc, &sc->tempInt, &sc->sdataID, &temp_int);
									temp_int.data.i = sc->numSharedBanks / 2 + 1;
									VkMul(sc, &sc->tempInt, &sc->tempInt, &temp_int, 0);
									temp_int.data.i = sc->numSharedBanks / 2;
									VkMod(sc, &sc->sdataID, &sc->sdataID, &temp_int);
									VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
								}
								else {
									if (sc->resolveBankConflictFirstStages == 1) {
										sc->resolveBankConflictFirstStages = 0;
										VkMov(sc, &sc->sharedStride, &sc->sharedStrideReadWriteConflict);
									}
								}
							}
							if (sc->stridedSharedLayout) {
								VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);

								VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);

							}
							else {
								if (sc->localSize[1].data.i > 1) {
									VkMul(sc, &sc->combinedID, &sc->gl_LocalInvocationID_y, &sc->sharedStride, 0);

									VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->combinedID);

								}
							}
							//sprintf(sc->sdataID, "sharedStride * gl_LocalInvocationID.y + inoutID + %" PRIu64 "", i * stageSize);
							if ((((sc->actualInverse) && (sc->normalize)) || ((sc->convolutionStep || sc->useBluesteinFFT) && (stageAngle->data.d > 0))) && (rader_stage == 0)) {
								if (normalizationValue.data.i != 1) {
									VkMul(sc, &sc->regIDs[id.data.i], &sc->regIDs[id.data.i], &stageNormalization, 0);
								}								
							}
							appendRegistersToShared(sc, &sc->sdataID, &sc->regIDs[id.data.i]);

							//sc->tempLen = sprintf(sc->tempStr, "	printf(\"%%d %%f %%f \\n \", %s, %s.x, %s.y);\n\n", sc->sdataID, sc->regIDs[id], sc->regIDs[id]);
							//VkAppendLine(sc);
							//
						}
						/*sc->tempLen = sprintf(sc->tempStr, "\
sdata[sharedStride * gl_LocalInvocationID.y + inoutID + %" PRIu64 "] = temp%s%s;\n", i * stageSize, sc->regIDs[id], stageNormalization);*/
					}
					if (subLogicalGroupSize.data.i * ((j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) <= locFFTDim) {
						if (subLogicalGroupSize.data.i * ((1 + j + k * logicalRegistersPerThread / locStageRadix) * locStageRadix) > locFFTDim) {
							VkIf_end(sc);
						}
					}
				}
				VkIf_end(sc);

				for (uint64_t j = logicalRegistersPerThread; j < sc->registers_per_thread; j++) {
					VkCopyContainer(sc, &tempID[t + k * sc->registers_per_thread], &sc->regIDs[t + k * sc->registers_per_thread]);
					t++;
				}
				t = 0;
			}
			for (uint64_t i = 0; i < sc->registers_per_thread * sc->registerBoost; i++) {
				VkCopyContainer(sc, &sc->regIDs[i], &tempID[i]);
			}
			for (uint64_t i = 0; i < sc->registers_per_thread * sc->registerBoost; i++) {
				VkDeallocateContainer(sc, &tempID[i]);
			}
			free(tempID);
			tempID = 0;
		}
		else
			sc->res = VKFFT_ERROR_MALLOC_FAILED;

		if (rader_stage < (int64_t)sc->currentRaderContainer->numStages - 1) {
			switch (locStageRadix) {
			case 2:
				locStageSizeSum += locStageSize;
				break;
			case 3:
				locStageSizeSum += locStageSize * 2;
				break;
			case 4:
				locStageSizeSum += locStageSize * 2;
				break;
			case 5:
				locStageSizeSum += locStageSize * 4;
				break;
			case 6:
				locStageSizeSum += locStageSize * 5;
				break;
			case 7:
				locStageSizeSum += locStageSize * 6;
				break;
			case 8:
				locStageSizeSum += locStageSize * 3;
				break;
			case 9:
				locStageSizeSum += locStageSize * 8;
				break;
			case 10:
				locStageSizeSum += locStageSize * 9;
				break;
			case 11:
				locStageSizeSum += locStageSize * 10;
				break;
			case 12:
				locStageSizeSum += locStageSize * 11;
				break;
			case 13:
				locStageSizeSum += locStageSize * 12;
				break;
			case 14:
				locStageSizeSum += locStageSize * 13;
				break;
			case 15:
				locStageSizeSum += locStageSize * 14;
				break;
			case 16:
				locStageSizeSum += locStageSize * 4;
				break;
			case 32:
				locStageSizeSum += locStageSize * 5;
				break;
			default:
				locStageSizeSum += locStageSize * (locStageRadix);
				break;
			}
		}
		locStageSize *= locStageRadix;
		locStageAngle /= locStageRadix;
		if (sc->useDisableThreads) {
			VkIf_end(sc);
		}

		
		
		appendBarrierVkFFT(sc);
		
		
		
		if (sc->useDisableThreads) {
			temp_int.data.i = 0;
			VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
		}
		
	}

	{
		uint64_t locStageRadix = sc->currentRaderContainer->stageRadix[sc->currentRaderContainer->numStages - 1];
		uint64_t logicalStoragePerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix] * sc->registerBoost;// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread * sc->registerBoost : sc->min_registers_per_thread * sc->registerBoost;
		//uint64_t logicalRegistersPerThread = sc->currentRaderContainer->registers_per_thread_per_radix[locStageRadix];// (sc->registers_per_thread % stageRadix == 0) ? sc->registers_per_thread : sc->min_registers_per_thread;
		uint64_t locFFTDim = sc->currentRaderContainer->containerFFTDim; //different length due to all -1 cutoffs
		//uint64_t locFFTsCombined = sc->currentRaderContainer->containerFFTNum * locFFTDim;
		//uint64_t logicalGroupSize = (uint64_t)ceil(locFFTsCombined / (double)logicalStoragePerThread);
		VkContainer subLogicalGroupSize;
		subLogicalGroupSize.type = 31;
		temp_int.data.i = locFFTDim;
		temp_int1.data.i = logicalStoragePerThread;
		VkDivCeil(sc, &subLogicalGroupSize, &temp_int, &temp_int1);

		if (!raderTranspose) {
			VkMod(sc, &sc->raderIDx, localInvocationID, &subLogicalGroupSize);
			VkDiv(sc, &sc->raderIDx2, localInvocationID, &subLogicalGroupSize);
		}
		else {
			temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
			VkDiv(sc, &sc->raderIDx, localInvocationID, &temp_int);
			VkMod(sc, &sc->raderIDx2, localInvocationID, &temp_int);
		}
		if (!raderTranspose) {
			temp_int.data.i = sc->currentRaderContainer->containerFFTNum;
			VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
		}
		else {
			VkIf_lt_start(sc, &sc->raderIDx, &subLogicalGroupSize);
		}
		
		temp_int.data.i = 0;
		VkIf_eq_start(sc, &sc->raderIDx, &temp_int);

		VkMod(sc, &sc->stageInvocationID, &sc->raderIDx2, stageSize);
		
		VkSub(sc, &sc->blockInvocationID, &sc->raderIDx2, &sc->stageInvocationID);
		
		VkMul(sc, &sc->raderIDx2, &sc->blockInvocationID, stageRadix, 0);
		
		VkAdd(sc, &sc->sdataID, &sc->raderIDx2, &sc->stageInvocationID);

		if (sc->stridedSharedLayout) {
			VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
			VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
		}
		else {
			if (sc->localSize[1].data.i > 1) {
				VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
				VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
			}
		}

		if (((sc->actualInverse) && (sc->normalize)) || ((sc->convolutionStep || sc->useBluesteinFFT) && (stageAngle->data.d > 0))) {
			if (normalizationValue.data.i != 1) {
				VkMul(sc, &sc->x0[1], &sc->x0[1], &stageNormalization, 0);
			}
		}

		appendRegistersToShared(sc, &sc->sdataID, &sc->x0[1]);
		
		VkIf_end(sc);
		VkIf_end(sc);

		if (sc->useDisableThreads) {
			VkIf_end(sc);
		}

		
		
		appendBarrierVkFFT(sc);
		
	}
	return;
}
static inline void appendMultRaderStage(VkFFTSpecializationConstantsLayout* sc, VkContainer* stageSize, VkContainer* stageSizeSum, VkContainer* stageAngle, VkContainer* stageRadix, int stageID) {
	if (sc->res != VKFFT_SUCCESS) return;

	VkContainer temp_complex;
	temp_complex.type = 33;
	VkContainer temp_double;
	temp_double.type = 32;
	VkContainer temp_int;
	temp_int.type = 31;
	VkContainer temp_int1;
	temp_int1.type = 31;
	
	VkContainer stageNormalization;
	stageNormalization.type = 32;
	VkContainer normalizationValue;
	normalizationValue.type = 31;
	normalizationValue.data.i = 1;

	if ((((sc->actualInverse) && (sc->normalize)) || (sc->convolutionStep && (stageAngle->data.d > 0))) && (stageSize->data.i == 1) && (sc->axis_upload_id == 0) && (!(sc->useBluesteinFFT && (stageAngle->data.d < 0)))) {
		if ((sc->performDCT) && (sc->actualInverse)) {
			if (sc->performDCT == 1)
				normalizationValue.data.i = (sc->sourceFFTSize.data.i - 1) * 2;
			else
				normalizationValue.data.i = sc->sourceFFTSize.data.i * 2;
		}
		else
			normalizationValue = sc->sourceFFTSize;
	}
	if (sc->useBluesteinFFT && (stageAngle->data.d > 0) && (stageSize->data.i == 1) && (sc->axis_upload_id == 0)) {
		normalizationValue.data.i *= sc->fft_dim_full.data.i;
	}
	if (normalizationValue.data.i != 1) {
		stageNormalization.data.d = 1.0 / (long double)(normalizationValue.data.i);
	}
	/*char convolutionInverse[10] = "";
	if (sc->convolutionStep) {
		if (stageAngle < 0)
			sprintf(convolutionInverse, ", 0");
		else
			sprintf(convolutionInverse, ", 1");
	}*/
	appendBarrierVkFFT(sc);
	
	
	
	if (sc->useDisableThreads) {
		temp_int.data.i = 0;
		VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
	}

	int64_t num_logical_subgroups = (sc->stridedSharedLayout) ? sc->localSize[1].data.i / ((stageRadix->data.i + 1) / 2) : sc->localSize[0].data.i / ((stageRadix->data.i + 1) / 2);
	VkContainer num_logical_groups;
	num_logical_groups.type = 31;
	temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
	temp_int1.data.i = num_logical_subgroups;
	VkDivCeil(sc, &num_logical_groups, &temp_int, &temp_int1);
	int64_t require_cutoff_check = ((sc->fftDim.data.i == (num_logical_subgroups * num_logical_groups.data.i * stageRadix->data.i))) ? 0 : 1;
	int64_t require_cutoff_check2;
	
	VkContainer* localInvocationID = VKFFT_ZERO_INIT;

	if (sc->stridedSharedLayout) {
		localInvocationID = &sc->gl_LocalInvocationID_y;
	}
	else {
		localInvocationID = &sc->gl_LocalInvocationID_x;
	}

	if (sc->stridedSharedLayout) {
		require_cutoff_check2 = ((sc->localSize[1].data.i % ((stageRadix->data.i + 1) / 2)) == 0) ? 0 : 1;
	}
	else {
		require_cutoff_check2 = ((sc->localSize[0].data.i % ((stageRadix->data.i + 1) / 2)) == 0) ? 0 : 1;
	}
	temp_int.data.i = (stageRadix->data.i + 1) / 2;
	VkMod(sc, &sc->raderIDx, localInvocationID, &temp_int);
	VkDiv(sc, &sc->raderIDx2, localInvocationID, &temp_int);
	
	for (int64_t k = 0; k < sc->registerBoost; k++) {
		for (int64_t j = 0; j < 1; j++) {
			if (stageSize->data.i > 1) {
				if (require_cutoff_check2) {
					if (sc->stridedSharedLayout) {
						temp_int.data.i = sc->localSize[1].data.i - sc->localSize[1].data.i % ((stageRadix->data.i + 1) / 2);
						VkIf_lt_start(sc, &sc->gl_LocalInvocationID_y, &temp_int);
					}
					else {
						temp_int.data.i = sc->localSize[0].data.i - sc->localSize[0].data.i % ((stageRadix->data.i + 1) / 2);
						VkIf_lt_start(sc, &sc->gl_LocalInvocationID_x, &temp_int);
					}
				}
				for (int64_t t = 0; t < num_logical_groups.data.i; t++) {
					if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
						temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
						VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);						
					}
					temp_int.data.i = t * num_logical_subgroups;
					VkAdd(sc, &sc->stageInvocationID, &sc->raderIDx2, &temp_int);
					VkMod(sc, &sc->stageInvocationID, &sc->stageInvocationID, stageSize);

					if (sc->LUT) {
						VkMul(sc, &sc->LUTId, &sc->stageInvocationID, stageRadix, 0);
						VkAdd(sc, &sc->LUTId, &sc->LUTId, stageSizeSum);
						VkAdd(sc, &sc->LUTId, &sc->LUTId, &sc->raderIDx);
						appendGlobalToRegisters(sc, &sc->w, &sc->LUTStruct, &sc->LUTId);
						if (!sc->inverse) {
							VkConjugate(sc, &sc->w, &sc->w);
						}
					}
					else {
						VkMul(sc, &sc->tempInt, &sc->stageInvocationID, &sc->raderIDx, 0);
						temp_double.data.d = stageAngle->data.d * 2.0 / stageRadix->data.d;
						VkMul(sc, &sc->angle, &sc->tempInt, &temp_double, 0);
						VkSinCos(sc, &sc->w, &sc->angle);
					}

					//sc->tempLen = sprintf(sc->tempStr, "	printf(\"%%d %%f %%f \\n \", %s, %s.x, %s.y);\n\n", sc->gl_LocalInvocationID_x, sc->w, sc->w);
					//VkAppendLine(sc);
					//
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
					VkMul(sc, &sc->sdataID, &sc->raderIDx, &temp_int, 0);
					VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->raderIDx2);
					temp_int.data.i = t * num_logical_subgroups;
					VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);
					
					if (sc->stridedSharedLayout) {
						VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
					}
					else {
						if (sc->localSize[1].data.i > 1) {
							VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
						}
					}

					appendSharedToRegisters(sc, &sc->regIDs[0], &sc->sdataID);
					
					VkMul(sc, &sc->temp, &sc->regIDs[0], &sc->w, 0);
					
					appendRegistersToShared(sc, &sc->sdataID, &sc->temp);

					temp_int.data.i = (stageRadix->data.i - 1) / 2;
					VkIf_lt_start(sc, &sc->raderIDx, &temp_int);
					
					temp_int.data.i = t * num_logical_subgroups;
					VkAdd(sc, &sc->stageInvocationID, &sc->raderIDx2, &temp_int);
					VkMod(sc, &sc->stageInvocationID, &sc->stageInvocationID, stageSize);

					if (sc->LUT) {
						VkMul(sc, &sc->LUTId, &sc->stageInvocationID, stageRadix, 0);
						VkAdd(sc, &sc->LUTId, &sc->LUTId, &sc->raderIDx);
						temp_int.data.i = stageSizeSum->data.i + (stageRadix->data.i + 1) / 2;
						VkAdd(sc, &sc->LUTId, &sc->LUTId, &temp_int);
						appendGlobalToRegisters(sc, &sc->w, &sc->LUTStruct, &sc->LUTId);
						if (!sc->inverse) {
							VkConjugate(sc, &sc->w, &sc->w);
						}
					}
					else {
						temp_int.data.i = (stageRadix->data.i + 1) / 2;
						VkAdd(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
						VkMul(sc, &sc->tempInt, &sc->stageInvocationID, &sc->tempInt, 0);
						temp_double.data.d = stageAngle->data.d * 2.0 / stageRadix->data.d;
						VkMul(sc, &sc->angle, &sc->tempInt, &temp_double, 0);
						VkSinCos(sc, &sc->w, &sc->angle);
					}

					//sc->tempLen = sprintf(sc->tempStr, "	printf(\"%%d %%f %%f \\n \", %s, %s.x, %s.y);\n\n", sc->gl_LocalInvocationID_x, sc->w, sc->w);
					//VkAppendLine(sc);
					//
					temp_int.data.i = (stageRadix->data.i + 1) / 2;
					VkAdd(sc, &sc->sdataID, &temp_int, &sc->raderIDx);
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
					VkMul(sc, &sc->sdataID, &sc->sdataID, &temp_int, 0);
					VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->raderIDx2);
					temp_int.data.i = t * num_logical_subgroups;
					VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);
					
					if (sc->stridedSharedLayout) {
						VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
					}
					else {
						if (sc->localSize[1].data.i > 1) {
							VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
						}
					}
					appendSharedToRegisters(sc, &sc->regIDs[0], &sc->sdataID);

					VkMul(sc, &sc->temp, &sc->regIDs[0], &sc->w, 0);
					
					appendRegistersToShared(sc, &sc->sdataID, &sc->temp);

					
					VkIf_end(sc);
					
					if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
						VkIf_end(sc);
					}
				}
				if (require_cutoff_check2) {
					VkIf_end(sc);
				}
				if (sc->useDisableThreads) {
					VkIf_end(sc);
				}
				
				
				appendBarrierVkFFT(sc);
				
				
				
				if (sc->useDisableThreads) {
					temp_int.data.i = 0;
					VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
				}
			}
			if (require_cutoff_check2) {
				if (sc->stridedSharedLayout) {
					temp_int.data.i = sc->localSize[1].data.i - sc->localSize[1].data.i % ((stageRadix->data.i + 1) / 2);
					VkIf_lt_start(sc, &sc->gl_LocalInvocationID_y, &temp_int);
				}
				else {
					temp_int.data.i = sc->localSize[0].data.i - sc->localSize[0].data.i % ((stageRadix->data.i + 1) / 2);
					VkIf_lt_start(sc, &sc->gl_LocalInvocationID_x, &temp_int);
				}
			}
			//save x0
			for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
				}
				if (sc->stridedSharedLayout) {
					if (sc->localSize[0].data.i > 1) {
						temp_int.data.i = t * num_logical_subgroups;
						VkAdd(sc, &sc->sdataID, &sc->raderIDx2, &temp_int);
						VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
					}
					else {
						temp_int.data.i = t * num_logical_subgroups;
						VkAdd(sc, &sc->sdataID, &sc->raderIDx2, &temp_int);
					}
				}
				else {
					if (sc->localSize[1].data.i > 1) {
						VkMul(sc, &sc->sdataID, &sc->gl_LocalInvocationID_y, &sc->sharedStride, 0);
						temp_int.data.i = t * num_logical_subgroups;
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->raderIDx2);
					}
					else {
						temp_int.data.i = t * num_logical_subgroups;
						VkAdd(sc, &sc->sdataID, &sc->raderIDx2, &temp_int);
					}
				}
				appendSharedToRegisters(sc, &sc->x0[t], &sc->sdataID);
				
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					VkIf_end(sc);
				}
			}
			//generator index + shuffle 
			temp_int.data.i = 0;
			VkIf_gt_start(sc, &sc->raderIDx, &temp_int);
			
			int g = sc->currentRaderContainer->generator;
			if (sc->inline_rader_g_pow == 1) {
				temp_int.data.i = 1;
				VkSub(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
				appendConstantToRegisters(sc, &sc->sdataID, &sc->currentRaderContainer->g_powConstantStruct, &sc->tempInt);
			}
			else if (sc->inline_rader_g_pow == 2) {
				temp_int.data.i = sc->currentRaderContainer->raderUintLUToffset - 1;
				VkAdd(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
				appendGlobalToRegisters(sc, &sc->sdataID, &sc->g_powStruct, &sc->tempInt);
			}
			else {
				/*sc->tempLen = sprintf(sc->tempStr, "\
			%s= (%s-1);\n\
			%s=1;\n\
			while (%s != 0)\n\
			{\n\
				%s = (%s * %" PRIu64 ") %% %" PRIu64 ";\n\
				%s--;\n\
			}\n", sc->inoutID, sc->raderIDx, sc->sdataID, sc->inoutID, sc->sdataID, sc->sdataID, g, stageRadix, sc->inoutID);
				VkAppendLine(sc);
				*/
			}
			for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
					
				}
				temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
				VkMul(sc, &sc->combinedID, &sc->sdataID, &temp_int, 0);
				VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->raderIDx2);
				temp_int.data.i = t * num_logical_subgroups;
				VkAdd(sc, &sc->combinedID, &sc->combinedID, &temp_int);
				
				if (sc->stridedSharedLayout) {
					VkMul(sc, &sc->combinedID, &sc->combinedID, &sc->sharedStride, 0);
					VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->gl_LocalInvocationID_x);
				}
				else {
					if (sc->localSize[1].data.i > 1) {
						VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
						VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->tempInt);
					}
				}

				appendSharedToRegisters(sc, &sc->regIDs[t * 2], &sc->combinedID);

				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					VkIf_end(sc);
				}
			}
			if (sc->inline_rader_g_pow == 1) {
				temp_int.data.i = (stageRadix->data.i - 1) / 2 - 1;
				VkAdd(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
				appendConstantToRegisters(sc, &sc->sdataID, &sc->currentRaderContainer->g_powConstantStruct, &sc->tempInt);
			}
			else if (sc->inline_rader_g_pow == 2) {
				temp_int.data.i = (stageRadix->data.i - 1) / 2 - 1 + sc->currentRaderContainer->raderUintLUToffset;
				VkAdd(sc, &sc->tempInt, &sc->raderIDx, &temp_int);
				appendGlobalToRegisters(sc, &sc->sdataID, &sc->g_powStruct, &sc->tempInt);
			}
			else {
				/*sc->tempLen = sprintf(sc->tempStr, "\
			%s= (%s+ %" PRIu64 ");\n\
			%s=1;\n\
			while (%s != 0)\n\
			{\n\
				%s = (%s * %" PRIu64 ") %% %" PRIu64 ";\n\
				%s--;\n\
			}\n", sc->inoutID, sc->raderIDx, (stageRadix - 1) / 2 - 1, sc->sdataID, sc->inoutID, sc->sdataID, sc->sdataID, g, stageRadix, sc->inoutID);
				VkAppendLine(sc);
				*/
			}

			for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
				}
				temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
				VkMul(sc, &sc->combinedID, &sc->sdataID, &temp_int, 0);
				VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->raderIDx2);
				temp_int.data.i = t * num_logical_subgroups;
				VkAdd(sc, &sc->combinedID, &sc->combinedID, &temp_int);

				
				if (sc->stridedSharedLayout) {
					VkMul(sc, &sc->combinedID, &sc->combinedID, &sc->sharedStride, 0);
					VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->gl_LocalInvocationID_x);
				}
				else {
					if (sc->localSize[1].data.i > 1) {
						VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
						VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->tempInt);
					}
				}
				appendSharedToRegisters(sc, &sc->regIDs[t * 2 + 1], &sc->combinedID);


				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					VkIf_end(sc);
				}
			}
			VkIf_end(sc);

			if (require_cutoff_check2) {
				VkIf_end(sc);
			}

			if (sc->useDisableThreads) {
				VkIf_end(sc);
			}

			
			
			appendBarrierVkFFT(sc);
			
			//load deconv kernel
			if (!sc->inline_rader_kernel) {
				for (uint64_t t = 0; t < (uint64_t)ceil((stageRadix->data.i - 1) / ((long double)(sc->localSize[0].data.i * sc->localSize[1].data.i))); t++) {
					VkMul(sc, &sc->combinedID, &sc->gl_LocalInvocationID_y, &sc->localSize[0], 0);
					VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->gl_LocalInvocationID_x);
					temp_int.data.i = t * sc->localSize[0].data.i * sc->localSize[1].data.i;
					VkAdd(sc, &sc->combinedID, &sc->combinedID, &temp_int);
					
					if (t == ((uint64_t)ceil((stageRadix->data.i - 1) / ((double)(sc->localSize[0].data.i * sc->localSize[1].data.i))) - 1)) {
						temp_int.data.i = stageRadix->data.i - 1;
						VkIf_lt_start(sc, &sc->combinedID, &temp_int);
					}
					if (sc->LUT) {
						temp_int.data.i = sc->currentRaderContainer->RaderKernelOffsetLUT;
						VkAdd(sc, &sc->tempInt, &sc->combinedID, &temp_int);
						appendGlobalToRegisters(sc, &sc->w, &sc->LUTStruct, &sc->tempInt);
						
						if (sc->inverse) {
							VkConjugate(sc, &sc->w, &sc->w);
						}

						VkAdd(sc, &sc->tempInt, &sc->combinedID, &sc->RaderKernelOffsetShared[stageID]);
						appendRegistersToShared(sc, &sc->tempInt, &sc->w);
					}
					else {
						if (sc->inline_rader_g_pow == 1) {
							temp_int.data.i = stageRadix->data.i - 1;
							VkSub(sc, &sc->tempInt, &temp_int, &sc->combinedID);
							appendConstantToRegisters(sc, &sc->sdataID, &sc->currentRaderContainer->g_powConstantStruct, &sc->tempInt);
							
						}
						else if (sc->inline_rader_g_pow == 2) {
							temp_int.data.i = stageRadix->data.i - 1 + sc->currentRaderContainer->raderUintLUToffset;
							VkSub(sc, &sc->tempInt, &temp_int, &sc->combinedID);
							appendGlobalToRegisters(sc, &sc->sdataID, &sc->g_powStruct, &sc->tempInt);
						}
						else {
							/*sc->tempLen = sprintf(sc->tempStr, "\
			%s= (%" PRIu64 " - %s);\n\
			%s=1;\n\
			while (%s != 0)\n\
			{\n\
				%s = (%s * %" PRIu64 ") %% %" PRIu64 ";\n\
				%s--;\n\
			}\n", sc->inoutID, stageRadix - 1, sc->combinedID, sc->sdataID, sc->inoutID, sc->sdataID, sc->sdataID, g, stageRadix, sc->inoutID);
							VkAppendLine(sc);
							*/
						}
						temp_double.data.d = 2.0 * sc->double_PI / stageRadix->data.i;
						VkMul(sc, &sc->tempFloat, &temp_double, &sc->sdataID, 0);
						VkSinCos(sc, &sc->w, &sc->tempFloat);
						if (!sc->inverse) {
							VkConjugate(sc, &sc->w, &sc->w);
						}

						VkAdd(sc, &sc->tempInt, &sc->combinedID, &sc->RaderKernelOffsetShared[stageID]);
						appendRegistersToShared(sc, &sc->tempInt, &sc->w);
					}
					if (t == ((uint64_t)ceil((stageRadix->data.i - 1) / ((long double)(sc->localSize[0].data.i * sc->localSize[1].data.i))) - 1)) {
						VkIf_end(sc);
					}
				}
			}
			
			
			if (sc->useDisableThreads) {
				temp_int.data.i = 0;
				VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
			}

			if (require_cutoff_check2) {
				if (sc->stridedSharedLayout) {
					temp_int.data.i = sc->localSize[1].data.i - sc->localSize[1].data.i % ((stageRadix->data.i + 1) / 2);
					VkIf_lt_start(sc, &sc->gl_LocalInvocationID_y, &temp_int);
				}
				else {
					temp_int.data.i = sc->localSize[0].data.i - sc->localSize[0].data.i % ((stageRadix->data.i + 1) / 2);
					VkIf_lt_start(sc, &sc->gl_LocalInvocationID_x, &temp_int);
				}
			}
			//x0 is ready

			//no subgroups
			/* {
				sc->tempLen = sprintf(sc->tempStr, "\
		if(%s==0){\n", sc->gl_LocalInvocationID_x);
				VkAppendLine(sc);
				
				sc->tempLen = sprintf(sc->tempStr, "\
		%s.x = 0;\n\
		%s.y = 0;\n", sc->regIDs[0], sc->regIDs[0]);
				VkAppendLine(sc);
				
				sc->tempLen = sprintf(sc->tempStr, "\
		%s = 0;\n", sc->combinedID);
				VkAppendLine(sc);
				

				if (sc->localSize[1] > 1) {
					sc->tempLen = sprintf(sc->tempStr, "\
		%s = %s + sharedStride * (%s);\n", sc->sdataID, sc->combinedID, sc->gl_LocalInvocationID_y);
					VkAppendLine(sc);
					
					sc->tempLen = sprintf(sc->tempStr, "\
		while(%s<%" PRIu64 "){\n\
		%s.x += sdata[%s].x;\n\
		%s.y += sdata[%s].y;\n\
		%s++; %s++;}\n", sc->combinedID, stageRadix, sc->regIDs[0], sc->sdataID, sc->regIDs[0], sc->sdataID, sc->combinedID, sc->sdataID);
					VkAppendLine(sc);
					
				}
				else {
					sc->tempLen = sprintf(sc->tempStr, "\
		while(%s<%" PRIu64 "){\n\
		%s.x += sdata[%s].x;\n\
		%s.y += sdata[%s].y;\n\
		%s++;}\n", sc->combinedID, stageRadix, sc->regIDs[0], sc->combinedID, sc->regIDs[0], sc->combinedID, sc->combinedID);
					VkAppendLine(sc);
					
				}
				sc->tempLen = sprintf(sc->tempStr, "\
		%s = 0;\n", sc->sdataID);
				VkAppendLine(sc);
				

				if (sc->localSize[1] > 1) {
					sc->tempLen = sprintf(sc->tempStr, "\
		%s = %s + sharedStride * (%s);\n", sc->sdataID, sc->sdataID, sc->gl_LocalInvocationID_y);
					VkAppendLine(sc);
					
				}
				sc->tempLen = sprintf(sc->tempStr, "\
		sdata[%s] = %s;\n", sc->sdataID, sc->regIDs[0]);
				VkAppendLine(sc);
				
				sc->tempLen = sprintf(sc->tempStr, "\
		}\n");
				VkAppendLine(sc);
				
			}*/
			//subgroups
			/* {
				uint64_t numGroupsQuant = ((((sc->localSize[0] * sc->localSize[1] * sc->localSize[2]) % sc->warpSize) == 0) || (sc->numSubgroups == 1)) ? sc->numSubgroups : sc->numSubgroups - 1;
				if (numGroupsQuant != sc->numSubgroups) {
					sc->tempLen = sprintf(sc->tempStr, "\
		if(%s<%" PRIu64 "){\n", sc->gl_SubgroupID, numGroupsQuant);
					VkAppendLine(sc);
					
				}
				for (uint64_t t = 0; t < (uint64_t)ceil(sc->localSize[1] / (double)numGroupsQuant); t++) {
					sc->tempLen = sprintf(sc->tempStr, "\
		%s.x = 0;\n", sc->regIDs[0]);
					VkAppendLine(sc);
					
					sc->tempLen = sprintf(sc->tempStr, "\
		%s.y = 0;\n", sc->regIDs[0]);
					VkAppendLine(sc);
					
					uint64_t quant = (sc->warpSize < (sc->localSize[0] * sc->localSize[1] * sc->localSize[2])) ? sc->warpSize : (sc->localSize[0] * sc->localSize[1] * sc->localSize[2]);
					for (uint64_t t2 = 0; t2 < (uint64_t)ceil(stageRadix / (double)quant); t2++) {
						if ((t == (uint64_t)ceil(sc->localSize[1] / (double)numGroupsQuant) - 1) && (sc->localSize[1] > 1) && ((sc->localSize[1] % numGroupsQuant) != 0)) {
							sc->tempLen = sprintf(sc->tempStr, "\
		if(%s<%" PRIu64 "){\n", sc->gl_SubgroupID, sc->localSize[1] % numGroupsQuant);
							VkAppendLine(sc);
							
						}
						if (t2 == (uint64_t)ceil(stageRadix / (double)quant) - 1) {
							sc->tempLen = sprintf(sc->tempStr, "\
		if(%s<%" PRIu64 "){\n", sc->gl_SubgroupInvocationID, stageRadix % quant);
							VkAppendLine(sc);
							
						}
						sc->tempLen = sprintf(sc->tempStr, "\
		%s = (%s+%" PRIu64 ") * %" PRIu64 ";\n", sc->sdataID, sc->gl_SubgroupInvocationID, t2 * quant, sc->fftDim / stageRadix);
						VkAppendLine(sc);
						

						if (sc->localSize[1] > 1) {
							sc->tempLen = sprintf(sc->tempStr, "\
		%s = %s + sharedStride * (%s+%" PRIu64 ");\n", sc->sdataID, sc->sdataID, sc->gl_SubgroupID, t * numGroupsQuant);
							VkAppendLine(sc);
							
						}
						sc->tempLen = sprintf(sc->tempStr, "\
		%s = sdata[%s];\n", sc->regIDs[1], sc->sdataID);
						VkAppendLine(sc);
						
						VkAddComplex(sc, sc->regIDs[0], sc->regIDs[0], sc->regIDs[1]);
						
						if (t2 == (uint64_t)ceil(stageRadix / (double)quant) - 1) {
							sc->tempLen = sprintf(sc->tempStr, "\
		}\n");
							VkAppendLine(sc);
							
						}
						if ((t == (uint64_t)ceil(sc->localSize[1] / (double)numGroupsQuant) - 1) && (sc->localSize[1] > 1) && ((sc->localSize[1] % numGroupsQuant) != 0)) {
							sc->tempLen = sprintf(sc->tempStr, "\
		}\n");
							VkAppendLine(sc);
							
						}
					}

					VkSubgroupAdd(sc, sc->regIDs[0], sc->regIDs[0], 1);
					

					if ((t == (uint64_t)ceil(sc->localSize[1] / (double)numGroupsQuant) - 1) && (sc->localSize[1] > 1) && ((sc->localSize[1] % numGroupsQuant) != 0)) {
						sc->tempLen = sprintf(sc->tempStr, "\
		if(%s<%" PRIu64 "){\n", sc->gl_SubgroupID, sc->localSize[1] % numGroupsQuant);
						VkAppendLine(sc);
						
					}
					sc->tempLen = sprintf(sc->tempStr, "\
		if(%s==0){\n", sc->gl_SubgroupInvocationID);
					VkAppendLine(sc);
					
					sc->tempLen = sprintf(sc->tempStr, "\
		%s = 0;\n", sc->sdataID);
					VkAppendLine(sc);
					

					if (sc->localSize[1] > 1) {
						sc->tempLen = sprintf(sc->tempStr, "\
		%s = %s + sharedStride * (%s+%" PRIu64 ");\n", sc->sdataID, sc->sdataID, sc->gl_SubgroupID, t * numGroupsQuant);
						VkAppendLine(sc);
						
					}
					sc->tempLen = sprintf(sc->tempStr, "\
		sdata[%s] = %s;\n", sc->sdataID, sc->regIDs[0]);
					VkAppendLine(sc);
					
					sc->tempLen = sprintf(sc->tempStr, "\
		}\n");
					VkAppendLine(sc);
					
					if ((t == (uint64_t)ceil(sc->localSize[1] / (double)numGroupsQuant) - 1) && (sc->localSize[1] > 1) && ((sc->localSize[1] % numGroupsQuant) != 0)) {
						sc->tempLen = sprintf(sc->tempStr, "\
		}\n");
						VkAppendLine(sc);
						
					}
				}
				if (numGroupsQuant != sc->numSubgroups) {
					sc->tempLen = sprintf(sc->tempStr, "\
		}\n");
					VkAppendLine(sc);
					
				}
			}*/

			temp_int.data.i = 0;
			VkIf_gt_start(sc, &sc->raderIDx, &temp_int);
			
			for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
				}
				temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i;
				VkMul(sc, &sc->sdataID, &sc->raderIDx, &temp_int, 0);
				VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->raderIDx2);
				temp_int.data.i = t * num_logical_subgroups;
				VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);


				if (sc->stridedSharedLayout) {
					VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
					VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);

					temp_int.data.i = (stageRadix->data.i - 1) / 2 * sc->fftDim.data.i / stageRadix->data.i;
					VkMul(sc, &sc->combinedID, &temp_int, &sc->sharedStride, 0);
					VkAdd(sc, &sc->combinedID, &sc->sdataID, &sc->combinedID);
				}
				else {
					if (sc->localSize[1].data.i > 1) {
						VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
					}
					temp_int.data.i = (stageRadix->data.i - 1) / 2 * sc->fftDim.data.i / stageRadix->data.i;
					VkAdd(sc, &sc->combinedID, &sc->sdataID, &temp_int);
				}
				
				VkSub_x(sc, &sc->temp, &sc->regIDs[2 * t], &sc->regIDs[2 * t + 1]);
				
				VkAdd_x(sc, &sc->regIDs[2 * t], &sc->regIDs[2 * t], &sc->regIDs[2 * t + 1]);
				
				VkAdd_y(sc, &sc->temp, &sc->regIDs[2 * t], &sc->regIDs[2 * t + 1]);

				VkSub_y(sc, &sc->regIDs[2 * t], &sc->regIDs[2 * t], &sc->regIDs[2 * t + 1]);

				appendRegistersToShared(sc, &sc->sdataID, &sc->regIDs[2 * t]);
				appendRegistersToShared(sc, &sc->combinedID, &sc->temp);
			
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					VkIf_end(sc);
				}
			}
			//sc->tempLen = sprintf(sc->tempStr, "	printf(\"%%d %%f %%f %%f %%f \\n \", %s, %s.x, %s.y, %s.x, %s.y);\n\n", sc->gl_LocalInvocationID_x, sc->regIDs[0], sc->regIDs[0], sc->temp, sc->temp);
			//VkAppendLine(sc);
			//
			VkIf_end(sc);

			if (require_cutoff_check2) {
				VkIf_end(sc);
			}

			if (sc->useDisableThreads) {
				VkIf_end(sc);
			}

			
			
			appendBarrierVkFFT(sc);
			
			
			
			if (sc->useDisableThreads) {
				temp_int.data.i = 0;
				VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
			}

			if (require_cutoff_check2) {
				if (sc->stridedSharedLayout) {
					temp_int.data.i = sc->localSize[1].data.i - sc->localSize[1].data.i % ((stageRadix->data.i + 1) / 2);
					VkIf_lt_start(sc, &sc->gl_LocalInvocationID_y, &temp_int);
				}
				else {
					temp_int.data.i = sc->localSize[0].data.i - sc->localSize[0].data.i % ((stageRadix->data.i + 1) / 2);
					VkIf_lt_start(sc, &sc->gl_LocalInvocationID_x, &temp_int);
				}
			}
			temp_int.data.i = (stageRadix->data.i + 1) / 2;
			VkIf_lt_start(sc, &sc->raderIDx, &temp_int);
			
			for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
				VkSetToZero(sc, &sc->regIDs[2 * t + 1]);
			}
			temp_int.data.i = (stageRadix->data.i - 1) / 2;
			VkIf_eq_start(sc, &sc->raderIDx, &temp_int);
			
			temp_complex.data.c[0] = 1;
			temp_complex.data.c[1] = 0;

			VkMov(sc, &sc->w, &temp_complex);
			
			VkIf_end(sc);
			
			for (uint64_t i = 0; i < (stageRadix->data.i - 1) / 2; i++) {

				temp_int.data.i = (stageRadix->data.i - 1) / 2;
				VkIf_lt_start(sc, &sc->raderIDx, &temp_int);

				temp_int.data.i = stageRadix->data.i - 1 - i;
				VkAdd(sc, &sc->sdataID, &sc->raderIDx, &temp_int);
				temp_int.data.i = stageRadix->data.i - 1;
				VkMod(sc, &sc->sdataID, &sc->sdataID, &temp_int);
				
				if (sc->inline_rader_kernel) {
					appendConstantToRegisters_x(sc, &sc->w, &sc->currentRaderContainer->r_rader_kernelConstantStruct, &sc->sdataID);
					appendConstantToRegisters_y(sc, &sc->w, &sc->currentRaderContainer->i_rader_kernelConstantStruct, &sc->sdataID);
				}
				else {
					VkAdd(sc, &sc->tempInt, &sc->sdataID, &sc->RaderKernelOffsetShared[stageID]);
					appendSharedToRegisters(sc, &sc->w, &sc->tempInt);
				}
				VkIf_end(sc);
				

				for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
#if(VKFFT_BACKEND != 2) //AMD compiler fix
					if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
						temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
						VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
					}
#endif
					temp_int.data.i = t * num_logical_subgroups + (1 + i) * sc->fftDim.data.i / stageRadix->data.i;
					VkAdd(sc, &sc->sdataID, &sc->raderIDx2, &temp_int);
					
					if (sc->stridedSharedLayout) {
						VkMul(sc, &sc->sdataID, &sc->sdataID, &sc->sharedStride, 0);
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->gl_LocalInvocationID_x);
					}
					else {
						if (sc->localSize[1].data.i > 1) {
							VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
							VkAdd(sc, &sc->sdataID, &sc->sdataID, &sc->tempInt);
						}
					}

					appendSharedToRegisters(sc, &sc->regIDs[0], &sc->sdataID);
					
					if (sc->stridedSharedLayout) {
						temp_int.data.i = sc->sharedStride.data.i * (stageRadix->data.i - 1) / 2 * sc->fftDim.data.i / stageRadix->data.i;
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);
					}
					else {
						temp_int.data.i = (stageRadix->data.i - 1) / 2 * sc->fftDim.data.i / stageRadix->data.i;
						VkAdd(sc, &sc->sdataID, &sc->sdataID, &temp_int);
					}
					appendSharedToRegisters(sc, &sc->temp, &sc->sdataID);
					
#if(VKFFT_BACKEND == 2) //AMD compiler fix
					if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
						temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
						VkIf_ge_start(sc, &sc->raderIDx2, &temp_int);
						VkSetToZero(sc, &sc->temp);
						VkSetToZero(sc, &sc->regIDs[0]);
						VkIf_end(sc);
					}
#endif
					VkFMA3(sc, &sc->x0[t], &sc->regIDs[2 * t + 1], &sc->regIDs[0], &sc->w, &sc->temp);
					
#if(VKFFT_BACKEND != 2) //AMD compiler fix
					if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
						VkIf_end(sc);
					}
#endif
#if(VKFFT_BACKEND == 2) //AMD compiler fix
					if ((uint64_t)ceil((sc->localSize[0].data.i * sc->localSize[1].data.i) / ((long double)sc->warpSize)) * sc->warpSize * (1 + sc->registers_per_thread + sc->usedLocRegs) > 2048) {
						VkIf_end(sc);

						if (require_cutoff_check2) {
							VkIf_end(sc);
						}

						if (sc->useDisableThreads) {
							VkIf_end(sc);
						}

						
						
						appendBarrierVkFFT(sc);
						
						
						
						if (sc->useDisableThreads) {
							temp_int.data.i = 0;
							VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
						}

						if (require_cutoff_check2) {
							if (sc->stridedSharedLayout) {
								temp_int.data.i = sc->localSize[1].data.i - sc->localSize[1].data.i % ((stageRadix->data.i + 1) / 2);
								VkIf_lt_start(sc, &sc->gl_LocalInvocationID_y, &temp_int);
							}
							else {
								temp_int.data.i = sc->localSize[0].data.i - sc->localSize[0].data.i % ((stageRadix->data.i + 1) / 2);
								VkIf_lt_start(sc, &sc->gl_LocalInvocationID_x, &temp_int);
							}
						}

						temp_int.data.i = (stageRadix->data.i + 1) / 2;
						VkIf_lt_start(sc, &sc->raderIDx, &temp_int);

					}
#endif
				}
#if(VKFFT_BACKEND == 2) //AMD compiler fix
				if ((uint64_t)ceil((sc->localSize[0].data.i * sc->localSize[1].data.i) / ((double)sc->warpSize)) * sc->warpSize * (1 + sc->registers_per_thread + sc->usedLocRegs) <= 2048) {
					VkIf_end(sc);

					if (require_cutoff_check2) {
						VkIf_end(sc);
					}

					if (sc->useDisableThreads) {
						VkIf_end(sc);
					}
					
					
					appendBarrierVkFFT(sc);
					
					
					
					if (sc->useDisableThreads) {
						temp_int.data.i = 0;
						VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
					}

					if (require_cutoff_check2) {
						if (sc->stridedSharedLayout) {
							temp_int.data.i = sc->localSize[1].data.i - sc->localSize[1].data.i % ((stageRadix->data.i + 1) / 2);
							VkIf_lt_start(sc, &sc->gl_LocalInvocationID_y, &temp_int);
						}
						else {
							temp_int.data.i = sc->localSize[0].data.i - sc->localSize[0].data.i % ((stageRadix->data.i + 1) / 2);
							VkIf_lt_start(sc, &sc->gl_LocalInvocationID_x, &temp_int);
						}
					}

					temp_int.data.i = (stageRadix->data.i + 1) / 2;
					VkIf_lt_start(sc, &sc->raderIDx, &temp_int);

				}
#endif
			}
			for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
				}
				
				VkSub_x(sc, &sc->regIDs[2 * t], &sc->x0[t], &sc->regIDs[2 * t + 1]);

				VkAdd_y(sc, &sc->regIDs[2 * t], &sc->x0[t], &sc->regIDs[2 * t + 1]);

				VkAdd_x(sc, &sc->regIDs[2 * t + 1], &sc->x0[t], &sc->regIDs[2 * t + 1]);

				VkSub_y(sc, &sc->regIDs[2 * t + 1], &sc->x0[t], &sc->regIDs[2 * t + 1]);


				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					VkIf_end(sc);
				}
			}
			VkIf_end(sc);

			if (require_cutoff_check2) {
				VkIf_end(sc);
			}

			if (sc->useDisableThreads) {
				VkIf_end(sc);
			}

			
			
			appendBarrierVkFFT(sc);
			
			
			
			if (sc->useDisableThreads) {
				temp_int.data.i = 0;
				VkIf_gt_start(sc, &sc->disableThreads, &temp_int);
			}

			if (require_cutoff_check2) {
				if (sc->stridedSharedLayout) {
					temp_int.data.i = sc->localSize[1].data.i - sc->localSize[1].data.i % ((stageRadix->data.i + 1) / 2);
					VkIf_lt_start(sc, &sc->gl_LocalInvocationID_y, &temp_int);
				}
				else {
					temp_int.data.i = sc->localSize[0].data.i - sc->localSize[0].data.i % ((stageRadix->data.i + 1) / 2);
					VkIf_lt_start(sc, &sc->gl_LocalInvocationID_x, &temp_int);
				}
			}

			temp_int.data.i = (stageRadix->data.i - 1) / 2;
			VkIf_lt_start(sc, &sc->raderIDx, &temp_int);
			//sc->tempLen = sprintf(sc->tempStr, "	printf(\"%%d %%f %%f \\n \", %s, %s.x, %s.y);\n\n", sc->gl_LocalInvocationID_x, sc->regIDs[1], sc->regIDs[1]);
			//VkAppendLine(sc);
			//
			if (sc->inline_rader_g_pow == 1) {
				temp_int.data.i = stageRadix->data.i - 1;
				VkSub(sc, &sc->tempInt, &temp_int, &sc->raderIDx);
				appendConstantToRegisters(sc, &sc->sdataID, &sc->currentRaderContainer->g_powConstantStruct, &sc->tempInt);

			}
			else if (sc->inline_rader_g_pow == 2) {
				temp_int.data.i = stageRadix->data.i - 1 + sc->currentRaderContainer->raderUintLUToffset;
				VkSub(sc, &sc->tempInt, &temp_int, &sc->raderIDx);
				appendGlobalToRegisters(sc, &sc->sdataID, &sc->g_powStruct, &sc->tempInt);
			}
			else {
				/*sc->tempLen = sprintf(sc->tempStr, "\
			%s= (%" PRIu64 "-%s);\n\
			%s=1;\n\
			while (%s != 0)\n\
			{\n\
				%s = (%s * %" PRIu64 ") %% %" PRIu64 ";\n\
				%s--;\n\
			}\n", sc->inoutID, stageRadix - 1, sc->raderIDx, sc->sdataID, sc->inoutID, sc->sdataID, sc->sdataID, g, stageRadix, sc->inoutID);
				VkAppendLine(sc);
				*/
			}
			VkIf_else(sc);
			
			VkSetToZero(sc, &sc->sdataID);
			
			VkIf_end(sc);
			
			for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
				}

				temp_int.data.i = t * num_logical_subgroups;
				VkAdd(sc, &sc->combinedID, &sc->raderIDx2, &temp_int);
				
				VkMod(sc, &sc->stageInvocationID, &sc->combinedID, stageSize);
				
				VkSub(sc, &sc->blockInvocationID, &sc->combinedID, &sc->stageInvocationID);
				
				VkMul(sc, &sc->inoutID, &sc->blockInvocationID, stageRadix, 0);
				

				VkMul(sc, &sc->combinedID, &sc->sdataID, stageSize, 0);
				VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->inoutID);
				VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->stageInvocationID);
				
				if (sc->stridedSharedLayout) {
					VkMul(sc, &sc->combinedID, &sc->combinedID, &sc->sharedStride, 0);
					VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->gl_LocalInvocationID_x);
				}
				else {
					if (sc->localSize[1].data.i > 1) {
						VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
						VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->tempInt);
					}
				}

				if (((sc->actualInverse) && (sc->normalize)) || ((sc->convolutionStep || sc->useBluesteinFFT) && (stageAngle->data.d > 0))) {
					if (normalizationValue.data.i != 1) {
						VkMul(sc, &sc->regIDs[2 * t], &sc->regIDs[2 * t], &stageNormalization, 0);
					}
				}
				appendRegistersToShared(sc, &sc->combinedID, &sc->regIDs[2 * t]);
				
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					VkIf_end(sc);
				}
			}
			temp_int.data.i = (stageRadix->data.i - 1) / 2;
			VkIf_lt_start(sc, &sc->raderIDx, &temp_int);
			
			if (sc->inline_rader_g_pow == 1) {
				temp_int.data.i = (stageRadix->data.i - 1) / 2;
				VkSub(sc, &sc->tempInt, &temp_int, &sc->raderIDx);
				appendConstantToRegisters(sc, &sc->sdataID, &sc->currentRaderContainer->g_powConstantStruct, &sc->tempInt);
			}
			else if (sc->inline_rader_g_pow == 2) {
				temp_int.data.i = (stageRadix->data.i - 1) / 2 + sc->currentRaderContainer->raderUintLUToffset;
				VkSub(sc, &sc->tempInt, &temp_int, &sc->raderIDx);
				appendGlobalToRegisters(sc, &sc->sdataID, &sc->g_powStruct, &sc->tempInt);
			}
			else {
				/*sc->tempLen = sprintf(sc->tempStr, "\
			%s= (%" PRIu64 "-%s);\n\
			%s=1;\n\
			while (%s != 0)\n\
			{\n\
				%s = (%s * %" PRIu64 ") %% %" PRIu64 ";\n\
				%s--;\n\
			}\n", sc->inoutID, (stageRadix - 1) / 2, sc->raderIDx, sc->sdataID, sc->inoutID, sc->sdataID, sc->sdataID, g, stageRadix, sc->inoutID);
				VkAppendLine(sc);
				*/
			}
			for (uint64_t t = 0; t < num_logical_groups.data.i; t++) {
				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					temp_int.data.i = sc->fftDim.data.i / stageRadix->data.i - t * num_logical_subgroups;
					VkIf_lt_start(sc, &sc->raderIDx2, &temp_int);
				}

				temp_int.data.i = t * num_logical_subgroups;
				VkAdd(sc, &sc->combinedID, &sc->raderIDx2, &temp_int);
				
				VkMod(sc, &sc->stageInvocationID, &sc->combinedID, stageSize);
				
				VkSub(sc, &sc->blockInvocationID, &sc->combinedID, &sc->stageInvocationID);
				
				VkMul(sc, &sc->inoutID, &sc->blockInvocationID, stageRadix, 0);
				
				VkMul(sc, &sc->combinedID, &sc->sdataID, stageSize, 0);
				VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->inoutID);
				VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->stageInvocationID);

				if (sc->stridedSharedLayout) {
					VkMul(sc, &sc->combinedID, &sc->combinedID, &sc->sharedStride, 0);
					VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->gl_LocalInvocationID_x);
				}
				else {
					if (sc->localSize[1].data.i > 1) {
						VkMul(sc, &sc->tempInt, &sc->sharedStride, &sc->gl_LocalInvocationID_y, 0);
						VkAdd(sc, &sc->combinedID, &sc->combinedID, &sc->tempInt);
					}
				}

				if (((sc->actualInverse) && (sc->normalize)) || ((sc->convolutionStep || sc->useBluesteinFFT) && (stageAngle->data.d > 0))) {
					if (normalizationValue.data.i != 1) {
						VkMul(sc, &sc->regIDs[2 * t+1], &sc->regIDs[2 * t+1], &stageNormalization, 0);
					}
					
				}
				appendRegistersToShared(sc, &sc->combinedID, &sc->regIDs[2 * t + 1]);

				if ((require_cutoff_check) && (t == num_logical_groups.data.i - 1)) {
					VkIf_end(sc);
				}
			}
			VkIf_end(sc);
			
			if (require_cutoff_check2) {
				VkIf_end(sc);
			}
			if (sc->useDisableThreads) {
				VkIf_end(sc);
			}
			
			
			appendBarrierVkFFT(sc);
			
		}
	}

	return;
}

#endif