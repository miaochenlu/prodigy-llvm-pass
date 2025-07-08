#ifndef ALLOC_INFO_H
#define ALLOC_INFO_H

/**
 * @file AllocInfo.h
 * @brief Core data structures for the Prodigy DIG representation
 * 
 * This header defines the fundamental data structures used to represent
 * the Data Indirection Graph (DIG) during compilation.
 * 
 * Key structures:
 * 
 * 1. AllocInfo - Represents a node in the DIG:
 *    - Corresponds to a memory allocation (malloc, calloc, new)
 *    - Stores allocation properties needed for prefetching:
 *      - Base pointer (memory address)
 *      - Number of elements in the allocation
 *      - Size of each element (for stride calculation)
 *      - Unique node ID for DIG representation
 *    - Tracks both compile-time constants and runtime values
 * 
 * 2. IndirectionInfo - Represents an edge in the DIG:
 *    - Captures data-dependent memory access patterns
 *    - Stores source and destination nodes
 *    - Identifies the type of indirection (single-valued or ranged)
 *    - Links to the actual access instruction for context
 * 
 * 3. EdgeKey - Used for deduplication of edges:
 *    - Ensures each unique edge is registered only once
 *    - Based on source/destination pointers and indirection type
 * 
 * These structures form the intermediate representation that bridges
 * the LLVM analysis and the final DIG registration calls.
 */

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "ProdigyTypes.h"
#include <cstdint>

namespace prodigy {

/**
 * @brief Information about a memory allocation
 */
struct AllocInfo {
    llvm::CallInst *allocCall;          // The allocation call instruction
    llvm::Value *basePtr;               // Base pointer returned by allocation
    llvm::Value *numElements;           // Number of elements (may be dynamic)
    llvm::Value *elementSize;           // Size of each element (may be dynamic)
    uint32_t nodeId;                    // Unique node ID in the DIG
    bool registered = false;            // Whether runtime registration is done
    
    // Enhanced metadata for element size inference
    llvm::Type *inferredElementType = nullptr;
    int64_t constantElementSize = -1;  // -1 means unknown
    int64_t constantNumElements = -1;  // -1 means unknown
};

/**
 * @brief Enumeration for indirection types
 */
enum class IndirectionType {
    SingleValued = 0,
    Ranged = 1
};

/**
 * @brief Information about an indirection pattern
 */
struct IndirectionInfo {
    IndirectionType indirectionType;
    llvm::Value* srcBase;      // Base pointer of source array
    llvm::Value* destBase;     // Base pointer of destination array  
    llvm::Instruction* accessInst;  // The instruction performing the indirect access
    uint32_t srcNodeId;        // Node ID of source
    uint32_t destNodeId;       // Node ID of destination
};

/**
 * @brief Key for tracking unique edges
 */
struct EdgeKey {
    llvm::Value* srcBase;
    llvm::Value* destBase;
    IndirectionType type;
    
    EdgeKey(llvm::Value* src, llvm::Value* dest, IndirectionType t)
        : srcBase(src), destBase(dest), type(t) {}
    
    bool operator==(const EdgeKey& other) const {
        return srcBase == other.srcBase && 
               destBase == other.destBase && 
               type == other.type;
    }
};

/**
 * @brief Hash function for EdgeKey
 */
struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& key) const {
        return std::hash<void*>()(key.srcBase) ^ 
               std::hash<void*>()(key.destBase) ^ 
               std::hash<int>()(static_cast<int>(key.type));
    }
};

} // namespace prodigy

#endif // ALLOC_INFO_H 