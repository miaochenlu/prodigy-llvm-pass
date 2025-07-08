#ifndef ELEMENT_SIZE_INFERENCE_H
#define ELEMENT_SIZE_INFERENCE_H

/**
 * @file ElementSizeInference.h  
 * @brief Infers element sizes for memory allocations
 * 
 * Element size inference is critical for the Prodigy prefetcher because it needs
 * to know the stride between array elements to correctly calculate prefetch addresses.
 * 
 * This component uses multiple strategies to infer element sizes:
 * 
 * 1. Direct analysis of allocation calls:
 *    - malloc(count * sizeof(type)) patterns
 *    - calloc(count, size) provides size directly
 *    - C++ new[] operators with known types
 * 
 * 2. Usage pattern analysis:
 *    - GEP (GetElementPtr) stride patterns
 *    - Loop access patterns with induction variables
 *    - Store/Load instruction types
 * 
 * 3. SCEV (Scalar Evolution) analysis:
 *    - Analyzes loop trip counts and access patterns
 *    - Identifies stride patterns in complex loops
 * 
 * The inferred element size is stored in the AllocInfo structure and used
 * when generating NODE registration calls. Accurate element size inference
 * is essential for ranged indirection patterns where we need to iterate
 * through consecutive array elements.
 */

#include "AllocInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DataLayout.h"
#include <vector>

namespace prodigy {

/**
 * @brief Infers element sizes for memory allocations
 */
class ElementSizeInference {
private:
    const llvm::DataLayout* DL;
    llvm::ScalarEvolution* SE;
    
public:
    ElementSizeInference() : DL(nullptr), SE(nullptr) {}
    
    ElementSizeInference(const llvm::DataLayout* dl, llvm::ScalarEvolution* se) 
        : DL(dl), SE(se) {}
    
    /**
     * @brief Main element size inference dispatcher
     */
    void inferElementSize(AllocInfo& info);
    
    /**
     * @brief Infer element size from malloc calls
     */
    void inferElementSizeFromMalloc(AllocInfo& info);
    
    /**
     * @brief Handle calloc which directly provides count and size
     */
    void inferElementSizeFromCalloc(AllocInfo& info);
    
    /**
     * @brief Handle C++ new/new[] operators
     */
    void inferElementSizeFromNew(AllocInfo& info);
    
private:
    /**
     * @brief Analyze malloc argument patterns (n*size, n<<shift, etc.)
     */
    bool analyzeAllocationArgument(llvm::Value* sizeArg, AllocInfo& info);
    
    /**
     * @brief Analyze how allocated memory is used to infer element size
     */
    bool analyzeUsagePatterns(AllocInfo& info);
    
    /**
     * @brief Analyze GEP instruction patterns to detect stride
     */
    bool analyzeGEPStrides(const std::vector<llvm::GetElementPtrInst*>& geps, AllocInfo& info);
    
    /**
     * @brief Use SCEV analysis for complex access patterns
     */
    bool analyzeSCEVPatterns(AllocInfo& info);
    
    /**
     * @brief Analyze loop patterns for element size inference
     */
    bool analyzeLoopPatterns(AllocInfo& info, 
                           const std::vector<llvm::LoadInst*>& loads,
                           const std::vector<llvm::StoreInst*>& stores);
    
    /**
     * @brief Check if a value is related to an induction variable
     */
    bool isRelatedToInductionVariable(llvm::Value* V, llvm::PHINode* IndVar);
    
    /**
     * @brief Recursively collect memory access instructions
     */
    void collectAccessInstructions(llvm::Value* V, std::vector<llvm::Instruction*>& accesses);
    
    /**
     * @brief (Work-in-progress) detect struct-field style allocations; currently unused but defined.
     */
    void inferFromStructAllocation(AllocInfo& info);
};

} // namespace prodigy

#endif // ELEMENT_SIZE_INFERENCE_H 