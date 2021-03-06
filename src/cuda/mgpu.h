/*
  !---------------------------------------------------------------------!
  ! Written by Madu Manathunga on 04/29/2020                            !
  !                                                                     ! 
  ! Copyright (C) 2020-2021 Merz lab                                    !
  ! Copyright (C) 2020-2021 Götz lab                                    !
  !                                                                     !
  ! This Source Code Form is subject to the terms of the Mozilla Public !
  ! License, v. 2.0. If a copy of the MPL was not distributed with this !
  ! file, You can obtain one at http://mozilla.org/MPL/2.0/.            !
  !_____________________________________________________________________!

  !---------------------------------------------------------------------!
  ! This source file contains methods required for QUICK multi GPU      !
  ! implementation. Additional changes have been made in the gpu_type.h !
  ! where we define variables holding device information.               !
  !                                                                     ! 
  !---------------------------------------------------------------------!
*/

#ifdef CUDA_MPIV

#include "string.h"

// device information
int validDevCount = 0;      // Number of devices that can be used
int* gpu_dev_id;            // Holds device IDs 

//-----------------------------------------------
// Get information about available GPUs and prepare
// a list of usable GPUs. Only master cpu runs this.
//-----------------------------------------------

extern "C" void mgpu_query_(int *mpisize, int *devcount)
{

//    PRINTDEBUG("BEGIN QUERYING DEVICES")
    int gpuCount = 0;           // Total number of cuda devices available
    size_t minMem = 8000000000; // Threshold  memory (in bytes) for device selection criteria
    cudaError_t status;
    bool isZeroID = false;

    status = cudaGetDeviceCount(&gpuCount);

    if(gpuCount == 0){
        PRINTERROR(status,"cudaGetDeviceCount gpu_init failed!");
        cudaDeviceReset();
        exit(-1);
    }else if(gpuCount == 1){
        isZeroID = true;
        gpuCount = *mpisize; 
    }

    int tmp_gpu_dev_id[gpuCount];        // Temporarily holds device IDs 
    unsigned int idx_tmp_gpu_dev_id = 0; // Array index counter for tmp_gpu_dev_id
    int devID = 0 ;

    for(int i=0;i<gpuCount;i++){

        cudaDeviceProp devProp;

        if(isZeroID == true){
            cudaGetDeviceProperties(&devProp, 0);
            devID = 0;
        }else{
            cudaGetDeviceProperties(&devProp, i);
            devID = i;
        }
        // Should be fixed to select based on sm value used during the compilation
        // For now, we select Volta and Turing devices only

        if((devProp.major == 7) && (devProp.minor >= 0) && (devProp.totalGlobalMem > minMem)){
            validDevCount++;
            tmp_gpu_dev_id[idx_tmp_gpu_dev_id] = devID;
            idx_tmp_gpu_dev_id++;
        }

    }

    if (validDevCount != gpuCount && validDevCount < *mpisize) {
        printf("MPISIZE AND NUMBER OF AVAILABLE GPUS MUST BE THE SAME.\n");
        gpu_shutdown_();
        exit(-1);
    }else{

        if(validDevCount > *mpisize){validDevCount = *mpisize;}

        // Store usable device IDs to broadcast to slaves

        gpu_dev_id = (int*) malloc(validDevCount*sizeof(int));

        for(int i=0; i<validDevCount; i++){
            gpu_dev_id[i] = tmp_gpu_dev_id[i];
        }

    }
 
    // send the device count back to f90 side
    *devcount = validDevCount;   

    return;
}

//-----------------------------------------------
// send gpu ids to f90 side for broadcasting
//-----------------------------------------------

extern "C" void mgpu_get_devices_(int *gpu_dev_ids)
{

    for(int i=0; i<validDevCount; i++){
	    gpu_dev_ids[i] = gpu_dev_id[i];
    }    

	return;
}

//-----------------------------------------------
// create gpu class
//-----------------------------------------------
void mgpu_startup(int mpirank)
{

#ifdef DEBUG
    char fname[16];
    sprintf(fname, "debug.cuda.%i", mpirank);    

    debugFile = fopen(fname, "w+");
#endif

    PRINTDEBUGNS("BEGIN TO WARM UP")

    gpu = new gpu_type;

#ifdef DEBUG
    gpu->debugFile = debugFile;
#endif

    PRINTDEBUG("CREATE NEW GPU")
}

