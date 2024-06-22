
#ifndef _GRAPH_H_
#define _GRAPH_H_

#include <assert.h>
#include <stdio.h>

#include "common.h"
#include "utils.h"

struct COOGraph {
    uint32_t numNodes;
    uint32_t numEdges;
    uint32_t* nodeIdxs;
    uint32_t* neighborIdxs;
};

struct CSRGraph {
    uint32_t numNodes;
    uint32_t numEdges;
    uint32_t* nodePtrs;
    uint32_t* neighborIdxs;
};

static struct COOGraph readCOOGraph(const char* fileName) {

    struct COOGraph cooGraph;

    // Initialize fields
    FILE* fp = fopen(fileName, "r");
    uint32_t numNodes, numCols;
    assert(fscanf(fp, "%u", &numNodes));
    assert(fscanf(fp, "%u", &numCols));
    if(numNodes == numCols) {
        cooGraph.numNodes = numNodes;
    } else {
        PRINT_WARNING("    Adjacency matrix is not square. Padding matrix to be square.");
        cooGraph.numNodes = (numNodes > numCols)? numNodes : numCols;
    }
    if(cooGraph.numNodes%64 != 0) {
        PRINT_WARNING("    Adjacency matrix dimension is %u which is not a multiple of 64 nodes.", cooGraph.numNodes);
        cooGraph.numNodes += (64 - cooGraph.numNodes%64);
        PRINT_WARNING("        Padding to %u which is a multiple of 64 nodes.", cooGraph.numNodes);
    }
    assert(fscanf(fp, "%u", &cooGraph.numEdges));
    cooGraph.nodeIdxs = (uint32_t*) malloc(cooGraph.numEdges*sizeof(uint32_t));
    cooGraph.neighborIdxs = (uint32_t*) malloc(cooGraph.numEdges*sizeof(uint32_t));

    // Read the neighborIdxs
    for(uint32_t edgeIdx = 0; edgeIdx < cooGraph.numEdges; ++edgeIdx) {
        uint32_t nodeIdx;
        assert(fscanf(fp, "%u", &nodeIdx));
        cooGraph.nodeIdxs[edgeIdx] = nodeIdx;
        uint32_t neighborIdx;
        assert(fscanf(fp, "%u", &neighborIdx));
        cooGraph.neighborIdxs[edgeIdx] = neighborIdx;
    }

    return cooGraph;

}

static void freeCOOGraph(struct COOGraph cooGraph) {
    free(cooGraph.nodeIdxs);
    free(cooGraph.neighborIdxs);
}

static struct CSRGraph coo2csr(struct COOGraph cooGraph) {

    struct CSRGraph csrGraph;

    // Initialize fields
    csrGraph.numNodes = cooGraph.numNodes;
    csrGraph.numEdges = cooGraph.numEdges;
    csrGraph.nodePtrs = (uint32_t*) calloc(ROUND_UP_TO_MULTIPLE_OF_2(csrGraph.numNodes + 1), sizeof(uint32_t));
    csrGraph.neighborIdxs = (uint32_t*)malloc(ROUND_UP_TO_MULTIPLE_OF_8(csrGraph.numEdges*sizeof(uint32_t)));

    // Histogram nodeIdxs
    for(uint32_t i = 0; i < cooGraph.numEdges; ++i) {
        uint32_t nodeIdx = cooGraph.nodeIdxs[i];
        csrGraph.nodePtrs[nodeIdx]++;
    }

    // Prefix sum nodePtrs
    uint32_t sumBeforeNextNode = 0;
    for(uint32_t nodeIdx = 0; nodeIdx < csrGraph.numNodes; ++nodeIdx) {
        uint32_t sumBeforeNode = sumBeforeNextNode;
        sumBeforeNextNode += csrGraph.nodePtrs[nodeIdx];
        csrGraph.nodePtrs[nodeIdx] = sumBeforeNode;
    }
    csrGraph.nodePtrs[csrGraph.numNodes] = sumBeforeNextNode;

    // Bin the neighborIdxs
    for(uint32_t i = 0; i < cooGraph.numEdges; ++i) {
        uint32_t nodeIdx = cooGraph.nodeIdxs[i];
        uint32_t neighborListIdx = csrGraph.nodePtrs[nodeIdx]++;
        csrGraph.neighborIdxs[neighborListIdx] = cooGraph.neighborIdxs[i];
    }

    // Restore nodePtrs
    for(uint32_t nodeIdx = csrGraph.numNodes - 1; nodeIdx > 0; --nodeIdx) {
        csrGraph.nodePtrs[nodeIdx] = csrGraph.nodePtrs[nodeIdx - 1];
    }
    csrGraph.nodePtrs[0] = 0;

    return csrGraph;

}

static void freeCSRGraph(struct CSRGraph csrGraph) {
    free(csrGraph.nodePtrs);
    free(csrGraph.neighborIdxs);
}

static struct CSRGraph read_graph_data(const char *filename) {
    struct CSRGraph csrGraph;

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int count, index;
    char *token;

    // Read nodePtrs_test
    if ((read = getline(&line, &len, file)) != -1) {
        count = 0;
        token = strtok(line, " ");
        while (token != NULL) {
            count++;
            token = strtok(NULL, " ");
        }

        // Allocate memory for nodePtrs
        csrGraph.nodePtrs = (uint32_t*) calloc(ROUND_UP_TO_MULTIPLE_OF_2(count), sizeof(uint32_t));
        if (csrGraph.nodePtrs == NULL) {
            fprintf(stderr, "Memory allocation failed for nodePtrs\n");
            exit(EXIT_FAILURE);
        }

        // Fill nodePtrs array
        fseek(file, 0, SEEK_SET);
        getline(&line, &len, file);
        token = strtok(line, " ");
        index = 0;
        while (token != NULL) {
            csrGraph.nodePtrs[index++] = atoi(token);
            token = strtok(NULL, " ");
        }
        csrGraph.numNodes = count - 1;
    }

    // Read neighborIdxs_test
     if ((read = getline(&line, &len, file)) != -1) {
        count = 0;
        token = strtok(line, " ");
        while (token != NULL) {
            count++;
            token = strtok(NULL, " ");
        }

        // Allocate memory for neighborIdxs
        csrGraph.neighborIdxs = (uint32_t*)malloc(ROUND_UP_TO_MULTIPLE_OF_8(count*sizeof(uint32_t)));
        if (csrGraph.neighborIdxs == NULL) {
            fprintf(stderr, "Memory allocation failed for neighborIdxs\n");
            exit(EXIT_FAILURE);
        }

        // Fill neighborIdxs array
        fseek(file, 0, SEEK_SET);
        getline(&line, &len, file);  // Skip the nodePtrs line
        getline(&line, &len, file);
        token = strtok(line, " ");
        index = 0;
        while (token != NULL) {
            csrGraph.neighborIdxs[index++] = atoi(token);
            token = strtok(NULL, " ");
        }
        csrGraph.numEdges = count;
    }

    fclose(file);
    if (line) {
        free(line);
    }
    return csrGraph;
}

#endif

