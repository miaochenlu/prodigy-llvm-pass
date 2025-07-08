#ifndef PRODIGY_TYPES_H
#define PRODIGY_TYPES_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace prodigy {

// Function IDs based on Prodigy documentation
namespace FunctionId {
    // Traversal Functions (0-4)
    constexpr uint32_t BaseOffset32 = 0;
    constexpr uint32_t BaseOffset64 = 1;
    constexpr uint32_t PointerBounds32 = 2;
    constexpr uint32_t PointerBounds64 = 3;
    constexpr uint32_t TraversalHolder = 4;

    // Trigger Functions (5-21)
    constexpr uint32_t UpToOffset = 5;
    constexpr uint32_t StaticOffset_1 = 6;
    constexpr uint32_t StaticOffset_2 = 7;
    constexpr uint32_t StaticOffset_4 = 8;
    constexpr uint32_t StaticOffset_8 = 9;
    constexpr uint32_t StaticOffset_16 = 10;
    constexpr uint32_t StaticOffset_32 = 11;
    constexpr uint32_t StaticOffset_64 = 12;
    constexpr uint32_t TriggerHolder = 13;
    constexpr uint32_t StaticUpToOffset_8_16 = 14;
    constexpr uint32_t StaticOffset_256 = 15;
    constexpr uint32_t StaticOffset_512 = 16;
    constexpr uint32_t StaticOffset_1024 = 17;
    constexpr uint32_t StaticOffset_2_reverse = 18;
    constexpr uint32_t StaticOffset_4_reverse = 19;
    constexpr uint32_t StaticOffset_8_reverse = 20;
    constexpr uint32_t StaticOffset_16_reverse = 21;

    // Squash Functions (22-24)
    constexpr uint32_t SquashIfLarger = 22;
    constexpr uint32_t SquashIfSmaller = 23;
    constexpr uint32_t NeverSquash = 24;

    constexpr uint32_t InvalidFunc = 25;
}

} // namespace prodigy

#endif // PRODIGY_TYPES_H 