//-----------------------------------------------
// Finalize the devices
//-----------------------------------------------
extern "C" void mgpu_shutdown_(void)
{ 

    PRINTDEBUG("BEGIN TO SHUTDOWN DEVICES")

    delete gpu;
    cudaDeviceReset();

    PRINTDEBUGNS("END DEVICE SHUTDOWN")    

#ifdef DEBUG
    fclose(debugFile);
#endif

}
//-----------------------------------------------
// Initialize the devices
//-----------------------------------------------
extern "C" void mgpu_init_(int *mpirank, int *mpisize, int *device)
{

    cudaError_t status;
    cudaDeviceProp deviceProp;

    // Each node starts up GPUs
    mgpu_startup(*mpirank);

    PRINTDEBUG("BEGIN MULTI GPU INITIALIZATION")

    gpu -> mpirank = *mpirank;
    gpu -> mpisize = *mpisize;
    gpu -> gpu_dev_id = *device;

#ifdef DEBUG
    fprintf(gpu->debugFile,"mpirank %i mpisize %i dev_id %i \n", *mpirank, *mpisize, *device);
#endif

    gpu -> gpu_dev_id = *device;

    status = cudaSetDevice(gpu -> gpu_dev_id);
    cudaGetDeviceProperties(&deviceProp, gpu -> gpu_dev_id);
    PRINTERROR(status, "cudaSetDevice gpu_init failed!");
    cudaDeviceSynchronize();

    cudaDeviceSetCacheConfig(cudaFuncCachePreferL1);

    size_t val;

    cudaDeviceGetLimit(&val, cudaLimitStackSize);
#ifdef DEBUG
    fprintf(gpu->debugFile,"mpirank: %i Stack size limit:    %zu\n", gpu -> mpirank,val);
#endif

    cudaDeviceGetLimit(&val, cudaLimitPrintfFifoSize);
#ifdef DEBUG
    fprintf(gpu->debugFile,"mpirank: %i Printf fifo limit:   %zu\n", gpu -> mpirank,val);
#endif

    cudaDeviceGetLimit(&val, cudaLimitMallocHeapSize);
#ifdef DEBUG
    fprintf(gpu->debugFile,"mpirank: %i Heap size limit:     %zu\n", gpu -> mpirank,val);
#endif

    cudaDeviceSetLimit(cudaLimitStackSize, 8192);

    cudaDeviceGetLimit(&val, cudaLimitStackSize);
#ifdef DEBUG
    fprintf(gpu->debugFile,"mpirank: %i New Stack size limit:    %zu\n", gpu -> mpirank,val);
#endif

    gpu->blocks = deviceProp.multiProcessorCount;

    gpu -> sm_version               = SM_2X;
    gpu -> threadsPerBlock          = SM_2X_THREADS_PER_BLOCK;
    gpu -> twoEThreadsPerBlock      = SM_2X_2E_THREADS_PER_BLOCK;
    gpu -> XCThreadsPerBlock        = SM_2X_XC_THREADS_PER_BLOCK;
    gpu -> gradThreadsPerBlock      = SM_2X_GRAD_THREADS_PER_BLOCK;

    PRINTDEBUG("FINISH MULTI GPU INITIALIZATION")

    return;
}

