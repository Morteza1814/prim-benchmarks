/*
* BFS with multiple tasklets
*
*/
#include <stdio.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <perfcounter.h>

#include "dpu-utils.h"
#include "common.h"

BARRIER_INIT(my_barrier, NR_TASKLETS);
BARRIER_INIT(bfsBarrier, NR_TASKLETS);
BARRIER_INIT(beforeStoringTC, NR_TASKLETS);

MUTEX_INIT(nextFrontierMutex);

// main
int main() {

    if(me() == 0) {
        mem_reset(); // Reset the heap
    }
    // Barrier
    barrier_wait(&my_barrier);

    // Load parameters
    uint32_t params_m = (uint32_t) DPU_MRAM_HEAP_POINTER;
    struct DPUParams* params_w = (struct DPUParams*) mem_alloc(ROUND_UP_TO_MULTIPLE_OF_8(sizeof(struct DPUParams)));
    mram_read((__mram_ptr void const*)params_m, params_w, ROUND_UP_TO_MULTIPLE_OF_8(sizeof(struct DPUParams)));

    // Extract parameters
    uint32_t numGlobalNodes = params_w->numNodes;
    uint32_t startNodeIdx = params_w->dpuStartNodeIdx;
    uint32_t numNodes = params_w->dpuNumNodes;
    uint32_t nodePtrsOffset = params_w->dpuNodePtrsOffset;
    uint32_t nodePtrs_m = params_w->dpuNodePtrs_m;
    uint32_t neighborIdxs_m = params_w->dpuNeighborIdxs_m;
    uint32_t dpuTriangleCount_m = params_w->dpuTriangleCount_m;

    if(numNodes > 0) {

        uint64_t* cache_w = mem_alloc(sizeof(uint64_t));
           

        // Identify tasklet's nodes
        uint32_t numNodesPerTasklet = (numNodes + NR_TASKLETS - 1)/NR_TASKLETS;
        uint32_t taskletNodesStart = me()*numNodesPerTasklet;
        uint32_t taskletNumNodes;
        if(taskletNodesStart > numNodes) {
            taskletNumNodes = 0;
        } else if(taskletNodesStart + numNodesPerTasklet > numNodes) {
            taskletNumNodes = numNodes - taskletNodesStart;
        } else {
            taskletNumNodes = numNodesPerTasklet;
        }

        uint32_t localTriangleCount = 0;
        for(uint32_t u = taskletNodesStart; u < taskletNodesStart + taskletNumNodes; ++u) {
            // get node u neighbors
            uint32_t uNodePtr = load4B(nodePtrs_m, u, cache_w) - nodePtrsOffset;
            uint32_t uNextNodePtr = load4B(nodePtrs_m, u + 1, cache_w) - nodePtrsOffset; // TODO: Optimize: might be in the same 8B as nodePtr
            
            // iterate through node u neighbors
            for(uint32_t i = uNodePtr; i < uNextNodePtr; ++i) {
                // read node v
                uint32_t v = load4B(neighborIdxs_m, i, cache_w); // TODO: Optimize: sequential access to neighbors can use sequential reader
                
                // read node v neighbors
                uint32_t vNodePtr = load4B(nodePtrs_m, v, cache_w) - nodePtrsOffset;
                uint32_t vNextNodePtr = load4B(nodePtrs_m, v + 1, cache_w) - nodePtrsOffset;
                // iterate through node v neighbors
                for(uint32_t j = vNodePtr; j < vNextNodePtr; ++j) {
                    uint32_t vNeighbor = load4B(neighborIdxs_m, j, cache_w);
                    for(uint32_t k = uNodePtr; k < uNextNodePtr; ++k) {
                        uint32_t uNeighbor = load4B(neighborIdxs_m, k, cache_w);
                        if(uNeighbor != v && vNeighbor != u && uNeighbor == vNeighbor) {
                            localTriangleCount++;
                        }
                    }
                }
            }
        }
        mutex_id_t mutexID = MUTEX_GET(nextFrontierMutex);
        mutex_lock(mutexID);
        uint32_t dpuTriangleCount = load4B(dpuTriangleCount_m, 0, cache_w);   
        dpuTriangleCount += localTriangleCount;
        store4B(dpuTriangleCount, dpuTriangleCount_m, 0, cache_w);
        mutex_unlock(mutexID);

        barrier_wait(&beforeStoringTC);

        if(me() == 0) {
            uint32_t dpuTriangleCount_last = load4B(dpuTriangleCount_m, 0, cache_w);   
            //devide by 6 because of repeatations
            uint32_t finalTriangleCount = dpuTriangleCount_last / 6;
            // Store triangle count
            store4B(finalTriangleCount, dpuTriangleCount_m, 0, cache_w);
        }
    }
    
    return 0;
}