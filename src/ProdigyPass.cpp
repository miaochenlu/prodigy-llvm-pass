#include "ProdigyPass.h"
#include "ProdigyTypes.h"
#include "AllocInfo.h"
#include "BasePointerTracker.h"
#include "ElementSizeInference.h"
#include "IndirectionDetector.h"
#include "DIGInsertion.h"

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
// MemorySSA not available in LLVM 3.4.2
// #include "llvm/Analysis/MemorySSA.h"
// #include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Constants.h"
// Not available in LLVM 3.4.2:
// #include "llvm/IR/Dominators.h" 
#include "llvm/IR/DataLayout.h"
// #include "llvm/IR/PatternMatch.h"
#include "llvm/Analysis/ValueTracking.h"
// #include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Support/CFG.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <algorithm>
#include <queue>
#include <set>
#include <cstdio>
#include <memory>

// Include dig_print.h for the macro definitions
// This enables printf-based DIG output
#define DIG_PRINT_MODE 1
#include "dig_print.h"

using namespace llvm;
// using namespace llvm::PatternMatch;  // Not available in LLVM 3.4.2

namespace prodigy {

char ProdigyPass::ID = 0;

ProdigyPass::ProdigyPass() : ModulePass(ID) {}

ProdigyPass::~ProdigyPass() {
    delete pointerTracker;
    delete elementSizeInference;
    delete indirectionDetector;
    delete digInsertion;
}

bool ProdigyPass::runOnModule(Module &M) {
    errs() << "\n========================================\n";
    errs() << "Running Prodigy Pass\n";
    errs() << "========================================\n\n";
    
    // Initialize components
    pointerTracker = new BasePointerTracker();
    elementSizeInference = new ElementSizeInference();
    indirectionDetector = new IndirectionDetector(pointerTracker);
    digInsertion = new DIGInsertion();
    
    // Initialize runtime functions for DIGInsertion
    digInsertion->initializeRuntimeFunctions(M);
    
    // Clear global state
    globalAllocations.clear();
    globalIndirections.clear();
    basePtrMap.clear();
    nextNodeId = 0;
    
    // Phase 1: Collect allocations from all functions
    errs() << "--- Phase 1: Collecting allocations ---\n";
    for (Function &F : M) {
        if (!F.isDeclaration()) {
            errs() << "Collecting allocations in function: " << F.getName() << "\n";
            collectAllocations(F);
        }
    }
    
    // Phase 2: Detect indirections across the module
    errs() << "\n--- Phase 2: Detecting indirections ---\n";
    // Comment out module-level detection for now to avoid segfault
    // indirectionDetector->detectIndirectionsInModule(M);
    
    // Run per-function detection
    for (Function &F : M) {
        if (!F.isDeclaration()) {
            detectIndirections(F);
        }
    }
    
    // Insert global DIG header
    digInsertion->insertGlobalDIGHeader(M);
    
    // Third pass: insert runtime calls
    errs() << "\n--- Phase 3: Inserting runtime calls ---\n";
    for (Function &F : M) {
        if (F.isDeclaration()) continue;
        
        // Skip OpenMP runtime functions
        StringRef FuncName = F.getName();
        if (FuncName.find(".omp") != StringRef::npos || FuncName.find("__kmpc") != StringRef::npos || 
            FuncName.find("omp_") != StringRef::npos || FuncName.find("GOMP") != StringRef::npos) {
            continue;
        }
        
        // Get indirections for this function
        std::vector<IndirectionInfo> indirections;
        if (globalIndirections.find(&F) != globalIndirections.end()) {
            indirections = globalIndirections[&F];
        }
        
        digInsertion->insertRuntimeCalls(F, globalAllocations, indirections, registeredEdges);
    }
    
    // Print summary
    size_t totalIndirections = 0;
    size_t singleValuedCount = 0;
    size_t rangedCount = 0;
    
    for (const auto& pair : globalIndirections) {
        const std::vector<IndirectionInfo>& indirections = pair.second;
        totalIndirections += indirections.size();
        
        for (const IndirectionInfo& info : indirections) {
            if (info.indirectionType == IndirectionType::SingleValued) {
                singleValuedCount++;
            } else {
                rangedCount++;
            }
        }
    }
    
    errs() << "\n=== Summary ===\n";
    errs() << "Total allocations found: " << globalAllocations.size() << "\n";
    errs() << "Total indirections found: " << totalIndirections << "\n";
    errs() << "  - Single-valued: " << singleValuedCount << "\n";
    errs() << "  - Ranged: " << rangedCount << "\n";
    errs() << "===================\n\n";
    
    return modified;
}

void ProdigyPass::getAnalysisUsage(AnalysisUsage &AU) const {
    // LLVM 3.4.2 uses different wrapper names
    AU.addRequired<LoopInfo>();
    AU.addRequired<ScalarEvolution>();
    // MemorySSA and DominatorTree not available in LLVM 3.4.2
    AU.setPreservesAll();
}

void ProdigyPass::collectAllocations(Function &F) {
    // First pass: collect direct allocations
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                Function *Callee = CI->getCalledFunction();
                if (!Callee) continue;
                
                StringRef FuncName = Callee->getName();
                
                // Detect various allocation functions
                if (FuncName == "malloc" || FuncName == "calloc" || 
                    FuncName == "realloc" || FuncName == "_Znwm" || 
                    FuncName == "_Znam") {
                    handleAllocation(CI);
                }
            }
        }
    }
    
    // Second pass: track allocations stored in struct members
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
                Value *StoredValue = SI->getValueOperand();
                Value *StorePtr = SI->getPointerOperand();
                
                // Check if we're storing a registered allocation
                if (pointerTracker->isRegistered(StoredValue)) {
                    // Check if storing to a struct member (GEP with struct access pattern)
                    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(StorePtr)) {
                        if (GEP->getNumIndices() >= 2) {
                            // Check for struct access pattern (first index is 0)
                            if (ConstantInt *FirstIdx = dyn_cast<ConstantInt>(GEP->getOperand(1))) {
                                if (FirstIdx->isZero()) {
                                    errs() << "Found allocation stored to struct member: " << *SI << "\n";
                                    
                                    // Register this GEP pattern with the same node ID
                                    uint32_t nodeId = pointerTracker->getNodeId(StoredValue);
                                    pointerTracker->registerPointer(GEP, nodeId);
                                    
                                    // Also register the struct pointer if it's an allocation
                                    Value *StructPtr = GEP->getPointerOperand();
                                    if (CallInst *StructAlloc = dyn_cast<CallInst>(StructPtr)) {
                                        if (Function *F = StructAlloc->getCalledFunction()) {
                                            if (F->getName() == "malloc" || F->getName() == "calloc") {
                                                errs() << "  Struct itself is allocated: " << *StructAlloc << "\n";
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

bool ProdigyPass::shouldTrackAllocation(CallInst *CI) {
    // Get the calling function
    Function *Caller = CI->getParent()->getParent();
    StringRef CallerName = Caller->getName();
    
    // Skip allocations in OpenMP runtime functions
    if (CallerName.startswith("__kmpc_") || 
        CallerName.startswith(".omp_") ||
        CallerName.startswith("__kmp_") ||
        CallerName.find("omp") != StringRef::npos) {
        errs() << "  Skipping OpenMP runtime allocation in " << CallerName << "\n";
        return false;
    }
    
    // Skip allocations in GOMP (GNU OpenMP) functions
    if (CallerName.startswith("GOMP_")) {
        errs() << "  Skipping GOMP allocation in " << CallerName << "\n";
        return false;
    }
    
    // Skip allocations in system libraries
    if (CallerName.startswith("__") && !CallerName.startswith("__main")) {
        errs() << "  Skipping system allocation in " << CallerName << "\n";
        return false;
    }
    
    // Check if the allocation has a reasonable size (not the suspicious 65536)
    if (ConstantInt *Size = dyn_cast<ConstantInt>(CI->getArgOperand(0))) {
        uint64_t AllocSize = Size->getZExtValue();
        if (AllocSize == 65536) {
            errs() << "  Suspicious allocation size 65536, likely OpenMP stack\n";
            return false;
        }
    }
    
    // Track allocations in user functions or main
    return true;
}

void ProdigyPass::handleAllocation(CallInst *CI) {
    // First check if we should track this allocation
    if (!shouldTrackAllocation(CI)) {
        return;
    }
    
    AllocInfo alloc;
    alloc.allocCall = CI;
    alloc.nodeId = globalAllocations.size();
    
    // Handle different allocation functions
    Function *Callee = CI->getCalledFunction();
    StringRef FuncName = Callee->getName();
    
    // Skip allocations from system/library functions
    if (shouldFilterAllocation(CI)) {
        return;
    }
    
    alloc.basePtr = CI;
    alloc.nodeId = nextNodeId++;
    
    // Initialize default values before inference
    alloc.numElements = nullptr;
    alloc.elementSize = nullptr;
    
    // Use enhanced element size inference
    elementSizeInference->inferElementSize(alloc);
    
    // Ensure valid element size and count
    if (!alloc.elementSize) {
        alloc.elementSize = ConstantInt::get(Type::getInt32Ty(CI->getContext()), 1);
        errs() << "  Warning: Could not infer element size, defaulting to 1\n";
    }
    if (!alloc.numElements) {
        // Try to use the allocation size argument directly
        if (CI->getNumArgOperands() > 0) {
            alloc.numElements = CI->getArgOperand(0);
        } else {
            alloc.numElements = ConstantInt::get(Type::getInt64Ty(CI->getContext()), 1);
        }
        errs() << "  Warning: Could not infer number of elements\n";
    }
    
    // Record globally
    globalAllocations.push_back(alloc);
    basePtrMap[CI] = &globalAllocations.back();
    
    // Register in pointer tracker
    pointerTracker->registerPointer(alloc.basePtr, alloc.nodeId);
    
    // Debug output
    errs() << "Found allocation: " << *CI << " (Node ID: " << alloc.nodeId << ")\n";
    errs() << "  Base pointer (result): " << alloc.basePtr << " (type: " << *alloc.basePtr->getType() << ")\n";
    errs() << "  Stored in map: " << alloc.basePtr << " -> " << alloc.nodeId << "\n";
    if (alloc.constantElementSize > 0) {
        errs() << "  Element size: " << alloc.constantElementSize << " bytes\n";
    }
    if (alloc.constantNumElements > 0) {
        errs() << "  Number of elements: " << alloc.constantNumElements << "\n";
    }
}

bool ProdigyPass::shouldFilterAllocation(CallInst *CI) {
    Function *ParentFunc = CI->getParent()->getParent();
    StringRef FuncName = ParentFunc->getName();
    
    // Skip OpenMP runtime allocations
    if (FuncName.find(".omp") != StringRef::npos || FuncName.find("__kmpc") != StringRef::npos || 
        FuncName.find("omp_") != StringRef::npos || FuncName.find("GOMP") != StringRef::npos) {
        return true;
    }
    
    return false;
}

void ProdigyPass::detectIndirections(Function &F) {
    // Use the indirection detector
    if (!indirectionDetector) {
        errs() << "Warning: IndirectionDetector not initialized\n";
        return;
    }
    
    indirectionDetector->clearIndirections();  // Clear previous results
    indirectionDetector->identifySingleValuedIndirections(F);
    indirectionDetector->identifyRangedIndirections(F);
    
    // Get the results
    const std::vector<IndirectionInfo>& detectedIndirections = 
        indirectionDetector->getIndirections();
    
    if (!detectedIndirections.empty()) {
        globalIndirections[&F] = detectedIndirections;
        errs() << "Function " << F.getName() << ": found " 
               << detectedIndirections.size() << " indirections\n";
        
        for (const IndirectionInfo &info : detectedIndirections) {
            errs() << "  - " << (info.indirectionType == IndirectionType::SingleValued 
                              ? "Single-valued" : "Ranged")
                   << " indirection from node " << info.srcNodeId 
                   << " to node " << info.destNodeId << "\n";
        }
    }
}

} // namespace prodigy

// Register the pass
using namespace prodigy;
static RegisterPass<ProdigyPass> X("prodigy", "Prodigy Hardware Prefetching Pass", false, false);

// LLVM 16 doesn't support new pass manager interface yet
#if 0
// New pass manager interface (for future LLVM versions)
extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "ProdigyPass", "v0.1",
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "prodigy") {
                        MPM.addPass(ProdigyPass());
                        return true;
                    }
                    return false;
                });
        }};
}
#endif 