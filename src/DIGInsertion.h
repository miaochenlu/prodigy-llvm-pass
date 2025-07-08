#ifndef DIG_INSERTION_H
#define DIG_INSERTION_H

/**
 * @file DIGInsertion.h
 * @brief Handles insertion of DIG registration calls into the LLVM IR
 * 
 * This component is responsible for instrumenting the application with calls
 * to communicate the DIG (Data Indirection Graph) to the hardware prefetcher.
 * 
 * In normal operation mode, it inserts calls to runtime functions:
 * - registerNode(base_ptr, num_elements, element_size, node_id)
 * - registerTravEdge(src_ptr, dest_ptr, traversal_function)
 * - registerTrigEdge(node_ptr, trigger_function)
 * 
 * In DIG_PRINT_MODE (used for debugging/analysis), it instead inserts printf
 * calls that output the DIG configuration in text format:
 * - NODE <id> <base_addr> <num_elements> <element_size>
 * - EDGE <src_id> <dest_id> <traversal_function>
 * - TRIGGER <src_id> <dest_id> <trigger_function> <squash_function>
 * 
 * Key responsibilities:
 * 1. Determining where to insert registration calls (typically after allocations
 *    for nodes, and at function entry for edges)
 * 
 * 2. Selecting appropriate traversal functions based on indirection type:
 *    - BaseOffset32/64 for single-valued indirection
 *    - PointerBounds32/64 for ranged indirection
 * 
 * 3. Selecting trigger functions based on DIG depth:
 *    - Deeper DIGs use smaller look-ahead distances
 *    - According to paper: depth >= 4 uses look-ahead of 1
 * 
 * 4. Ensuring registrations happen exactly once (using global guards in print mode)
 * 
 * 5. Maintaining proper ordering: nodes before edges before triggers
 */

#include "AllocInfo.h"
#include "BasePointerTracker.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include <vector>
#include <unordered_set>

namespace prodigy {

/**
 * @brief Handles insertion of DIG registration calls
 */
class DIGInsertion {
private:
    // Runtime function declarations
    llvm::Function* printfFunc = nullptr;
    llvm::Function* registerNodeFunc = nullptr;
    llvm::Function* registerTravEdgeFunc = nullptr;
    llvm::Function* registerTrigEdgeFunc = nullptr;
    
public:
    DIGInsertion();
    
    /**
     * @brief Initialize runtime functions and format strings
     */
    void initializeRuntimeFunctions(llvm::Module& module);
    
    /**
     * @brief Insert global DIG header in main function
     */
    void insertGlobalDIGHeader(llvm::Module& module);
    
    /**
     * @brief Insert DIG header at the beginning of a function
     */
    void insertDIGHeader(llvm::Function& F);
    
    /**
     * @brief Insert runtime calls for DIG generation
     */
    void insertRuntimeCalls(llvm::Function& F,
                          const std::vector<AllocInfo>& allocations,
                          const std::vector<IndirectionInfo>& indirections,
                          std::unordered_set<EdgeKey, EdgeKeyHash>& registeredEdges);
    
    /**
     * @brief Get traversal function ID based on edge type
     */
    static uint32_t getTraversalFunctionId(IndirectionType type);
    
    /**
     * @brief Get trigger function ID based on allocation
     */
    static uint32_t getTriggerFunctionId();
    
    /**
     * @brief Get squash function ID
     */
    static uint32_t getSquashFunctionId();
    
    /**
     * @brief Select trigger function based on node ID
     */
    uint32_t getTriggerFunctionForNode(uint32_t nodeId,
                                      const std::vector<AllocInfo>& allocations,
                                      const std::vector<IndirectionInfo>& indirections);
    
    /**
     * @brief Calculate DIG depth from a node
     */
    int calculateDIGDepthFromNode(uint32_t nodeId,
                                 const std::vector<IndirectionInfo>& indirections);
    
private:
    /**
     * @brief Insert node registrations
     */
    void insertNodeRegistrations(llvm::Function& F, const std::vector<AllocInfo>& allocations);
    
    /**
     * @brief Insert edge registrations
     */
    void insertEdges(llvm::Function& F, 
                     const std::vector<IndirectionInfo>& indirections,
                     std::unordered_set<EdgeKey, EdgeKeyHash>& registeredEdges,
                     llvm::Instruction* insertAfter = nullptr);
    
    /**
     * @brief Insert trigger edges
     */
    void insertTriggerEdges(llvm::Function& F, const std::vector<AllocInfo>& allocations,
                          const std::vector<IndirectionInfo>& indirections,
                          std::unordered_set<EdgeKey, EdgeKeyHash>& registeredEdges);
};

} // namespace prodigy

#endif // DIG_INSERTION_H 