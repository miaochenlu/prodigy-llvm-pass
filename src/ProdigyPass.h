#ifndef PRODIGY_PASS_H
#define PRODIGY_PASS_H

/**
 * @file ProdigyPass.h
 * @brief Main LLVM pass for Prodigy hardware-software co-design prefetcher
 * 
 * This pass implements the compiler analysis described in the Prodigy paper:
 * "Prodigy: Improving the Memory Latency of Data-Indirect Irregular Workloads 
 * Using Hardware-Software Co-Design" (HPCA 2021)
 * 
 * The pass performs three main tasks:
 * 1. Node Identification: Detects memory allocations (malloc, calloc, new) and
 *    extracts their properties (base address, number of elements, element size)
 * 
 * 2. Edge Detection: Identifies two types of data-dependent indirect memory accesses:
 *    - Single-valued indirection (w0): A[B[i]] pattern where data from one array
 *      is used to index into another array
 *    - Ranged indirection (w1): Accessing elements A[B[i]] to A[B[i+1]], commonly
 *      used in CSR/CSC sparse matrix representations
 * 
 * 3. Trigger Edge Identification: Nodes without incoming edges get trigger edges
 *    (self-edges) that initialize prefetch sequences. The trigger function is
 *    selected based on the DIG depth from that node.
 * 
 * The pass generates a Data Indirection Graph (DIG) representation that is
 * communicated to the hardware prefetcher through runtime API calls:
 * - registerNode(base_ptr, num_elements, element_size, node_id)
 * - registerTravEdge(src_addr, dest_addr, edge_type) 
 * - registerTrigEdge(addr, trigger_function)
 * 
 * In DIG_PRINT_MODE, these calls are replaced with printf statements that
 * output the DIG configuration in a text format for debugging/analysis.
 */

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "AllocInfo.h"
#include "BasePointerTracker.h"
#include "ElementSizeInference.h"
#include "IndirectionDetector.h"
#include "DIGInsertion.h"
#include <memory>

namespace prodigy {

/**
 * @brief Main LLVM pass for Prodigy prefetcher configuration
 */
class ProdigyPass : public llvm::ModulePass {
public:
    static char ID;
    
    ProdigyPass();
    ~ProdigyPass();
    
    bool runOnModule(llvm::Module& M) override;
    
    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
    
    const char* getPassName() const override {
        return "Prodigy DIG Construction Pass";
    }
    
private:
    // State management
    bool modified = false;
    const llvm::DataLayout* DL = nullptr;
    llvm::ScalarEvolution* SE = nullptr;
    
    // Global data structures
    std::vector<AllocInfo> globalAllocations;
    std::unordered_map<llvm::Function*, std::vector<IndirectionInfo>> globalIndirections;
    std::unordered_set<EdgeKey, EdgeKeyHash> registeredEdges;
    std::unordered_map<llvm::Value*, AllocInfo*> basePtrMap; // track unique allocations
    std::unordered_set<EdgeKey, EdgeKeyHash> detectedRangedPatterns;
    uint32_t nextNodeId = 0;
    
    // Components
    BasePointerTracker *pointerTracker;
    ElementSizeInference *elementSizeInference;
    IndirectionDetector *indirectionDetector;
    DIGInsertion *digInsertion;
    
    /**
     * @brief Process a single function
     */
    void processFunction(llvm::Function& F);
    
    /**
     * @brief Collect memory allocations in a function
     */
    void collectAllocations(llvm::Function& F);
    
    /**
     * @brief Handle a single allocation call
     */
    void handleAllocation(llvm::CallInst* CI);
    
    /**
     * @brief Check if we should filter out an allocation
     */
    bool shouldFilterAllocation(llvm::CallInst* CI);
    
    void detectIndirections(llvm::Function &F);
    void insertDIGCalls(llvm::Function &F);
    bool shouldTrackAllocation(llvm::CallInst *CI);
};

} // namespace prodigy

#endif // PRODIGY_PASS_H 