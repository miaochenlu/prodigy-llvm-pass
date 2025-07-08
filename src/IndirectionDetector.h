#ifndef INDIRECTION_DETECTOR_H
#define INDIRECTION_DETECTOR_H

/**
 * @file IndirectionDetector.h
 * @brief Detects data-dependent indirect memory access patterns for the DIG
 * 
 * This is the core pattern recognition component of the Prodigy pass. It identifies
 * the two types of indirection patterns described in the paper:
 * 
 * 1. Single-Valued Indirection (w0): A[B[i]] pattern
 *    Example: visited[edgeList[i]] in graph algorithms
 *    - Load a value from array B at index i
 *    - Use that loaded value as an index into array A
 *    - Common in: vertex property lookups, indirect array accesses
 * 
 * 2. Ranged Indirection (w1): Accessing A[B[i]] through A[B[i+1]]
 *    Example: for(j = offset[v]; j < offset[v+1]; j++) { process(edges[j]); }
 *    - Load two consecutive values from array B (at indices i and i+1)
 *    - Use these as bounds to access a range of elements in array A
 *    - Common in: CSR/CSC sparse matrix formats, adjacency list representations
 * 
 * The detector analyzes LLVM IR patterns including:
 * - GetElementPtr (GEP) instructions for array indexing
 * - Load instructions and their data dependencies
 * - Loop structures and bounds checking for ranged patterns
 * - Sign/zero extensions that often appear in index calculations
 * 
 * Challenges handled:
 * - Indirect accesses through multiple levels of pointers
 * - Optimizations that may obscure the original access pattern
 * - Type conversions and pointer arithmetic
 * - Distinguishing between regular array accesses and indirection patterns
 */

#include "AllocInfo.h"
#include "BasePointerTracker.h"
#include "llvm/IR/Function.h"
#include <vector>
#include <unordered_set>
#include <map>

namespace prodigy {

/**
 * @brief Detects indirection patterns in LLVM IR
 */
class IndirectionDetector {
private:
    BasePointerTracker* bpTracker;
    std::vector<IndirectionInfo> indirections;
    std::unordered_set<EdgeKey, EdgeKeyHash> detectedPatterns;
    std::unordered_set<EdgeKey, EdgeKeyHash> detectedRangedPatterns;
    
    /**
     * @brief Storage for patterns found in accessor functions
     */
    struct AccessorPattern {
        llvm::LoadInst* indexLoad;
        llvm::LoadInst* dataLoad;
        llvm::GetElementPtrInst* gep;
    };
    
    std::map<llvm::Function*, std::vector<AccessorPattern>> accessorPatterns;
    
    // Helper methods
    llvm::LoadInst* traceToLoad(llvm::Value* V);
    llvm::Value* getUltimateBase(llvm::Value* V);
    bool areConsecutiveArrayLoads(llvm::LoadInst* Load1, llvm::LoadInst* Load2);
    bool areLoadsUsedInBoundsCheck(llvm::LoadInst* StartLoad, llvm::LoadInst* EndLoad, 
                                   llvm::Function& F);
    bool findTargetArrayAccess(llvm::LoadInst* StartLoad, llvm::LoadInst* EndLoad,
                              llvm::Function& F);
    
    /**
     * @brief Connect accessor patterns with caller context
     */
    void connectAccessorPattern(llvm::CallInst* CI, llvm::Function* AccessorFunc);
    
    /**
     * @brief Detect BFS-specific patterns
     */
    void detectBFSPatterns(llvm::Function& F);
    
    /**
     * @brief Analyze uses of a loaded pointer
     */
    void analyzePointerUses(llvm::LoadInst* PtrLoad, llvm::Value* SourceArray, 
                           const std::map<unsigned, llvm::Value*>& allMembers);
    
    /**
     * @brief Check if a function should be analyzed inline
     */
    bool shouldAnalyzeInline(llvm::Function* F);
    
