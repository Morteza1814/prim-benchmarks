/**
* app.c
* BFS Host Application Source File
*
*/
#include <dpu.h>
#include <dpu_log.h>

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mram-management.h"
#include "common.h"
#include "graph.h"
#include "params.h"
#include "timer.h"
#include "utils.h"

#ifndef ENERGY
#define ENERGY 0
#endif
#if ENERGY
#include <dpu_probe.h>
#endif

#define DPU_BINARY "./bin/dpu_code"

    uint32_t countTriangles( uint32_t numNodes, uint32_t* nodePtrs, uint32_t* neighborIdxs) {
        uint32_t numTriangles = 0;

        for (uint32_t u = 0; u < numNodes; u++) {
            for (uint32_t i = nodePtrs[u]; i < nodePtrs[u + 1]; i++) {
                uint32_t v = neighborIdxs[i];
                if (v > u) {
                    for (uint32_t j = nodePtrs[v]; j < nodePtrs[v + 1]; j++) {
                        uint32_t w = neighborIdxs[j];
                        if (w > v) {
                            for (uint32_t k = nodePtrs[u]; k < nodePtrs[u + 1]; k++) {
                                if (neighborIdxs[k] == w) {
                                    numTriangles++;
                                }
                            }
                        }
                    }
                }
            }
        }

        return numTriangles;
    }


