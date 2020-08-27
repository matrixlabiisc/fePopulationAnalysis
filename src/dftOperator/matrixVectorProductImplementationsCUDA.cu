// ---------------------------------------------------------------------
//
// Copyright (c) 2017-2018 The Regents of the University of Michigan and DFT-FE authors.
//
// This file is part of the DFT-FE code.
//
// The DFT-FE code is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE at
// the top level of the DFT-FE distribution.
//
// ---------------------------------------------------------------------
//
// @author Phani Motamarri, Sambit Das
//


/** @file matrixVectorProductImplementations.cc
 *  @brief Contains linear algebra operations
 *
 */


	template<unsigned int FEOrder>
void kohnShamDFTOperatorCUDAClass<FEOrder>::computeLocalHamiltonianTimesX(const double* src,
		const unsigned int numberWaveFunctions,
		double* dst) 
{
	const unsigned int kpointSpinIndex=(1+dftParameters::spinPolarized)*d_kPointIndex+d_spinIndex;
	const unsigned int totalLocallyOwnedCells = dftPtr->matrix_free_data.n_physical_cells();  

	copyCUDAKernel<<<(numberWaveFunctions+255)/256*totalLocallyOwnedCells*d_numberNodesPerElement,256>>>(numberWaveFunctions, 
			totalLocallyOwnedCells*d_numberNodesPerElement,
			src,
			thrust::raw_pointer_cast(&d_cellWaveFunctionMatrix[0]),
			thrust::raw_pointer_cast(&d_flattenedArrayCellLocalProcIndexIdMapDevice[0]));


	const double scalarCoeffAlpha = 1.0,scalarCoeffBeta = 0.0;
	const unsigned int strideA = d_numberNodesPerElement*numberWaveFunctions;
	const unsigned int strideB = d_numberNodesPerElement*d_numberNodesPerElement; 
	const unsigned int strideC = d_numberNodesPerElement*numberWaveFunctions;


	cublasDgemmStridedBatched(d_cublasHandle,
			CUBLAS_OP_N,
			CUBLAS_OP_N,
			numberWaveFunctions,
			d_numberNodesPerElement,
			d_numberNodesPerElement,
			&scalarCoeffAlpha,
			thrust::raw_pointer_cast(&d_cellWaveFunctionMatrix[0]),
			numberWaveFunctions,
			strideA,
			thrust::raw_pointer_cast(&d_cellHamiltonianMatrixFlattenedDevice[d_numLocallyOwnedCells*d_numberNodesPerElement*d_numberNodesPerElement*kpointSpinIndex]),
			d_numberNodesPerElement,
			strideB,
			&scalarCoeffBeta,
			thrust::raw_pointer_cast(&d_cellHamMatrixTimesWaveMatrix[0]),
			numberWaveFunctions,
			strideC,
			totalLocallyOwnedCells);


	if(!(dftParameters::isPseudopotential && dftPtr->d_nonLocalAtomGlobalChargeIds.size() > 0))
	{
    daxpyAtomicAddKernel<<<(numberWaveFunctions+255)/256*d_numLocallyOwnedCells*d_numberNodesPerElement,256>>>
      (numberWaveFunctions,
       d_numLocallyOwnedCells*d_numberNodesPerElement,
       thrust::raw_pointer_cast(&d_cellHamMatrixTimesWaveMatrix[0]),
       dst,
       thrust::raw_pointer_cast(&d_flattenedArrayCellLocalProcIndexIdMapDevice[0]));

	}


}
