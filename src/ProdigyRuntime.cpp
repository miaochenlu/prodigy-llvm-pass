// Stub implementation for Prodigy runtime functions
// These functions are not called when using DIG_PRINT_MODE
// They exist only to satisfy linker requirements

#include "../include/ProdigyRuntime.h"

void registerNode(void* base_addr, uint64_t num_elements, uint32_t element_size, uint32_t node_id) {
    // Empty stub - actual functionality provided by printf macros in the LLVM pass
}

void registerTravEdge(void* src_addr, void* dest_addr, uint32_t edge_type) {
    // Empty stub - actual functionality provided by printf macros in the LLVM pass
}

void registerTrigEdge(void* trigger_addr, uint32_t prefetch_params) {
    // Empty stub - actual functionality provided by printf macros in the LLVM pass
} 