    /**
     * @brief Analyze a call site for potential indirections
     */
    void analyzeCallSite(llvm::CallInst* CI, llvm::Function* Callee,
                        const std::map<llvm::Value*, std::map<unsigned, llvm::Value*>>& structMembers);
    
    /**
     * @brief Analyze Neighborhood-related functions for graph patterns
     */
    void analyzeNeighborhoodFunction(llvm::CallInst* CI, llvm::Function* F);
    
    /**
     * @brief Map values through function arguments
     */
    llvm::Value* mapThroughArguments(llvm::Value* V, const std::map<llvm::Value*, llvm::Value*>& argMap);
    
    /**
     * @brief Detect ranged access patterns
     */
    void detectRangedAccessPattern(llvm::Function* F, llvm::LoadInst* StartLoad, 
                                  llvm::LoadInst* EndLoad,
                                  const std::map<llvm::Value*, llvm::Value*>& argMap);
    
    /**
     * @brief Create an indirection entry
     */
    void createIndirectionEntry(llvm::Value* SrcBase, llvm::Value* DestBase, 
                               llvm::Instruction* AccessInst, IndirectionType Type);
    
    /**
     * @brief Analyze usage of CSRGraph member arrays
     */
    void analyzeCSRGraphMemberUse(llvm::User* U, llvm::Value* MemberArray, 
                                  unsigned MemberNum, 
                                  const std::map<unsigned, llvm::Value*>& allMembers);
    
    /**
     * @brief Analyze calls to out_neigh or similar CSRGraph methods
     */
    void analyzeOutNeighCall(llvm::CallInst* CI, 
                            const std::map<llvm::Value*, std::map<unsigned, llvm::Value*>>& csrGraphMembers);
    
    /**
     * @brief Check if the two loads are used as bounds for accessing another array
     */
    void checkForRangedPattern(llvm::LoadInst* StartLoad, llvm::LoadInst* EndLoad);
    
    /**
     * @brief Find loads that might be ranged accesses
     */
    void findLoadsInRangedAccess(llvm::BasicBlock* BB, std::vector<llvm::LoadInst*>& loads);
    
    /**
     * @brief Check if a load access is part of a ranged pattern
     */
    bool isRangedAccess(llvm::LoadInst* Access, llvm::LoadInst* StartLoad, llvm::LoadInst* EndLoad);
    
    /**
     * @brief Check if a value is related to another value
     */
    bool isRelatedToValue(llvm::Value* V1, llvm::Value* V2);
    
    /**
     * @brief Helper to get node ID from base pointer
     */
    uint32_t getNodeIdFromBase(llvm::Value* base, const std::vector<AllocInfo>& allocations);
    
public:
    IndirectionDetector(BasePointerTracker* tracker);
    
    /**
     * @brief Identify single-valued indirection patterns (A[B[i]])
     */
    void identifySingleValuedIndirections(llvm::Function& F);
    
    /**
     * @brief Identify ranged indirection patterns
     */
    void identifyRangedIndirections(llvm::Function& F);
    
    /**
     * @brief Detect indirections across the entire module
     */
    void detectIndirectionsInModule(llvm::Module& M);
    
    /**
     * @brief Check if a function is a simple accessor suitable for analysis
     */
    bool isSimpleAccessorFunction(llvm::Function& F);
    
    /**
     * @brief Analyze accessor functions for indirection patterns
     */
    void analyzeAccessorFunction(llvm::Function& F);
    
    /**
     * @brief Detect all indirection patterns in a function
     */
    std::vector<IndirectionInfo> detectIndirections(llvm::Function& F, 
                                                   const std::vector<AllocInfo>& allocations);
    
    /**
     * @brief Get the list of detected indirections
     */
    const std::vector<IndirectionInfo>& getIndirections() const { return indirections; }
    
    /**
     * @brief Clear detected indirections
     */
    void clearIndirections() { indirections.clear(); }
};

} // namespace prodigy

#endif // INDIRECTION_DETECTOR_H 