// DIG Print Macros for GAPBS
// Converts Prodigy DIG registration calls to printf output for file generation

#ifndef DIG_PRINT_MACROS_H
#define DIG_PRINT_MACROS_H

#include <cstdio>
#include <cstdint>

// Control macro to enable/disable DIG printf output
#ifndef DIG_PRINT_MODE
#define DIG_PRINT_MODE 1  // 0: normal registration, 1: printf output
#endif

#define DIG_PRINT_MODE 1

// Helper macro to get element size
#define DIG_ELEM_SIZE(ptr) sizeof(*ptr)

// Define function IDs based on Prodigy documentation
// Traversal Functions (0-4)
#define BaseOffset32 0
#define BaseOffset64 1
#define PointerBounds32 2
#define PointerBounds64 3
#define TraversalHolder 4

// Trigger Functions (11-16)
#define UpToOffset 5
#define StaticOffset_1 6
#define StaticOffset_2 7
#define StaticOffset_4 8
#define StaticOffset_8 9
#define StaticOffset_16 10
#define StaticOffset_32 11
#define StaticOffset_64 12
#define TriggerHolder 13
#define StaticUpToOffset_8_16 14
#define StaticOffset_256 15
#define StaticOffset_512 16
#define StaticOffset_1024 17
#define StaticOffset_2_reverse 18
#define StaticOffset_4_reverse 19
#define StaticOffset_8_reverse 20
#define StaticOffset_16_reverse 21

// Squash Functions (22-24)
#define SquashIfLarger 22
#define SquashIfSmaller 23
#define NeverSquash 24

#define InvalidFunc 25

// Traversal function name mapping
#define DIG_FUNC_NAME(func_id) \
    ((func_id) == 0 ? "BaseOffset32" : \
     (func_id) == 1 ? "BaseOffset64" : \
     (func_id) == 2 ? "PointerBounds32" : \
     (func_id) == 3 ? "PointerBounds64" : \
     (func_id) == 4 ? "TraversalHolder" : \
     "InvalidTraversal")

// Trigger function name mapping  
#define DIG_TRIGGER_NAME(func_id) \
    ((func_id) == 5 ? "UpToOffset" : \
     (func_id) == 6 ? "StaticOffset_1" : \
     (func_id) == 7 ? "StaticOffset_2" : \
     (func_id) == 8 ? "StaticOffset_4" : \
     (func_id) == 9 ? "StaticOffset_8" : \
     (func_id) == 10 ? "StaticOffset_16" : \
     (func_id) == 11 ? "StaticOffset_32" : \
     (func_id) == 12 ? "StaticOffset_64" : \
     (func_id) == 13 ? "TriggerHolder" : \
     (func_id) == 14 ? "StaticUpToOffset_8_16" : \
     (func_id) == 15 ? "StaticOffset_256" : \
     (func_id) == 16 ? "StaticOffset_512" : \
     (func_id) == 17 ? "StaticOffset_1024" : \
     (func_id) == 18 ? "StaticOffset_2_reverse" : \
     (func_id) == 19 ? "StaticOffset_4_reverse" : \
     (func_id) == 20 ? "StaticOffset_8_reverse" : \
     (func_id) == 21 ? "StaticOffset_16_reverse" : \
     "InvalidTrigger")

// Squash function name mapping
#define DIG_SQUASH_NAME(func_id) \
    ((func_id) == 22 ? "SquashIfLarger" : \
     (func_id) == 23 ? "SquashIfSmaller" : \
     (func_id) == 24 ? "NeverSquash" : \
     "InvalidSquash")

#if DIG_PRINT_MODE == 1

// Printf versions of DIG registration functions

#define DIG_REGISTER_NODE(ptr, size, id) \
    do { \
        printf("NODE %d 0x%lx %ld %ld\n", \
               (int)(id), \
               (uint64_t)(ptr), \
               (int64_t)(size), \
               (int64_t)sizeof(*(ptr))); \
    } while(0)

// node, array_len, element_size, id
#define DIG_REGISTER_NODE_WITH_SIZE(ptr, size, elem_size, id) \
    do { \
        printf("NODE %d 0x%lx %ld %ld\n", \
               (int)(id), \
               (uint64_t)(ptr), \
               (int64_t)(size), \
               (int64_t)(elem_size)); \
    } while(0)

#define DIG_REGISTER_TRAV_EDGE(from_id, to_id, func) \
    do { \
        printf("EDGE %d %d %d  # %s\n", \
               (int)(from_id), \
               (int)(to_id), \
               (int)(func), \
               DIG_FUNC_NAME(func)); \
    } while(0)

#define DIG_REGISTER_TRIG_EDGE(from_id, to_id, trigger_func, squash_func) \
    do { \
        printf("TRIGGER %d %d %d %d  # %s, %s\n", \
               (int)(from_id), \
               (int)(to_id), \
               (int)(trigger_func), \
               (int)(squash_func), \
               DIG_TRIGGER_NAME(trigger_func), \
               DIG_SQUASH_NAME(squash_func)); \
    } while(0)

// Version with return value for trigger edge ID
#define DIG_REGISTER_TRIG_EDGE_WITH_ID(from_id, to_id, trigger_func, squash_func, edge_id) \
    do { \
        printf("TRIGGER %d %d %d %d  # %s, %s\n", \
               (int)(from_id), \
               (int)(to_id), \
               (int)(trigger_func), \
               (int)(squash_func), \
               DIG_TRIGGER_NAME(trigger_func), \
               DIG_SQUASH_NAME(squash_func)); \
        edge_id = ((uint64_t)(from_id) * 16) + (uint64_t)(to_id); \
    } while(0)

#else

// Normal registration mode - no params needed for printf mode
// These macros are no-ops when not in printf mode

#define DIG_REGISTER_NODE(ptr, size, id) do {} while(0)

#define DIG_REGISTER_NODE_WITH_SIZE(ptr, size, elem_size, id) do {} while(0)

#define DIG_REGISTER_TRAV_EDGE(from_id, to_id, func) do {} while(0)

#define DIG_REGISTER_TRIG_EDGE(from_id, to_id, trigger_func, squash_func) do {} while(0)

#define DIG_REGISTER_TRIG_EDGE_WITH_ID(from_id, to_id, trigger_func, squash_func, edge_id) do {} while(0)

#endif // DIG_PRINT_MODE

// Additional helper macros for common patterns

// Register a graph's vertex and edge arrays
#define DIG_REGISTER_GRAPH_VERTICES(graph, vertex_id, edge_id) \
    do { \
        DIG_REGISTER_NODE((graph).out_index_, (graph).num_nodes()+1, vertex_id); \
        DIG_REGISTER_NODE((graph).out_neighbors_, (graph).num_edges(), edge_id); \
    } while(0)

// Register a worklist/frontier
#define DIG_REGISTER_WORKLIST(worklist, size, id) \
    DIG_REGISTER_NODE(&(*(worklist).begin()), size, id)

// Register a simple array
#define DIG_REGISTER_ARRAY(array, size, id) \
    DIG_REGISTER_NODE(&(*(array).begin()), size, id)

// Print DIG configuration header
#define DIG_PRINT_HEADER(graph_name) \
    do { \
        if (DIG_PRINT_MODE == 1) { \
            printf("# DIG Configuration for %s\n", graph_name); \
            printf("# Generated from GAPBS\n\n"); \
        } \
    } while(0)

// Print DIG configuration comment
#define DIG_PRINT_COMMENT(comment) \
    do { \
        if (DIG_PRINT_MODE == 1) { \
            printf("# %s\n", comment); \
        } \
    } while(0)

#endif // DIG_PRINT_MACROS_H 