// Main of the Host Application
int main(int argc, char** argv) {
    // Timer and profiling
    Timer timer;
    float loadTime = 0.0f, dpuTime = 0.0f, hostTime = 0.0f, retrieveTime = 0.0f, cpuTime = 0.0f;

    // Process parameters
    struct Params p = input_params(argc, argv);

    uint32_t nodePtrs_test[] = {0, 2, 4, 6, 8, 10, 12};
    uint32_t neighborIdxs_test[] = {1, 2, 0, 2, 0, 1, 4, 5, 3, 5, 3, 4};
    startTimer(&timer);
    uint32_t numTriangles = countTriangles(6, nodePtrs_test, neighborIdxs_test);
    stopTimer(&timer);
    cpuTime += getElapsedTime(timer);
    PRINT_INFO(p.verbosity >= 1, "CPU time: %f ms", cpuTime*1e3);
    PRINT_INFO(p.verbosity >= 1, "CPU:   Graph has  %d triangles", numTriangles);

    #if ENERGY
    struct dpu_probe_t probe;
    DPU_ASSERT(dpu_probe_init("energy_probe", &probe));
    double tenergy=0;
    #endif

    // Allocate DPUs and load binary
    struct dpu_set_t dpu_set, dpu;
    uint32_t numDPUs;
    DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpu_set));
    DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));
    DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &numDPUs));
    PRINT_INFO(p.verbosity >= 1, "Allocated %d DPU(s)", numDPUs);

    // Initialize BFS data structures
    PRINT_INFO(p.verbosity >= 1, "Reading graph %s", p.fileName);
    struct COOGraph cooGraph = readCOOGraph(p.fileName);
    PRINT_INFO(p.verbosity >= 1, "    Graph has %d nodes and %d edges", cooGraph.numNodes, cooGraph.numEdges);
    struct CSRGraph csrGraph = coo2csr(cooGraph);
    uint32_t numNodes = 6;
    uint32_t* nodePtrs = nodePtrs_test;
    uint32_t* neighborIdxs = neighborIdxs_test;

    // Partition data structure across DPUs
    uint32_t numNodesPerDPU = ROUND_UP_TO_MULTIPLE_OF_64((numNodes - 1)/numDPUs + 1);
    PRINT_INFO(p.verbosity >= 1, "Assigning %u nodes per DPU", numNodesPerDPU);
    struct DPUParams dpuParams[numDPUs];
    uint32_t dpuParams_m[numDPUs];
    unsigned int dpuIdx = 0;
    DPU_FOREACH (dpu_set, dpu) {

        // Allocate parameters
        struct mram_heap_allocator_t allocator;
        init_allocator(&allocator);
        dpuParams_m[dpuIdx] = mram_heap_alloc(&allocator, sizeof(struct DPUParams));

        // Find DPU's nodes
        uint32_t dpuStartNodeIdx = dpuIdx*numNodesPerDPU;
        uint32_t dpuNumNodes;
        if(dpuStartNodeIdx > numNodes) {
            dpuNumNodes = 0;
        } else if(dpuStartNodeIdx + numNodesPerDPU > numNodes) {
            dpuNumNodes = numNodes - dpuStartNodeIdx;
        } else {
            dpuNumNodes = numNodesPerDPU;
        }
        PRINT_INFO(p.verbosity >= 1, "dpuStartNodeIdx: %u ", dpuStartNodeIdx);//morteza log
        dpuParams[dpuIdx].dpuNumNodes = dpuNumNodes;
        PRINT_INFO(p.verbosity >= 2, "    DPU %u:", dpuIdx);
        PRINT_INFO(p.verbosity >= 2, "        Receives %u nodes", dpuNumNodes);

        // Partition edges and copy data
        if(dpuNumNodes > 0) {

            // Find DPU's CSR graph partition
            uint32_t* dpuNodePtrs_h = &nodePtrs[dpuStartNodeIdx];
            uint32_t dpuNodePtrsOffset = dpuNodePtrs_h[0];
            uint32_t* dpuNeighborIdxs_h = neighborIdxs + dpuNodePtrsOffset;
            uint32_t dpuNumNeighbors = dpuNodePtrs_h[dpuNumNodes] - dpuNodePtrsOffset;
            uint32_t dpuTriangleCount = 0;
            PRINT_INFO(p.verbosity >= 1, "dpuNodePtrsOffset: %u ", dpuNodePtrsOffset);//morteza log
            PRINT_INFO(p.verbosity >= 1, "dpuNumNeighbors: %u ", dpuNumNeighbors);//morteza log

            // Allocate MRAM
            uint32_t dpuNodePtrs_m = mram_heap_alloc(&allocator, (dpuNumNodes + 1)*sizeof(uint32_t));
            uint32_t dpuNeighborIdxs_m = mram_heap_alloc(&allocator, dpuNumNeighbors*sizeof(uint32_t));
            uint32_t dpuTriangleCount_m = mram_heap_alloc(&allocator, sizeof(uint32_t));
            PRINT_INFO(p.verbosity >= 2, "        Total memory allocated is %d bytes", allocator.totalAllocated);

            // Set up DPU parameters
            dpuParams[dpuIdx].numNodes = numNodes;
            dpuParams[dpuIdx].dpuStartNodeIdx = dpuStartNodeIdx;
            dpuParams[dpuIdx].dpuNodePtrsOffset = dpuNodePtrsOffset;
            dpuParams[dpuIdx].dpuNodePtrs_m = dpuNodePtrs_m;
            dpuParams[dpuIdx].dpuNeighborIdxs_m = dpuNeighborIdxs_m;
            dpuParams[dpuIdx].dpuTriangleCount_m = dpuTriangleCount_m;

            // Send data to DPU
            PRINT_INFO(p.verbosity >= 2, "        Copying data to DPU");
            startTimer(&timer);
            copyToDPU(dpu, (uint8_t*)dpuNodePtrs_h, dpuNodePtrs_m, (dpuNumNodes + 1)*sizeof(uint32_t));
            copyToDPU(dpu, (uint8_t*)dpuNeighborIdxs_h, dpuNeighborIdxs_m, dpuNumNeighbors*sizeof(uint32_t));
            copyToDPU(dpu, (uint8_t*)&dpuTriangleCount, dpuTriangleCount_m, sizeof(uint32_t));
            stopTimer(&timer);
            loadTime += getElapsedTime(timer);

        }

        // Send parameters to DPU
        PRINT_INFO(p.verbosity >= 2, "        Copying parameters to DPU");
        startTimer(&timer);
        copyToDPU(dpu, (uint8_t*)&dpuParams[dpuIdx], dpuParams_m[dpuIdx], sizeof(struct DPUParams));
        stopTimer(&timer);
        loadTime += getElapsedTime(timer);

        ++dpuIdx;

    }
    PRINT_INFO(p.verbosity >= 1, "    CPU-DPU Time: %f ms", loadTime*1e3);

	#if ENERGY
	DPU_ASSERT(dpu_probe_start(&probe));
	#endif
        // Run all DPUs
        PRINT_INFO(p.verbosity >= 1, "    Booting DPUs");
        startTimer(&timer);
        DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
        stopTimer(&timer);
        dpuTime += getElapsedTime(timer);
        PRINT_INFO(p.verbosity >= 2, "    Level DPU Time: %f ms", getElapsedTime(timer)*1e3);
	#if ENERGY
    	DPU_ASSERT(dpu_probe_stop(&probe));
    	double energy;
    	DPU_ASSERT(dpu_probe_get(&probe, DPU_ENERGY, DPU_AVERAGE, &energy));
	tenergy += energy;
	#endif

    PRINT_INFO(p.verbosity >= 1, "DPU Kernel Time: %f ms", dpuTime*1e3);
    PRINT_INFO(p.verbosity >= 1, "Inter-DPU Time: %f ms", hostTime*1e3);
    #if ENERGY
    PRINT_INFO(p.verbosity >= 1, "    DPU Energy: %f J", tenergy);
    #endif

    // Copy back node levels
    PRINT_INFO(p.verbosity >= 1, "Copying back the result");
    startTimer(&timer);
    dpuIdx = 0;
    DPU_FOREACH (dpu_set, dpu) {
        uint32_t dpuNumNodes = dpuParams[dpuIdx].dpuNumNodes;
        PRINT_INFO(p.verbosity >= 2, "    DPU %u has %u nodes", dpuIdx, dpuNumNodes);
        if(dpuNumNodes > 0) {
            // uint32_t dpuStartNodeIdx = dpuIdx*numNodesPerDPU;
            uint32_t dpuTriangleCount = 0;
            copyFromDPU(dpu, dpuParams[dpuIdx].dpuTriangleCount_m, (uint8_t*) &(dpuTriangleCount), sizeof(uint32_t));
            PRINT_INFO(p.verbosity >= 1, "        DPU %u counted %u triangles", dpuIdx, dpuTriangleCount);
        }
        ++dpuIdx;
    }
    stopTimer(&timer);
    retrieveTime += getElapsedTime(timer);
    PRINT_INFO(p.verbosity >= 1, "    DPU-CPU Time: %f ms", retrieveTime*1e3);
    if(p.verbosity == 0) PRINT("CPU-DPU Time(ms): %f    DPU Kernel Time (ms): %f    Inter-DPU Time (ms): %f    DPU-CPU Time (ms): %f", loadTime*1e3, dpuTime*1e3, hostTime*1e3, retrieveTime*1e3);

    // Display DPU Logs
    if(p.verbosity >= 2) {
        PRINT_INFO(p.verbosity >= 2, "Displaying DPU Logs:");
        dpuIdx = 0;
        DPU_FOREACH (dpu_set, dpu) {
            PRINT("DPU %u:", dpuIdx);
            DPU_ASSERT(dpu_log_read(dpu, stdout));
            ++dpuIdx;
        }
    }

    // Deallocate data structures
    freeCOOGraph(cooGraph);
    freeCSRGraph(csrGraph);

    return 0;

}