//--------------------------------------------------------
// Method to distribute sorted shell information among nodes  
//--------------------------------------------------------
void mgpu_eri_greedy_distribute(){

    // Total number of items to distribute
    int nitems=gpu->gpu_cutoff->sqrQshell;

    // Array to store total number of items each core would have
    int tot_pcore[gpu->mpisize];

    // Save shell indices for each core
    int2 mpi_qidx[gpu->mpisize][nitems];

    // Keep track of primitive count
    int2 mpi_pidx[gpu->mpisize][nitems];

    // Save a set of flags unique to each core, these will be uploaded 
    // to GPU by responsible cores
    char mpi_flags[gpu->mpisize][nitems];

    // Keep track of shell type
    int2 qtypes[gpu->mpisize][nitems];

    // Keep track of total primitive value of each core
    int tot_pval[gpu->mpisize]; 

    // Keep track of how many shell types each core has
    // ss, sp, sd, ps, pp, pd, dd, dp, dd
    int qtype_pcore[gpu->mpisize][16];

    //set arrays to zero
    memset(tot_pcore,0, sizeof(int)*gpu->mpisize);
    memset(mpi_qidx,0,sizeof(int2)*gpu->mpisize*nitems);
    memset(mpi_pidx,0,sizeof(int2)*gpu->mpisize*nitems);
    memset(mpi_flags,0,sizeof(char)*gpu->mpisize*nitems);
    memset(qtypes,0,sizeof(int2)*gpu->mpisize*nitems);
    memset(tot_pval,0,sizeof(int)*gpu->mpisize);
    memset(qtype_pcore,0,sizeof(int2)*gpu->mpisize*16);

#ifdef DEBUG
    fprintf(gpu->debugFile," Greedy distribute sqrQshells= %i number of GPUs= %i \n", nitems, gpu->mpisize);
#endif

    int q1_idx, q2_idx, q1, q2, p1, p2, psum, minp, min_core;
    // Helps to store shell types per each core
    int a=0;

    // Sort s,p,d for the time being, increase the value by one to facilitate sorting
    for(int q1_typ=0; q1_typ<4; q1_typ++){
        for(int q2_typ=0; q2_typ<4; q2_typ++){

            //Go through items
            for (int i = 0; i<nitems; i++) {  

                // Get the shell type
                q1     = gpu->gpu_basis->sorted_Qnumber->_hostData[gpu->gpu_cutoff->sorted_YCutoffIJ ->_hostData[i].x];
                q2     = gpu->gpu_basis->sorted_Qnumber->_hostData[gpu->gpu_cutoff->sorted_YCutoffIJ ->_hostData[i].y];
 
                // Check if the picked shell types match currently interested shell types              
                if(q1 == q1_typ && q2 == q2_typ){
                    
                    // Find out the core with least number of primitives of the current shell types
                    min_core = 0;       // Assume master has the lowest number of primitives
                    minp = tot_pval[0]; // Set master's primitive count as the lowest
                    for(int impi=0; impi<gpu->mpisize;impi++){
                        if(minp > tot_pval[impi]){
                            minp = tot_pval[impi];
                            min_core = impi;
                        }
                    }

                    // Store the primitive value in the total primitive value counter
                    p1 = gpu->gpu_basis->kprim->_hostData[gpu->gpu_basis->sorted_Q->_hostData[gpu->gpu_cutoff->sorted_YCutoffIJ ->_hostData[i].x]];
                    p2 = gpu->gpu_basis->kprim->_hostData[gpu->gpu_basis->sorted_Q->_hostData[gpu->gpu_cutoff->sorted_YCutoffIJ ->_hostData[i].y]]; 
                    psum=p1+p2;
                    tot_pval[min_core] += psum;

                    //Get the q indices  
                    q1_idx = gpu->gpu_basis->sorted_Q->_hostData[gpu->gpu_cutoff->sorted_YCutoffIJ ->_hostData[i].x];                    
                    q2_idx = gpu->gpu_basis->sorted_Q->_hostData[gpu->gpu_cutoff->sorted_YCutoffIJ ->_hostData[i].y];

                    //Assign the indices for corresponding core
                    mpi_qidx[min_core][tot_pcore[min_core]].x = q1_idx;
                    mpi_qidx[min_core][tot_pcore[min_core]].y = q2_idx;

                    // Save the flag
                    mpi_flags[min_core][i] = 1;

                    // Store shell types for debugging
                    qtype_pcore[min_core][a] +=1;
                    
                    //Store primitve number for debugging
                    mpi_pidx[min_core][tot_pcore[min_core]].x = p1;
                    mpi_pidx[min_core][tot_pcore[min_core]].y = p2;

                    // Store the Qshell type for debugging
                    qtypes[min_core][tot_pcore[min_core]].x = q1;
                    qtypes[min_core][tot_pcore[min_core]].y = q2;                    
                   
                    // Increase the counter for minimum core
                    tot_pcore[min_core] += 1;
                }                
            }

            // Reset the primitive counter for current shell type 
            memset(tot_pval,0,sizeof(int)*gpu->mpisize);
            a++;
        }
    }

#ifdef DEBUG
    // Print information for debugging
    for(int impi=0; impi<gpu->mpisize; impi++){
        for(int icount=0; icount<tot_pcore[impi]; icount++){
            fprintf(gpu->debugFile," Greedy Distribute GPU: %i Qindex= %i %i Qtype= %i %i Prim= %i %i \n ",impi, mpi_qidx[impi][icount].x, mpi_qidx[impi][icount].y, \
            qtypes[impi][icount].x, qtypes[impi][icount].y, mpi_pidx[impi][icount].x, mpi_pidx[impi][icount].y);
        }
    }

    for(int impi=0; impi<gpu->mpisize;impi++){
        fprintf(gpu->debugFile," Greedy Distribute GPU: %i ss= %i sp= %i sd= %i sf= %i ps= %i pp= %i pd= %i pf= %i ds= %i dp= %i dd= %i df= %i fs= %i fp=%i fd=%i ff=%i \n",impi, qtype_pcore[impi][0], \
        qtype_pcore[impi][1], qtype_pcore[impi][2], qtype_pcore[impi][3], qtype_pcore[impi][4], qtype_pcore[impi][5], \
        qtype_pcore[impi][6], qtype_pcore[impi][7], qtype_pcore[impi][8], qtype_pcore[impi][9], qtype_pcore[impi][10],\
        qtype_pcore[impi][11], qtype_pcore[impi][12], qtype_pcore[impi][13], qtype_pcore[impi][14], qtype_pcore[impi][15]);
    }

    fprintf(gpu->debugFile," Greedy Distribute GPU: %i Total shell pairs for this GPU= %i \n", gpu -> mpirank, tot_pcore[gpu -> mpirank]);
#endif

    // Upload the flags to GPU
    gpu -> gpu_basis -> mpi_bcompute = new cuda_buffer_type<char>(nitems);

    memcpy(gpu -> gpu_basis -> mpi_bcompute -> _hostData, &mpi_flags[gpu->mpirank][0], sizeof(char)*nitems);

    gpu -> gpu_basis -> mpi_bcompute -> Upload();
    gpu -> gpu_sim.mpi_bcompute  = gpu -> gpu_basis -> mpi_bcompute  -> _devData;

}

