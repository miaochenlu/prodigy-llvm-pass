#ifndef BASE_POINTER_TRACKER_H
#define BASE_POINTER_TRACKER_H

/**
 * @file BasePointerTracker.h
 * @brief Tracks base pointers for memory allocations in the DIG
 * 
 * This component is responsible for mapping LLVM Values (pointers) back to their
 * original allocation sites. This is crucial for the Prodigy pass because:
 * 
 * 1. Indirection patterns often involve pointer arithmetic (GEPs) and we need to
 *    identify which allocation/node is being accessed
 * 
 * 2. The same allocation might be accessed through different pointers (e.g., after
 *    pointer arithmetic or passing through function parameters)
 * 
 * 3. We need to distinguish between different allocations to correctly identify
 *    edges in the DIG
 * 
 * The tracker follows pointer chains through:
 * - GetElementPtr instructions (GEPs)
 * - Load/Store operations
 * - PHI nodes
 * - Bitcasts and other type conversions
 * 
 * Special handling is provided for struct field accesses where multiple fields
 * might contain pointers to different allocations.
 */

#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"
#include <unordered_map>
#include <cstdint>

namespace prodigy {

/**
 * @brief Tracks base pointers for memory allocations
 */
class BasePointerTracker {
private:
    std::unordered_map<llvm::Value*, uint32_t> ptrToNodeId;
    
public:
    BasePointerTracker() = default;
    
    /**
     * @brief Register a pointer with its node ID
     */
    void registerPointer(llvm::Value* ptr, uint32_t nodeId) {
        ptrToNodeId[ptr] = nodeId;
    }
    
    /**
     * @brief Check if a pointer is registered
     */
    bool isRegistered(llvm::Value* ptr) const {
        return ptrToNodeId.find(ptr) != ptrToNodeId.end();
    }
    
    /**
     * @brief Get node ID for a pointer
     */
    uint32_t getNodeId(llvm::Value* ptr) const {
        auto it = ptrToNodeId.find(ptr);
        return (it != ptrToNodeId.end()) ? it->second : UINT32_MAX;
    }
    
    /**
     * @brief Get the base pointer for a value (following loads, GEPs, etc.)
     */
    llvm::Value* getBasePointer(llvm::Value* ptr);
    
    // Debug method to get all registered pointers
    const std::unordered_map<llvm::Value*, uint32_t>& getRegisteredPointers() const {
        return ptrToNodeId;
    }
    
    // Helper to check if two GEPs access similar struct fields
    bool areGEPsSimilar(llvm::GetElementPtrInst *GEP1, llvm::GetElementPtrInst *GEP2);

private:
    /**
     * @brief Find allocation for a struct field access
     */
    llvm::Value* findStructFieldAllocation(llvm::GetElementPtrInst *FieldGEP);
};

} // namespace prodigy

#endif // BASE_POINTER_TRACKER_H 