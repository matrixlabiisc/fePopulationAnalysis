// ---------------------------------------------------------------------
//
// Copyright (c) 2017 The Regents of the University of Michigan and DFT-FE authors.
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
// @author Sambit Das (2017)
//

//compute configurational force on the mesh nodes using linear shape function generators
template<unsigned int FEOrder>
void forceClass<FEOrder>::computeConfigurationalForceEEshelbyTensorFPSPFnlLinFE()
{
  const unsigned int numberGlobalAtoms = dftPtr->atomLocations.size();
  const unsigned int numberImageCharges = dftPtr->d_imageIds.size();
  const unsigned int totalNumberAtoms = numberGlobalAtoms + numberImageCharges; 
  std::map<unsigned int, std::vector<double> > forceContributionFPSPLocalGammaAtoms;  

  bool isPseudopotential = dftParameters::isPseudopotential;

  const unsigned int numVectorizedArrayElements=VectorizedArray<double>::n_array_elements;
  const MatrixFree<3,double> & matrix_free_data=dftPtr->matrix_free_data;
  //std::cout<< "n array elements" << numVectorizedArrayElements <<std::endl;

  FEEvaluation<C_DIM,1,C_num1DQuad<FEOrder>(),C_DIM>  forceEval(matrix_free_data,d_forceDofHandlerIndex, 0);
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> phiTotEval(matrix_free_data,dftPtr->phiTotDofHandlerIndex, 0);
#ifdef ENABLE_PERIODIC_BC
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),2> psiEval(matrix_free_data,dftPtr->eigenDofHandlerIndex , 0);
#else  
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> psiEval(matrix_free_data,dftPtr->eigenDofHandlerIndex , 0);
#endif  
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> phiExtEval(matrix_free_data, dftPtr->phiExtDofHandlerIndex, 0);
  QGauss<C_DIM>  quadrature(C_num1DQuad<FEOrder>());   
  FEValues<C_DIM> feVselfValues (dftPtr->FE, quadrature, update_gradients | update_quadrature_points);

  const unsigned int numQuadPoints=forceEval.n_q_points;
  const unsigned int numEigenVectors=dftPtr->eigenVectorsOrig[0].size();  
  const unsigned int numKPoints=dftPtr->d_kPointWeights.size();
  DoFHandler<C_DIM>::active_cell_iterator subCellPtr;
  Tensor<1,2,VectorizedArray<double> > zeroTensor1;zeroTensor1[0]=make_vectorized_array(0.0);zeroTensor1[1]=make_vectorized_array(0.0);
  Tensor<1,2, Tensor<1,C_DIM,VectorizedArray<double> > > zeroTensor2;
  Tensor<1,C_DIM,VectorizedArray<double> > zeroTensor3;
  for (unsigned int idim=0; idim<C_DIM; idim++){
    zeroTensor2[0][idim]=make_vectorized_array(0.0);
    zeroTensor2[1][idim]=make_vectorized_array(0.0);
    zeroTensor3[idim]=make_vectorized_array(0.0);
  }

  VectorizedArray<double> phiExtFactor=make_vectorized_array(0.0);
  if (isPseudopotential){
    phiExtFactor=make_vectorized_array(1.0);
  }

  for (unsigned int cell=0; cell<matrix_free_data.n_macro_cells(); ++cell){
    forceEval.reinit(cell);
    phiTotEval.reinit(cell);
    psiEval.reinit(cell);    
    phiTotEval.read_dof_values_plain(dftPtr->poissonPtr->phiTotRhoOut);//read without taking constraints into account
    phiTotEval.evaluate(true,true);

    phiExtEval.reinit(cell);
    phiExtEval.read_dof_values_plain(dftPtr->poissonPtr->phiExt);
    phiExtEval.evaluate(true,true);

    std::vector<VectorizedArray<double> > rhoQuads(numQuadPoints,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradRhoQuads(numQuadPoints,zeroTensor3);
    std::vector<VectorizedArray<double> > excQuads(numQuadPoints,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > derExcGradRhoQuads(numQuadPoints,zeroTensor3);
    std::vector<VectorizedArray<double> > pseudoVLocQuads(numQuadPoints,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradPseudoVLocQuads(numQuadPoints,zeroTensor3);
    const unsigned int numSubCells=matrix_free_data.n_components_filled(cell);
    std::vector<double> exchValQuads(numQuadPoints);
    std::vector<double> corrValQuads(numQuadPoints); 
    std::vector<double> sigmaValQuads(numQuadPoints);
    std::vector<double> derExchEnergyWithDensityVal(numQuadPoints), derCorrEnergyWithDensityVal(numQuadPoints), derExchEnergyWithSigma(numQuadPoints),derCorrEnergyWithSigma(numQuadPoints);    
    for (unsigned int iSubCell=0; iSubCell<numSubCells; ++iSubCell)
    {
       subCellPtr= matrix_free_data.get_cell_iterator(cell,iSubCell);
       dealii::CellId subCellId=subCellPtr->id();
       if(dftParameters::xc_id == 4)
       {
	  for (unsigned int q = 0; q < numQuadPoints; ++q)
	  {
	      double gradRhoX = ((*dftPtr->gradRhoOutValues)[subCellId][3*q + 0]);
	      double gradRhoY = ((*dftPtr->gradRhoOutValues)[subCellId][3*q + 1]);
	      double gradRhoZ = ((*dftPtr->gradRhoOutValues)[subCellId][3*q + 2]);
	      sigmaValQuads[q] = gradRhoX*gradRhoX + gradRhoY*gradRhoY + gradRhoZ*gradRhoZ;
	  }	   
	  xc_gga_exc_vxc(&(dftPtr->funcX),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&sigmaValQuads[0],&exchValQuads[0],&derExchEnergyWithDensityVal[0],&derExchEnergyWithSigma[0]);
	  xc_gga_exc_vxc(&(dftPtr->funcC),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&sigmaValQuads[0],&corrValQuads[0],&derCorrEnergyWithDensityVal[0],&derCorrEnergyWithSigma[0]);
          for (unsigned int q=0; q<numQuadPoints; ++q)
	  {
	     excQuads[q][iSubCell]=exchValQuads[q]+corrValQuads[q];
	     double temp = derExchEnergyWithSigma[q]+derCorrEnergyWithSigma[q];
	     derExcGradRhoQuads[q][0][iSubCell]=(*dftPtr->gradRhoOutValues)[subCellId][3*q]*temp;
	     derExcGradRhoQuads[q][1][iSubCell]=(*dftPtr->gradRhoOutValues)[subCellId][3*q+1]*temp;
             derExcGradRhoQuads[q][2][iSubCell]=(*dftPtr->gradRhoOutValues)[subCellId][3*q+2]*temp; 	     
          }	  
	  
       }
       else
       {
          xc_lda_exc(&(dftPtr->funcX),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&exchValQuads[0]);
          xc_lda_exc(&(dftPtr->funcC),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&corrValQuads[0]);     
          for (unsigned int q=0; q<numQuadPoints; ++q)
	  {
	     excQuads[q][iSubCell]=exchValQuads[q]+corrValQuads[q];
          }
       }

       for (unsigned int q=0; q<numQuadPoints; ++q)
       {
         rhoQuads[q][iSubCell]=(*dftPtr->rhoOutValues)[subCellId][q];
         if(dftParameters::xc_id == 4)
	 {
	   gradRhoQuads[q][0][iSubCell]=(*dftPtr->gradRhoOutValues)[subCellId][3*q];
	   gradRhoQuads[q][1][iSubCell]=(*dftPtr->gradRhoOutValues)[subCellId][3*q+1];
           gradRhoQuads[q][2][iSubCell]=(*dftPtr->gradRhoOutValues)[subCellId][3*q+2]; 
	 }	 
       }
    }   
   
    if(isPseudopotential)
    {
       for (unsigned int iSubCell=0; iSubCell<numSubCells; ++iSubCell)
       {
          subCellPtr= matrix_free_data.get_cell_iterator(cell,iSubCell);
          dealii::CellId subCellId=subCellPtr->id();	       
	  for (unsigned int q=0; q<numQuadPoints; ++q)
	  {
	     pseudoVLocQuads[q][iSubCell]=((*dftPtr->pseudoValues)[subCellId][q]);
	     gradPseudoVLocQuads[q][0][iSubCell]=d_gradPseudoVLoc[subCellId][C_DIM*q+0];
             gradPseudoVLocQuads[q][1][iSubCell]=d_gradPseudoVLoc[subCellId][C_DIM*q+1];
	     gradPseudoVLocQuads[q][2][iSubCell]=d_gradPseudoVLoc[subCellId][C_DIM*q+2];
	  }
       }
       //compute FPSPLocalGammaAtoms  (contibution due to Gamma(Rj)) 
       FPSPLocalGammaAtomsElementalContribution(forceContributionFPSPLocalGammaAtoms,
		                                feVselfValues,
			                        forceEval,
					        cell,
					        rhoQuads);      
    }//is pseudopotential check
#ifdef ENABLE_PERIODIC_BC
    std::vector<Tensor<1,2,VectorizedArray<double> > > psiQuads(numQuadPoints*numEigenVectors*numKPoints,zeroTensor1);
    std::vector<Tensor<1,2,Tensor<1,C_DIM,VectorizedArray<double> > > > gradPsiQuads(numQuadPoints*numEigenVectors*numKPoints,zeroTensor2);
#else     
    std::vector< VectorizedArray<double> > psiQuads(numQuadPoints*numEigenVectors,make_vectorized_array(0.0));
    std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradPsiQuads(numQuadPoints*numEigenVectors,zeroTensor3);
#endif    
    for (unsigned int ikPoint=0; ikPoint<numKPoints; ++ikPoint){ 
     for (unsigned int iEigenVec=0; iEigenVec<numEigenVectors; ++iEigenVec){
       //psiEval.reinit(cell);	    
       psiEval.read_dof_values_plain(*((dftPtr->eigenVectorsOrig)[ikPoint][iEigenVec]));//read without taking constraints into account
       psiEval.evaluate(true,true);
       for (unsigned int q=0; q<numQuadPoints; ++q){
         psiQuads[q*numEigenVectors*numKPoints+numEigenVectors*ikPoint+iEigenVec]=psiEval.get_value(q);   
         gradPsiQuads[q*numEigenVectors*numKPoints+numEigenVectors*ikPoint+iEigenVec]=psiEval.get_gradient(q);	 
       }     
     } 
    }
    

    for (unsigned int q=0; q<numQuadPoints; ++q){
       VectorizedArray<double> phiTot_q =phiTotEval.get_value(q);   
       Tensor<1,C_DIM,VectorizedArray<double> > gradPhiTot_q =phiTotEval.get_gradient(q);
       VectorizedArray<double> phiExt_q =phiExtEval.get_value(q)*phiExtFactor;
#ifdef ENABLE_PERIODIC_BC      
       Tensor<2,C_DIM,VectorizedArray<double> > E=eshelbyTensor::getELocEshelbyTensorPeriodic(phiTot_q,
			                                      gradPhiTot_q,
						              rhoQuads[q],
						              gradRhoQuads[q],
						              excQuads[q],
						              derExcGradRhoQuads[q],
							      pseudoVLocQuads[q],
							      phiExt_q,				      
						              psiQuads.begin()+q*numEigenVectors*numKPoints,
						              gradPsiQuads.begin()+q*numEigenVectors*numKPoints,
							      dftPtr->d_kPointCoordinates,
							      dftPtr->d_kPointWeights,
							      dftPtr->eigenValues,
							      dftPtr->fermiEnergy,
							      dftParameters::TVal);
#else         
       Tensor<2,C_DIM,VectorizedArray<double> > E=eshelbyTensor::getELocEshelbyTensorNonPeriodic(phiTot_q,
			                                         gradPhiTot_q,
						                 rhoQuads[q],
						                 gradRhoQuads[q],
						                 excQuads[q],
						                 derExcGradRhoQuads[q],
								 pseudoVLocQuads[q],
								 phiExt_q,
						                 psiQuads.begin()+q*numEigenVectors,
						                 gradPsiQuads.begin()+q*numEigenVectors,
								 (dftPtr->eigenValues)[0],
								 dftPtr->fermiEnergy,
								 dftParameters::TVal);
#endif
       forceEval.submit_gradient(E,q);       
    }
    if(isPseudopotential){
      for (unsigned int q=0; q<numQuadPoints; ++q){
        Tensor<1,C_DIM,VectorizedArray<double> > gradPhiExt_q =phiExtEval.get_gradient(q);
	Tensor<1,C_DIM,VectorizedArray<double> > F=eshelbyTensor::getFPSPLocal(rhoQuads[q],
		                                                               gradPseudoVLocQuads[q],
			                                                       gradPhiExt_q);
        forceEval.submit_value(F,q);	      
      }
      forceEval.integrate(true,true);
    }
    else{
      forceEval.integrate (false,true);
    }    
    forceEval.distribute_local_to_global(d_configForceVectorLinFE);//also takes care of constraints
  }

  // add global FPSPLocal contribution due to Gamma(Rj) to the configurational force vector
  if(isPseudopotential){
     distributeForceContributionFPSPLocalGammaAtoms(forceContributionFPSPLocalGammaAtoms);
  }  
}