//--------------------------------------------------------
// Function to distribute XC quadrature bins among nodes. 
// Note that here we naively distribute packed bins. 
//--------------------------------------------------------
void mgpu_xc_naive_distribute(){

    // due to grid point packing, npoints is always a multiple of bin_size
    int nbins    = gpu -> gpu_xcq -> nbins;
    int bin_size = gpu -> gpu_xcq -> bin_size;

#ifdef DEBUG
    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i nbins= %i bin_size= %i \n", gpu->mpirank, nbins, bin_size);
#endif

    // array to keep track of how many bins per core
    int bins_pcore[gpu->mpisize];

    memset(bins_pcore,0, sizeof(int)*gpu->mpisize);

    int dividend  = (int) (nbins/gpu->mpisize);  
    int remainder = nbins - (dividend * gpu->mpisize);

#ifdef DEBUG
    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i dividend= %i remainder= %i \n", gpu->mpirank, dividend, remainder);
#endif

    for(int i=0; i< gpu->mpisize; i++){
        bins_pcore[i] = dividend;
    }

#ifdef DEBUG
    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i bins_pcore[0]= %i bins_pcore[1]= %i \n", gpu->mpirank, bins_pcore[0], bins_pcore[1]);
#endif

    // distribute the remainder among cores
    int cremainder = remainder;
    for(int i=0; i<remainder; i+=gpu->mpisize ){
        for(int j=0; j< gpu->mpisize; j++){
            bins_pcore[j] += 1;
            cremainder--;

            if(cremainder < 1) {
                break;
            }
        } 
    }

#ifdef DEBUG
    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i bins_pcore[0]= %i bins_pcore[1]= %i \n", gpu->mpirank, bins_pcore[0], bins_pcore[1]);
#endif

    // compute lower and upper grid point limits
    int xcstart, xcend, count;
    count = 0;

    if(gpu->mpirank == 0){
        xcstart = 0;
        xcend   = bins_pcore[gpu->mpirank] * bin_size;
    }else{

#ifdef DEBUG
    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i setting borders.. \n", gpu -> mpirank);
#endif

        for(int i=0; i < gpu->mpirank; i++){
            count += bins_pcore[i];
#ifdef DEBUG
    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i count= %i \n", gpu -> mpirank, count);
#endif
        }
     
        xcstart = count * bin_size;
        xcend   = (count + bins_pcore[gpu->mpirank]) * bin_size;
#ifdef DEBUG
    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i start and end points= %i %i \n", gpu -> mpirank, xcstart, xcend);
#endif

    }

    gpu -> gpu_sim.mpi_xcstart = xcstart;
    gpu -> gpu_sim.mpi_xcend   = xcend;

#ifdef DEBUG
    // print information for debugging

    for(int i=0; i<gpu->mpisize; i++){
        fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i number of bins for gpu %i = %i \n", gpu -> mpirank, i, bins_pcore[i]);
    }

    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i start and end points= %i %i \n", gpu -> mpirank, xcstart, xcend);

    fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i start and end points= %i %i \n", gpu -> mpirank, gpu -> gpu_sim.mpi_xcstart, gpu -> gpu_sim.mpi_xcend);

#endif

}


//--------------------------------------------------------
// Function to distribute XC quadrature points among nodes.  
// Note that here we consider number of true grid points in 
// each bin during the distribution.
//--------------------------------------------------------
void mgpu_xc_greedy_distribute(){

    PRINTDEBUG("BEGIN TO DISTRIBUTE XC GRID POINTS")

    // due to grid point packing, npoints is always a multiple of bin_size
    int nbins    = gpu -> gpu_xcq -> nbins;
    int bin_size = gpu -> gpu_xcq -> bin_size;

#ifdef DEBUG
    fprintf(gpu->debugFile,"GPU: %i nbins= %i bin_size= %i \n", gpu->mpirank, nbins, bin_size);
#endif

    // array to keep track of how many true grid points per bin
    int tpoints[nbins];

    // save a set of flags to indicate if a given node should work on a particular bin
    char mpi_xcflags[gpu->mpisize][nbins];

    // array to keep track of how many bins per gpu
    int bins_pcore[gpu->mpisize];

    // array to keep track of how many true grid points per core
    int tpts_pcore[gpu->mpisize];

    // initialize all arrays to zero
    memset(tpoints,0, sizeof(int)*nbins);
    memset(mpi_xcflags,0, sizeof(char)*nbins*gpu->mpisize);
    memset(bins_pcore,0, sizeof(int)*gpu->mpisize);
    memset(tpts_pcore,0, sizeof(int)*gpu->mpisize);

    // count how many true grid point in each bin and store in tpoints
    int tot_tpts=0;
    for(int i=0; i<nbins; i++){
        for(int j=0; j<bin_size; j++){
            if(gpu -> gpu_xcq -> dweight -> _hostData[i*bin_size + j] > 0 ){
                tpoints[i]++;
                tot_tpts++;
            }
        }
    }

#ifdef DEBUG
    for(int i=0; i<nbins; i++){
        fprintf(gpu->debugFile,"GPU: %i bin= %i true points= %i \n", gpu->mpirank, i, tpoints[i]);
    }
#endif

    // now distribute the bins considering the total number of true grid points each core would receive 

    int mincore, min_tpts;

    for(int i=0; i<nbins; i++){

        // find out the core with minimum number of true grid points
        mincore  = 0;             // assume master has the lowest number of points
        min_tpts = tpts_pcore[0]; // set master's point count as default

        for(int impi=0; impi< gpu->mpisize; impi++){
            if(min_tpts > tpts_pcore[impi]){
                mincore  = impi;
                min_tpts = tpts_pcore[impi];
            }
        }

        // increase the point counter by the amount in current bin
        tpts_pcore[mincore] += tpoints[i];

        // assign the bin to corresponding core        
        mpi_xcflags[mincore][i] = 1;

    }

#ifdef DEBUG

    // print information for debugging
    for(int i=0; i<gpu->mpisize; i++){
        fprintf(gpu->debugFile," XC Greedy Distribute GPU: %i number of points for gpu %i = %i \n", gpu -> mpirank, i, tpts_pcore[i]);
    }

#endif

    // upload flags to gpu
    gpu -> gpu_xcq -> mpi_bxccompute = new cuda_buffer_type<char>(nbins);

    memcpy(gpu -> gpu_xcq -> mpi_bxccompute -> _hostData, &mpi_xcflags[gpu->mpirank][0], sizeof(char)*nbins);

    gpu -> gpu_xcq -> mpi_bxccompute -> Upload();

    gpu -> gpu_sim.mpi_bxccompute  = gpu -> gpu_xcq -> mpi_bxccompute  -> _devData;

    PRINTDEBUG("END DISTRIBUTING XC GRID POINTS")

}


//--------------------------------------------------------
// Methods passing gpu information to f90 side for printing
//--------------------------------------------------------

extern "C" void mgpu_get_dev_count_(int *gpu_dev_count){
    *gpu_dev_count = validDevCount;
}

extern "C" void mgpu_get_device_info_(int *rank, int* dev_id,int* gpu_dev_mem,
                                     int* gpu_num_proc,double* gpu_core_freq,char* gpu_dev_name,int* name_len, int* majorv, int* minorv)
{
    cudaDeviceProp prop;
    size_t device_mem;

    *dev_id = gpu_dev_id[*rank];

    cudaGetDeviceProperties(&prop,*dev_id);
    device_mem = (prop.totalGlobalMem/(1024*1024));
    *gpu_dev_mem = (int) device_mem;
    *gpu_num_proc = (int) (prop.multiProcessorCount);
    *gpu_core_freq = (double) (prop.clockRate * 1e-6f);
    strcpy(gpu_dev_name,prop.name);
    *name_len = strlen(gpu_dev_name);
    *majorv = prop.major;
    *minorv = prop.minor;

}
#endif
