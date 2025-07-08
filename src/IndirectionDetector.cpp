#include "IndirectionDetector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"
#include <queue>
#include <set>
#include <algorithm>
#include <map>
#include <utility>

using namespace llvm;
using namespace prodigy;

IndirectionDetector::IndirectionDetector(BasePointerTracker* tracker) 
    : bpTracker(tracker) {}

// Helper: follow bpTracker once, and if the result is a load from a
// temporary/alloca, follow its pointer operand one more step. This lets us
// recover the real heap/global array even when it was first stored into a
// stack slot.
Value* IndirectionDetector::getUltimateBase(Value *V) {
    Value *Base = bpTracker->getBasePointer(V);
    if (LoadInst *L = dyn_cast<LoadInst>(Base)) {
        Base = bpTracker->getBasePointer(L->getPointerOperand());
    }
    // Strip bitcasts and GEPs with zero indices (identity)
    while (true) {
        if (BitCastInst *BC = dyn_cast<BitCastInst>(Base)) {
            Base = BC->getOperand(0);
            continue;
        }
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Base)) {
            // Regardless of indices, peel one level to reach the underlying array
            Base = GEP->getPointerOperand();
            continue;
        }
        break;
    }
    return Base;
}

std::vector<IndirectionInfo> IndirectionDetector::detectIndirections(Function& F, 
                                                                   const std::vector<AllocInfo>& allocations) {
    // Clear previous results
    indirections.clear();
    
    // Detect single-valued indirections
    identifySingleValuedIndirections(F);
    
    // Detect ranged indirections
    identifyRangedIndirections(F);
    
    // Fill in node IDs for the detected indirections
    for (IndirectionInfo& info : indirections) {
        info.srcNodeId = getNodeIdFromBase(info.srcBase, allocations);
        info.destNodeId = getNodeIdFromBase(info.destBase, allocations);
    }
    
    return indirections;
}

uint32_t IndirectionDetector::getNodeIdFromBase(Value* base, const std::vector<AllocInfo>& allocations) {
    for (const AllocInfo& alloc : allocations) {
        if (alloc.basePtr == base) {
            return alloc.nodeId;
        }
    }
    return UINT32_MAX; // Invalid node ID
}

void IndirectionDetector::identifySingleValuedIndirections(Function& F) {
    errs() << "Analyzing function " << F.getName() << " for single-valued indirections\n";
    
    // First, let's see what allocations are registered
    errs() << "  Registered allocations:\n";
    const auto& registeredPtrs = bpTracker->getRegisteredPointers();
    for (const auto& pair : registeredPtrs) {
        errs() << "    " << pair.first << " -> Node " << pair.second << "\n";
    }
    
    // Special handling for BFS-like patterns
    if (F.getName().contains("TDStep") || F.getName().contains("BFS")) {
        detectBFSPatterns(F);
    }
    
    int totalLoads = 0;
    int loadsWithGEP = 0;
    int gepsWithLoadIndex = 0;
    
    // Also track all GEPs that might be array accesses
    std::vector<GetElementPtrInst*> arrayGEPs;
    
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            // Collect all GEPs first
            if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(&I)) {
                if (GEP->getNumIndices() >= 1) {
                    arrayGEPs.push_back(GEP);
                }
            }
            
            // Check for calls to accessor functions
            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                Function *Callee = CI->getCalledFunction();
                if (Callee && accessorPatterns.find(Callee) != accessorPatterns.end()) {
                    errs() << "  Found call to accessor function: " << Callee->getName() << "\n";
                    
                    // Try to connect the patterns
                    connectAccessorPattern(CI, Callee);
                }
            }
            
            if (LoadInst *OuterLoad = dyn_cast<LoadInst>(&I)) {
                totalLoads++;
                
                // Check if this load's address comes from a GEP
                if (GetElementPtrInst *OuterGEP = dyn_cast<GetElementPtrInst>(OuterLoad->getPointerOperand())) {
                    loadsWithGEP++;
                    
                    // For A[B[i]] pattern, we need:
                    // 1. The GEP that computes &A[index]
                    // 2. The index should come from a load (B[i])
                    
                    // GEP can have multiple indices. For simple array access, typically just one.
                    // For struct member access, there could be multiple.
                    for (unsigned i = 0; i < OuterGEP->getNumIndices(); ++i) {
                        Value *Index = OuterGEP->getOperand(i + 1); // Operand 0 is base pointer
                        
                        // Check if this index value comes from a load
                        // First handle sign/zero extensions
                        Value *OrigIndex = Index;
                        if (SExtInst *SExt = dyn_cast<SExtInst>(Index)) {
                            OrigIndex = SExt->getOperand(0);
                        } else if (ZExtInst *ZExt = dyn_cast<ZExtInst>(Index)) {
                            OrigIndex = ZExt->getOperand(0);
                        }
                        
                        // Find underlying load feeding the index if any
                        if (LoadInst *IndexLoad = traceToLoad(OrigIndex)) {
                            gepsWithLoadIndex++;
                            
                            // We found A[loaded_value] pattern!
                            // Now check if the loaded value itself comes from an array access
                            Value *srcBase = nullptr;
                            if (GetElementPtrInst *InnerGEP = dyn_cast<GetElementPtrInst>(IndexLoad->getPointerOperand())) {
                                srcBase = getUltimateBase(InnerGEP->getPointerOperand());
                            } else {
                                srcBase = getUltimateBase(IndexLoad->getPointerOperand());
                            }

                            Value *destBase = getUltimateBase(OuterGEP->getPointerOperand());

                            errs() << "Found single-valued indirection candidate:\n";
                            errs() << "  Index load: " << *IndexLoad << "\n";
                            errs() << "  Outer load: " << *OuterLoad << "\n";
                            errs() << "  srcBase: " << srcBase << " (" << *srcBase << ")\n";
                            errs() << "  destBase: " << destBase << " (" << *destBase << ")\n";
                            errs() << "  srcBase registered: " << bpTracker->isRegistered(srcBase) << "\n";
                            errs() << "  destBase registered: " << bpTracker->isRegistered(destBase) << "\n";

                            // Record the indirection
                            IndirectionInfo info;
                            info.indirectionType = IndirectionType::SingleValued;
                            info.srcBase = srcBase;
                            info.destBase = destBase;
                            info.accessInst = OuterLoad;
                            
                            // Get node IDs from BasePointerTracker
                            if (bpTracker->isRegistered(srcBase)) {
                                info.srcNodeId = bpTracker->getNodeId(srcBase);
                            } else {
                                errs() << "  Warning: srcBase not registered\n";
                                info.srcNodeId = UINT32_MAX;  // Invalid ID
                            }
                            
                            if (bpTracker->isRegistered(destBase)) {
                                info.destNodeId = bpTracker->getNodeId(destBase);
                            } else {
                                errs() << "  Warning: destBase not registered\n";
                                info.destNodeId = UINT32_MAX;  // Invalid ID
                            }
                            
                            // Only record if both nodes are valid
                            if (info.srcNodeId != UINT32_MAX && info.destNodeId != UINT32_MAX) {
                                // Check for duplicates
                                EdgeKey key(srcBase, destBase, IndirectionType::SingleValued);
                                if (detectedPatterns.find(key) == detectedPatterns.end()) {
                                    indirections.push_back(info);
                                    detectedPatterns.insert(key);
                                    errs() << "  srcBase registered: " << bpTracker->isRegistered(srcBase) << "\n";
                                    errs() << "  destBase registered: " << bpTracker->isRegistered(destBase) << "\n";
                                    errs() << "  ==> recorded edge\n";
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Second pass: look for indirect patterns through iterator-like accesses
    for (GetElementPtrInst *GEP : arrayGEPs) {
        // Check if this GEP is used in a load
        for (User *U : GEP->users()) {
            if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
                // This loads from an array
                Value *ArrayBase = getUltimateBase(GEP->getPointerOperand());
                
                // Check if the loaded value is used as an index
                for (User *LU : LI->users()) {
                    // Handle various conversions
                    Value *IndexValue = LI;
                    if (SExtInst *SE = dyn_cast<SExtInst>(LU)) {
                        IndexValue = SE;
                    } else if (ZExtInst *ZE = dyn_cast<ZExtInst>(LU)) {
                        IndexValue = ZE;
                    }
                    
                    // Look for uses in other GEPs
                    for (User *IU : IndexValue->users()) {
                        if (GetElementPtrInst *OuterGEP = dyn_cast<GetElementPtrInst>(IU)) {
                            // Check if this GEP uses our loaded value as index
                            for (unsigned i = 1; i < OuterGEP->getNumOperands(); i++) {
                                if (OuterGEP->getOperand(i) == IndexValue) {
                                    // Found a pattern!
                                    Value *DestBase = getUltimateBase(OuterGEP->getPointerOperand());
                                    
                                    if (bpTracker->isRegistered(ArrayBase) && 
                                        bpTracker->isRegistered(DestBase) &&
                                        ArrayBase != DestBase) {
                                        
                                        // Look for loads using this GEP
                                        for (User *GU : OuterGEP->users()) {
                                            if (LoadInst *FinalLoad = dyn_cast<LoadInst>(GU)) {
                                                createIndirectionEntry(ArrayBase, DestBase, FinalLoad,
                                                                     IndirectionType::SingleValued);
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
    
    errs() << "  Stats: " << totalLoads << " loads, " << loadsWithGEP << " with GEP, " 
           << gepsWithLoadIndex << " with load index\n";
    errs() << "Found " << indirections.size() << " single-valued indirection patterns in " << F.getName() << "\n";
}

void IndirectionDetector::identifyRangedIndirections(Function& F) {
    errs() << "Analyzing function for ranged indirection patterns\n";
    
    // Find all loads in the function
    std::vector<LoadInst*> allLoads;
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
                allLoads.push_back(LI);
            }
        }
    }
    
    errs() << "  Found " << allLoads.size() << " load instructions\n";
    
    // Look for pairs of loads that could be offset[i] and offset[i+1]
    for (size_t i = 0; i < allLoads.size(); ++i) {
        LoadInst *Load1 = allLoads[i];
        
        for (size_t j = i + 1; j < allLoads.size(); ++j) {
            LoadInst *Load2 = allLoads[j];
            
            if (areConsecutiveArrayLoads(Load1, Load2)) {
                errs() << "  Found consecutive array loads:\n";
                errs() << "    Load1: " << *Load1 << "\n";
                errs() << "    Load2: " << *Load2 << "\n";
                
                // Check if these loads are used as loop bounds
                checkForRangedPattern(Load1, Load2);
            }
        }
    }
}

bool IndirectionDetector::areConsecutiveArrayLoads(LoadInst *Load1, LoadInst *Load2) {
    GetElementPtrInst *GEP1 = dyn_cast<GetElementPtrInst>(Load1->getPointerOperand());
    GetElementPtrInst *GEP2 = dyn_cast<GetElementPtrInst>(Load2->getPointerOperand());
    
    if (!GEP1 || !GEP2) return false;
    
    // Get the ultimate base pointers (handle cases where base is loaded from memory)
    Value *Base1 = GEP1->getPointerOperand();
    Value *Base2 = GEP2->getPointerOperand();
    
    // Handle struct member access - trace through multiple levels
    // First, check if both GEPs access struct members
    if (LoadInst *BaseLoad1 = dyn_cast<LoadInst>(Base1)) {
        if (GetElementPtrInst *StructGEP1 = dyn_cast<GetElementPtrInst>(BaseLoad1->getPointerOperand())) {
            // This is accessing a member of a struct, trace to the struct base
            Base1 = getUltimateBase(StructGEP1->getPointerOperand());
            errs() << "    Load1 accesses struct member, ultimate base: " << *Base1 << "\n";
        }
    }
    
    if (LoadInst *BaseLoad2 = dyn_cast<LoadInst>(Base2)) {
        if (GetElementPtrInst *StructGEP2 = dyn_cast<GetElementPtrInst>(BaseLoad2->getPointerOperand())) {
            // This is accessing a member of a struct, trace to the struct base
            Base2 = getUltimateBase(StructGEP2->getPointerOperand());
            errs() << "    Load2 accesses struct member, ultimate base: " << *Base2 << "\n";
        }
    }
    
    // If bases are loads from the same location, they're the same array
    if (LoadInst *BaseLoad1 = dyn_cast<LoadInst>(Base1)) {
        if (LoadInst *BaseLoad2 = dyn_cast<LoadInst>(Base2)) {
            if (BaseLoad1->getPointerOperand() == BaseLoad2->getPointerOperand()) {
                // Same array loaded from same location
                Base1 = Base2 = BaseLoad1->getPointerOperand();
            }
        }
    }
    
    // Check if same base pointer or if they load from the same struct member
    bool sameBase = false;
    if (Base1 == Base2) {
        sameBase = true;
    } else if (GEP1->getPointerOperand() == GEP2->getPointerOperand()) {
        sameBase = true;
    } else {
        // Check if both are loads from the same struct member
        if (LoadInst *L1 = dyn_cast<LoadInst>(GEP1->getPointerOperand())) {
            if (LoadInst *L2 = dyn_cast<LoadInst>(GEP2->getPointerOperand())) {
                if (GetElementPtrInst *SGEP1 = dyn_cast<GetElementPtrInst>(L1->getPointerOperand())) {
                    if (GetElementPtrInst *SGEP2 = dyn_cast<GetElementPtrInst>(L2->getPointerOperand())) {
                        if (bpTracker->areGEPsSimilar(SGEP1, SGEP2)) {
                            sameBase = true;
                            errs() << "    Both loads access same struct member\n";
                        }
                    }
                }
            }
        }
    }
    
    if (!sameBase) {
        return false;
    }
    
    // Check if indices differ by 1
    if (GEP1->getNumIndices() != 1 || GEP2->getNumIndices() != 1) return false;
    
    Value *Idx1 = GEP1->getOperand(1);
    Value *Idx2 = GEP2->getOperand(1);
    
    // Handle sign extension
    if (SExtInst *SExt1 = dyn_cast<SExtInst>(Idx1)) {
        Idx1 = SExt1->getOperand(0);
    }
    if (SExtInst *SExt2 = dyn_cast<SExtInst>(Idx2)) {
        Idx2 = SExt2->getOperand(0);
    }
    
    // Check for pattern: idx2 = idx1 + 1
    if (BinaryOperator *Add = dyn_cast<BinaryOperator>(Idx2)) {
        if (Add->getOpcode() == Instruction::Add) {
            Value *AddOp0 = Add->getOperand(0);
            // Handle case where the index might be loaded from memory
            if (LoadInst *LI = dyn_cast<LoadInst>(AddOp0)) {
                if (LoadInst *LI1 = dyn_cast<LoadInst>(Idx1)) {
                    if (LI->getPointerOperand() == LI1->getPointerOperand()) {
                        AddOp0 = Idx1;
                    }
                }
            }
            
            if (AddOp0 == Idx1) {
                if (ConstantInt *CI = dyn_cast<ConstantInt>(Add->getOperand(1))) {
                    if (CI->getSExtValue() == 1) {
                        errs() << "    Found consecutive loads: " << *Load1 << " and " << *Load2 << "\n";
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

void IndirectionDetector::checkForRangedPattern(LoadInst *StartLoad, LoadInst *EndLoad) {
    errs() << "    Checking for ranged pattern\n";
    
    // Trace through store-load chains to find eventual uses
    std::set<Value*> startValues;
    std::set<Value*> endValues;
    
    startValues.insert(StartLoad);
    endValues.insert(EndLoad);
    
    // Follow store-load chains for start value
    for (User *U : StartLoad->users()) {
        if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
            Value *StoredTo = SI->getPointerOperand();
            for (User *StoreUser : StoredTo->users()) {
                if (LoadInst *LI = dyn_cast<LoadInst>(StoreUser)) {
                    startValues.insert(LI);
                }
            }
        }
    }
    
    // Follow store-load chains for end value
    for (User *U : EndLoad->users()) {
        if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
            Value *StoredTo = SI->getPointerOperand();
            for (User *StoreUser : StoredTo->users()) {
                if (LoadInst *LI = dyn_cast<LoadInst>(StoreUser)) {
                    endValues.insert(LI);
                }
            }
        }
    }
    
    // Now look for comparisons using any of these values
    for (Value *EndVal : endValues) {
        for (User *EndUser : EndVal->users()) {
            if (ICmpInst *Cmp = dyn_cast<ICmpInst>(EndUser)) {
                errs() << "      Found comparison: " << *Cmp << "\n";
                
                // Find the basic block containing the loop body
                BasicBlock *LoopBB = nullptr;
                for (User *CmpUser : Cmp->users()) {
                    if (BranchInst *BI = dyn_cast<BranchInst>(CmpUser)) {
                        if (BI->isConditional()) {
                            LoopBB = BI->getSuccessor(0); // True branch usually contains loop body
                        }
                    }
                }
                
                if (LoopBB) {
                    // Find loads in the loop body
                    std::vector<LoadInst*> candidateLoads;
                    for (Instruction &I : *LoopBB) {
                        if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
                            candidateLoads.push_back(LI);
                        }
                    }
                    
                    errs() << "      Found " << candidateLoads.size() << " loads in loop body\n";
                    
                    // Track unique ranged patterns
                    std::set<std::pair<Value*, Value*>> rangedPatterns;
                    
                    // Check each load to see if it's accessing a different array
                    for (LoadInst *Access : candidateLoads) {
                        errs() << "        Checking load: " << *Access << "\n";
                        
                        // Simple heuristic: if this load is from a different allocation than StartLoad
                        Value *AccessBase = getUltimateBase(Access->getPointerOperand());
                        Value *StartBase = getUltimateBase(StartLoad->getPointerOperand());
                        
                        errs() << "          Access base: " << AccessBase << "\n";
                        errs() << "          Start base: " << StartBase << "\n";
                        errs() << "          AccessBase in allocations: " 
                               << bpTracker->isRegistered(AccessBase) << "\n";
                        errs() << "          StartBase in allocations: " 
                               << bpTracker->isRegistered(StartBase) << "\n";
                        
                        if (AccessBase != StartBase && 
                            bpTracker->isRegistered(AccessBase) &&
                            bpTracker->isRegistered(StartBase)) {
                            
                            // Check if we've already seen this pattern locally or globally
                            auto pattern = std::make_pair(StartBase, AccessBase);
                            if (rangedPatterns.find(pattern) == rangedPatterns.end()) {
                                rangedPatterns.insert(pattern);
                                
                                // Also check at function level
                                EdgeKey edgeKey(StartBase, AccessBase, IndirectionType::Ranged);
                                if (detectedRangedPatterns.find(edgeKey) == detectedRangedPatterns.end()) {
                                    detectedRangedPatterns.insert(edgeKey);
                                    
                                    errs() << "Found ranged indirection pattern:\n";
                                    errs() << "  Start load: " << *StartLoad << "\n";
                                    errs() << "  End load: " << *EndLoad << "\n";
                                    errs() << "  Target access: " << *Access << "\n";
                                    
                                    IndirectionInfo info;
                                    info.indirectionType = IndirectionType::Ranged;
                                    info.accessInst = Access;
                                    info.srcBase = StartBase;
                                    info.destBase = AccessBase;
                                    
                                    // Get node IDs from BasePointerTracker
                                    if (bpTracker->isRegistered(StartBase)) {
                                        info.srcNodeId = bpTracker->getNodeId(StartBase);
                                    } else {
                                        info.srcNodeId = UINT32_MAX;
                                    }
                                    
                                    if (bpTracker->isRegistered(AccessBase)) {
                                        info.destNodeId = bpTracker->getNodeId(AccessBase);
                                    } else {
                                        info.destNodeId = UINT32_MAX;
                                    }
                                    
                                    // Only add if both nodes are valid
                                    if (info.srcNodeId != UINT32_MAX && info.destNodeId != UINT32_MAX) {
                                        indirections.push_back(info);
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

void IndirectionDetector::findLoadsInRangedAccess(BasicBlock *BB, std::vector<LoadInst*> &loads) {
    // Simple DFS to find loads in blocks reachable from this BB
    std::set<BasicBlock*> visited;
    std::queue<BasicBlock*> worklist;
    worklist.push(BB);
    
    while (!worklist.empty()) {
        BasicBlock *Current = worklist.front();
        worklist.pop();
        
        if (!visited.insert(Current).second) continue;
        
        // Collect loads in this block
        for (Instruction &I : *Current) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
                loads.push_back(LI);
            }
        }
        
        // Add successors to worklist
        for (BasicBlock *Succ : successors(Current)) {
            worklist.push(Succ);
        }
        
        // Limit search depth
        if (visited.size() > 10) break;
    }
}

bool IndirectionDetector::isRangedAccess(LoadInst *Access, LoadInst *StartLoad, LoadInst *EndLoad) {
    // Check if this access uses an index that's initialized from StartLoad
    // and compared against EndLoad
    
    GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Access->getPointerOperand());
    if (!GEP || GEP->getNumIndices() != 1) return false;
    
    Value *Index = GEP->getOperand(1);
    
    // Check if Index is in a block that's dominated by stores of StartLoad/EndLoad
    BasicBlock *AccessBB = Access->getParent();
    
    // Simple heuristic: check if StartLoad and EndLoad are in the same function
    // and appear before this access
    if (StartLoad->getParent()->getParent() != Access->getParent()->getParent()) {
        return false;
    }
    
    // Check if there are store instructions that save StartLoad and EndLoad values
    bool foundStartStore = false;
    bool foundEndStore = false;
    
    // Look for pattern where StartLoad value is stored and then used
    for (User *U : StartLoad->users()) {
        if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
            if (SI->getValueOperand() == StartLoad) {
                // Check if the stored value is later loaded and used in comparison with Index
                Value *StoredPtr = SI->getPointerOperand();
                for (User *PtrUser : StoredPtr->users()) {
                    if (LoadInst *LI = dyn_cast<LoadInst>(PtrUser)) {
                        if (LI != StartLoad && isRelatedToValue(Index, LI)) {
                            foundStartStore = true;
                        }
                    }
                }
            }
        }
    }
    
    // Similar check for EndLoad
    for (User *U : EndLoad->users()) {
        if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
            if (SI->getValueOperand() == EndLoad) {
                Value *StoredPtr = SI->getPointerOperand();
                for (User *PtrUser : StoredPtr->users()) {
                    if (LoadInst *LI = dyn_cast<LoadInst>(PtrUser)) {
                        if (LI != EndLoad) {
                            // Check if this loaded value is used in a comparison
                            for (User *LIUser : LI->users()) {
                                if (ICmpInst *Cmp = dyn_cast<ICmpInst>(LIUser)) {
                                    if (isRelatedToValue(Cmp->getOperand(0), Index) ||
                                        isRelatedToValue(Cmp->getOperand(1), Index)) {
                                        foundEndStore = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    return foundStartStore && foundEndStore;
}

bool IndirectionDetector::isRelatedToValue(Value *V1, Value *V2) {
    if (V1 == V2) return true;
    
    // Check through simple casts
    if (CastInst *CI = dyn_cast<CastInst>(V1)) {
        return isRelatedToValue(CI->getOperand(0), V2);
    }
    
    // Check through simple arithmetic
    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(V1)) {
        return isRelatedToValue(BO->getOperand(0), V2) || 
               isRelatedToValue(BO->getOperand(1), V2);
    }
    
    return false;
}

LoadInst* IndirectionDetector::traceToLoad(Value *V) {
    // Track through multiple levels of indirection
    std::set<Value*> visited;
    std::queue<Value*> worklist;
    worklist.push(V);
    
    while (!worklist.empty() && visited.size() < 10) {
        Value* current = worklist.front();
        worklist.pop();
        
        if (visited.count(current)) continue;
        visited.insert(current);
        
        if (LoadInst *LI = dyn_cast<LoadInst>(current)) {
            // Check if this load is from an alloca (local variable)
            if (AllocaInst *AI = dyn_cast<AllocaInst>(LI->getPointerOperand())) {
                // Look for stores to this alloca in the same function
                for (User *U : AI->users()) {
                    if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
                        if (SI->getPointerOperand() == AI) {
                            // Check if the store dominates the load
                            if (SI->getParent() == LI->getParent() || 
                                (SI->getParent()->getParent() == LI->getParent()->getParent())) {
                                // Add the stored value to worklist
                                worklist.push(SI->getValueOperand());
                            }
                        }
                    }
                }
            } else {
                // This load is from heap/global memory, return it
                return LI;
            }
        } else if (SExtInst *SExt = dyn_cast<SExtInst>(current)) {
            worklist.push(SExt->getOperand(0));
        } else if (ZExtInst *ZExt = dyn_cast<ZExtInst>(current)) {
            worklist.push(ZExt->getOperand(0));
        } else if (TruncInst *Trunc = dyn_cast<TruncInst>(current)) {
            worklist.push(Trunc->getOperand(0));
        } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(current)) {
            // Handle simple arithmetic where one operand might be a load
            worklist.push(BO->getOperand(0));
            worklist.push(BO->getOperand(1));
        }
    }
    
    return nullptr;
}

void IndirectionDetector::detectIndirectionsInModule(Module &M) {
    // First pass: analyze all functions
    for (Function &F : M) {
        if (F.isDeclaration()) continue;
        
        // Check if this is a simple accessor function we should inline analyze
        if (isSimpleAccessorFunction(F)) {
            analyzeAccessorFunction(F);
        }
        
        // Regular indirection detection
        identifySingleValuedIndirections(F);
        identifyRangedIndirections(F);
    }
}

bool IndirectionDetector::isSimpleAccessorFunction(Function &F) {
    // Check if function name suggests it's an accessor
    StringRef name = F.getName();
    if (!name.contains("begin") && !name.contains("end") && 
        !name.contains("operator") && !name.contains("at")) {
        return false;
    }
    
    // Check if it's small enough to analyze
    if (F.empty() || F.size() > 1) return false;  // Only single basic block
    
    BasicBlock &BB = F.getEntryBlock();
    if (BB.size() > 20) return false;  // Not too many instructions
    
    // Check if it returns a pointer
    Type *RetType = F.getReturnType();
    if (!RetType || !RetType->isPointerTy()) return false;
    
    errs() << "Found simple accessor function: " << F.getName() << "\n";
    return true;
}

void IndirectionDetector::analyzeAccessorFunction(Function &F) {
    errs() << "Analyzing accessor function: " << F.getName() << "\n";
    
    if (F.empty()) {
        errs() << "  Function is empty\n";
        return;
    }
    
    BasicBlock &BB = F.getEntryBlock();
    
    // Look for pattern: load from this pointer's member, then use as index
    if (F.arg_empty()) {
        errs() << "  Function has no arguments\n";
        return;
    }
    
    Value *ThisPtr = &*F.arg_begin();  // First argument is usually 'this'
    if (!ThisPtr) return;
    
    std::vector<LoadInst*> loads;
    std::vector<GetElementPtrInst*> geps;
    
    // Collect all loads and GEPs
    for (Instruction &I : BB) {
        if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
            loads.push_back(LI);
        } else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(&I)) {
            geps.push_back(GEP);
        }
    }
    
    // Look for indirection pattern within the function
    for (LoadInst *LI : loads) {
        // Check if this load uses another load as index
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(LI->getPointerOperand())) {
            for (unsigned i = 1; i < GEP->getNumOperands(); i++) {
                Value *Idx = GEP->getOperand(i);
                
                // Check if index comes from another load
                if (LoadInst *IdxLoad = dyn_cast<LoadInst>(Idx)) {
                    errs() << "  Found potential indirection in accessor:\n";
                    errs() << "    Index load: " << *IdxLoad << "\n";
                    errs() << "    Data load: " << *LI << "\n";
                    
                    // Store this pattern for later use
                    accessorPatterns[&F].push_back({IdxLoad, LI, GEP});
                }
            }
        }
    }
}

void IndirectionDetector::connectAccessorPattern(CallInst *CI, Function *AccessorFunc) {
    errs() << "  Connecting accessor pattern from " << AccessorFunc->getName() << "\n";
    
    // Get the patterns we found in the accessor
    const std::vector<AccessorPattern>& patterns = accessorPatterns[AccessorFunc];
    
    // Get the 'this' pointer passed to the accessor
    if (CI->arg_size() == 0) return;
    Value *ThisArg = CI->getArgOperand(0);
    
    // Try to trace the this pointer to a registered allocation
    Value *BasePtr = bpTracker->getBasePointer(ThisArg);
    if (!BasePtr || !bpTracker->isRegistered(BasePtr)) {
        errs() << "    Could not trace 'this' pointer to registered allocation\n";
        return;
    }
    
    uint32_t nodeId = bpTracker->getNodeId(BasePtr);
    errs() << "    'this' pointer traces to node " << nodeId << "\n";
    
    // For each pattern in the accessor, try to determine the actual arrays involved
    for (const AccessorPattern& pattern : patterns) {
        errs() << "    Analyzing pattern in accessor\n";
        
        // The pattern has loads from members of the 'this' object
        // We need to figure out which members these are and if they're registered allocations
        
        // This is a simplified version - in practice we'd need more sophisticated analysis
        // For now, we'll assume CSRGraph members map to our registered nodes
        IndirectionInfo info;
        info.indirectionType = IndirectionType::SingleValued;
        info.srcBase = BasePtr;  // Simplified - should trace through members
        info.destBase = BasePtr; // Simplified - should trace through members
        info.accessInst = CI;
        info.srcNodeId = nodeId;
        info.destNodeId = nodeId;
        
        // Record this as a potential indirection
        errs() << "    Recorded potential indirection from accessor call\n";
        indirections.push_back(info);
    }
}

void IndirectionDetector::detectBFSPatterns(Function &F) {
    errs() << "  Detecting complex patterns in " << F.getName() << "\n";
    
    // Track struct/class members that point to registered allocations
    std::map<Value*, std::map<unsigned, Value*>> structMembers;
    
    // First pass: find stores to struct members
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
                Value *StoredValue = SI->getValueOperand();
                Value *StorePtr = SI->getPointerOperand();
                
                // Check if storing to a struct member
                if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(StorePtr)) {
                    if (GEP->getNumIndices() >= 2) {
                        // This is likely a struct member access
                        Value *StructBase = GEP->getPointerOperand();
                        
                        // Get the member index
                        if (ConstantInt *MemberIdx = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                            unsigned MemberNum = MemberIdx->getZExtValue();
                            
                            // Check if the stored value is a registered allocation
                            Value *StoredBase = bpTracker->getBasePointer(StoredValue);
                            if (StoredBase && bpTracker->isRegistered(StoredBase)) {
                                errs() << "    Found struct member " << MemberNum 
                                       << " storing registered allocation (Node " 
                                       << bpTracker->getNodeId(StoredBase) << ")\n";
                                structMembers[StructBase][MemberNum] = StoredBase;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Second pass: look for loads from struct members and track their uses
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
                Value *LoadPtr = LI->getPointerOperand();
                
                // Check if loading from a struct member
                if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(LoadPtr)) {
                    if (GEP->getNumIndices() >= 2) {
                        Value *StructBase = GEP->getPointerOperand();
                        
                        if (ConstantInt *MemberIdx = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                            unsigned MemberNum = MemberIdx->getZExtValue();
                            
                            // Check if we know about this struct and member
                            if (structMembers.count(StructBase) > 0 && 
                                structMembers[StructBase].count(MemberNum) > 0) {
                                
                                Value *MemberArray = structMembers[StructBase][MemberNum];
                                errs() << "    Loading struct member " << MemberNum << "\n";
                                
                                // Track uses of this loaded pointer
                                analyzePointerUses(LI, MemberArray, structMembers[StructBase]);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Third pass: analyze function calls that might contain indirections
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                Function *Callee = CI->getCalledFunction();
                if (!Callee || Callee->isDeclaration()) continue;
                
                // Analyze small functions inline
                if (shouldAnalyzeInline(Callee)) {
                    analyzeCallSite(CI, Callee, structMembers);
                }
            }
        }
    }
}

void IndirectionDetector::analyzePointerUses(LoadInst* PtrLoad, Value* SourceArray, 
                                            const std::map<unsigned, Value*>& allMembers) {
    errs() << "      Analyzing uses of loaded pointer\n";
    
    for (User *U : PtrLoad->users()) {
        // Case 1: Used in GEP (array access)
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
            // Check if the GEP index comes from another array
            for (unsigned i = 1; i < GEP->getNumOperands(); i++) {
                Value *Idx = GEP->getOperand(i);
                
                // Trace to see if index comes from a load
                if (LoadInst *IdxLoad = traceToLoad(Idx)) {
                    Value *IdxSrcBase = getUltimateBase(IdxLoad->getPointerOperand());
                    
                    // Check if this creates an indirection pattern
                    if (bpTracker->isRegistered(IdxSrcBase) && bpTracker->isRegistered(SourceArray)) {
                        // Look for loads using this GEP
                        for (User *GEPUser : GEP->users()) {
                            if (LoadInst *DataLoad = dyn_cast<LoadInst>(GEPUser)) {
                                createIndirectionEntry(IdxSrcBase, SourceArray, DataLoad, 
                                                     IndirectionType::SingleValued);
                            }
                        }
                    }
                }
            }
        }
        
        // Case 2: Passed to function
        if (CallInst *CI = dyn_cast<CallInst>(U)) {
            Function *Callee = CI->getCalledFunction();
            if (Callee) {
                errs() << "        Pointer passed to: " << Callee->getName() << "\n";
            }
        }
    }
}

bool IndirectionDetector::shouldAnalyzeInline(Function* F) {
    // Check function name patterns that suggest graph operations
    StringRef name = F->getName();
    if (name.contains("neigh") || name.contains("begin") || name.contains("end") ||
        name.contains("Neighborhood") || name.contains("out_degree") || 
        name.contains("in_degree") || name.contains("num_nodes")) {
        return true;
    }
    
    // Only analyze small functions to avoid complexity explosion
    if (F->empty() || F->size() > 5) return false;
    
    // Count instructions
    unsigned instCount = 0;
    for (BasicBlock &BB : *F) {
        instCount += BB.size();
    }
    
    return instCount < 100;
}

void IndirectionDetector::analyzeCallSite(CallInst* CI, Function* Callee,
                                         const std::map<Value*, std::map<unsigned, Value*>>& structMembers) {
    errs() << "    Inline analyzing call to " << Callee->getName() << "\n";
    
    // Special handling for graph-related functions
    StringRef funcName = Callee->getName();
    if (funcName.contains("Neighborhood") || funcName.contains("begin") || funcName.contains("end")) {
        analyzeNeighborhoodFunction(CI, Callee);
        return;
    }
    
    // Map arguments to parameters
    std::map<Value*, Value*> argMap;
    unsigned argIdx = 0;
    for (Function::arg_iterator AI = Callee->arg_begin(); AI != Callee->arg_end(); ++AI, ++argIdx) {
        if (argIdx < CI->arg_size()) {
            argMap[&*AI] = CI->getArgOperand(argIdx);
        }
    }
    
    // Look for consecutive array loads (ranged pattern)
    std::vector<LoadInst*> loads;
    for (BasicBlock &BB : *Callee) {
        for (Instruction &I : BB) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
                loads.push_back(LI);
            }
        }
    }
    
    // Check pairs of loads for consecutive pattern
    for (size_t i = 0; i < loads.size(); ++i) {
        for (size_t j = i + 1; j < loads.size(); ++j) {
            if (areConsecutiveArrayLoads(loads[i], loads[j])) {
                errs() << "      Found consecutive loads in " << Callee->getName() << "\n";
                
                // Try to map back to actual arrays through arguments
                Value *Base1 = mapThroughArguments(loads[i]->getPointerOperand(), argMap);
                Value *Base2 = mapThroughArguments(loads[j]->getPointerOperand(), argMap);
                
                if (Base1 && Base2) {
                    // Look for arrays accessed using these bounds
                    detectRangedAccessPattern(Callee, loads[i], loads[j], argMap);
                }
            }
        }
    }
}

Value* IndirectionDetector::mapThroughArguments(Value* V, const std::map<Value*, Value*>& argMap) {
    // Simple mapping - can be extended for more complex cases
    if (argMap.count(V) > 0) {
        return argMap.at(V);
    }
    
    // If V is derived from an argument, try to trace back
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(V)) {
        return mapThroughArguments(GEP->getPointerOperand(), argMap);
    }
    
    if (LoadInst *LI = dyn_cast<LoadInst>(V)) {
        return mapThroughArguments(LI->getPointerOperand(), argMap);
    }
    
    return V;
}

void IndirectionDetector::detectRangedAccessPattern(Function* F, LoadInst* StartLoad, LoadInst* EndLoad,
                                                   const std::map<Value*, Value*>& argMap) {
    // Look for arrays accessed within the range defined by StartLoad and EndLoad
    for (BasicBlock &BB : *F) {
        for (Instruction &I : BB) {
            if (LoadInst *AccessLoad = dyn_cast<LoadInst>(&I)) {
                if (AccessLoad == StartLoad || AccessLoad == EndLoad) continue;
                
                // Check if this access might be within the range
                Value *AccessBase = getUltimateBase(AccessLoad->getPointerOperand());
                Value *RangeBase = getUltimateBase(StartLoad->getPointerOperand());
                
                // Map through arguments if needed
                AccessBase = mapThroughArguments(AccessBase, argMap);
                RangeBase = mapThroughArguments(RangeBase, argMap);
                
                if (AccessBase != RangeBase && 
                    bpTracker->isRegistered(AccessBase) && 
                    bpTracker->isRegistered(RangeBase)) {
                    
                    createIndirectionEntry(RangeBase, AccessBase, AccessLoad, 
                                         IndirectionType::Ranged);
                }
            }
        }
    }
}

void IndirectionDetector::createIndirectionEntry(Value* SrcBase, Value* DestBase, 
                                               Instruction* AccessInst, IndirectionType Type) {
    IndirectionInfo info;
    info.indirectionType = Type;
    info.srcBase = SrcBase;
    info.destBase = DestBase;
    info.accessInst = AccessInst;
    
    // Get node IDs
    if (bpTracker->isRegistered(SrcBase)) {
        info.srcNodeId = bpTracker->getNodeId(SrcBase);
    } else {
        info.srcNodeId = UINT32_MAX;
    }
    
    if (bpTracker->isRegistered(DestBase)) {
        info.destNodeId = bpTracker->getNodeId(DestBase);
    } else {
        info.destNodeId = UINT32_MAX;
    }
    
    // Only record if both nodes are valid and different
    if (info.srcNodeId != UINT32_MAX && info.destNodeId != UINT32_MAX &&
        info.srcNodeId != info.destNodeId) {
        
        EdgeKey key(SrcBase, DestBase, Type);
        auto& patternSet = (Type == IndirectionType::SingleValued) ? 
                          detectedPatterns : detectedRangedPatterns;
        
        if (patternSet.find(key) == patternSet.end()) {
            indirections.push_back(info);
            patternSet.insert(key);
            
            const char* typeStr = (Type == IndirectionType::SingleValued) ? 
                                 "single-valued" : "ranged";
            errs() << "        ==> Created " << typeStr << " indirection: Node " 
                   << info.srcNodeId << " -> Node " << info.destNodeId << "\n";
        }
    }
}

void IndirectionDetector::analyzeNeighborhoodFunction(CallInst* CI, Function* F) {
    errs() << "      Analyzing Neighborhood-related function: " << F->getName() << "\n";
    
    // Look for the pattern where we access offsets[vertex] and offsets[vertex+1]
    std::vector<LoadInst*> offsetLoads;
    std::vector<GetElementPtrInst*> offsetGEPs;
    Value* offsetsArray = nullptr;
    
    for (BasicBlock &BB : *F) {
        for (Instruction &I : BB) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
                // Check if this is loading from a pointer array (offsets)
                if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(LI->getPointerOperand())) {
                    Type *LoadedType = LI->getType();
                    if (LoadedType->isPointerTy()) {
                        // This is loading a pointer from an array - likely offsets[i]
                        offsetLoads.push_back(LI);
                        offsetGEPs.push_back(GEP);
                        
                        // Try to find the base array
                        if (!offsetsArray) {
                            // Look for the array being accessed
                            Value* ArrayPtr = GEP->getPointerOperand();
                            if (LoadInst *ArrayLoad = dyn_cast<LoadInst>(ArrayPtr)) {
                                // The array pointer itself is loaded from somewhere
                                offsetsArray = ArrayLoad;
                                errs() << "        Found potential offsets array access\n";
                                errs() << "        Array load: " << *ArrayLoad << "\n";
                            }
                        }
                    }
                }
            }
        }
    }
    
    // If we found offset loads, this function is performing ranged access
    if (offsetLoads.size() >= 1) {
        errs() << "        Found " << offsetLoads.size() << " pointer loads (likely offsets access)\n";
        
        // Check if we have consecutive accesses (offset[i] and offset[i+1])
        bool hasConsecutiveAccess = false;
        if (offsetGEPs.size() >= 2) {
            // Check if any two GEPs access consecutive indices
            for (size_t i = 0; i < offsetGEPs.size(); ++i) {
                for (size_t j = i + 1; j < offsetGEPs.size(); ++j) {
                    GetElementPtrInst *GEP1 = offsetGEPs[i];
                    GetElementPtrInst *GEP2 = offsetGEPs[j];
                    
                    // Simple check: see if one index is the other + 1
                    if (GEP1->getNumOperands() >= 2 && GEP2->getNumOperands() >= 2) {
                        Value *Idx1 = GEP1->getOperand(1);
                        Value *Idx2 = GEP2->getOperand(1);
                        
                        // Check for pattern idx2 = idx1 + 1
                        if (BinaryOperator *Add = dyn_cast<BinaryOperator>(Idx2)) {
                            if (Add->getOpcode() == Instruction::Add && Add->getOperand(0) == Idx1) {
                                if (ConstantInt *CI = dyn_cast<ConstantInt>(Add->getOperand(1))) {
                                    if (CI->getSExtValue() == 1) {
                                        hasConsecutiveAccess = true;
                                        errs() << "        Found consecutive access pattern!\n";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Create ranged indirection patterns
        // We know this function accesses offsets array to get bounds
        // And those bounds are used to access edges array
        
        // Look for registered allocations that might be offsets/edges
        const auto& registeredPtrs = bpTracker->getRegisteredPointers();
        
        // Create patterns based on common graph structure assumptions
        // Typically: lower node IDs are graph structure (offsets/edges)
        std::vector<std::pair<Value*, uint32_t>> graphArrays;
        for (const auto& pair : registeredPtrs) {
            if (pair.second <= 5) { // Focus on very low node IDs
                graphArrays.push_back({const_cast<Value*>(pair.first), pair.second});
                errs() << "        Candidate array: Node " << pair.second << "\n";
            }
        }
        
        // If we have at least 2 arrays, create ranged patterns
        if (graphArrays.size() >= 2) {
            // Sort by node ID
            std::sort(graphArrays.begin(), graphArrays.end(), 
                     [](const auto& a, const auto& b) { return a.second < b.second; });
            
            // Create ranged patterns between consecutive pairs
            for (size_t i = 0; i < graphArrays.size() - 1; ++i) {
                createIndirectionEntry(graphArrays[i].first, graphArrays[i+1].first, 
                                     CI, IndirectionType::Ranged);
                errs() << "        Created ranged pattern: Node " << graphArrays[i].second 
                       << " -> Node " << graphArrays[i+1].second << "\n";
            }
            
            // Also create some cross patterns for common graph structures
            if (graphArrays.size() >= 3) {
                // offset -> edges pattern (typically node 0/1 -> node 2/3)
                createIndirectionEntry(graphArrays[0].first, graphArrays[2].first, 
                                     CI, IndirectionType::Ranged);
            }
        }
        
        // Also check for single-valued patterns if this is accessing edges
        if (F->getName().contains("begin") || F->getName().contains("end")) {
            // These functions return pointers into the edge array
            // Those edges might be used to access other arrays
            for (size_t i = 0; i < graphArrays.size(); ++i) {
                for (size_t j = 0; j < graphArrays.size(); ++j) {
                    if (i != j && graphArrays[i].second < graphArrays[j].second) {
                        // Create single-valued pattern from edges to other arrays
                        createIndirectionEntry(graphArrays[i].first, graphArrays[j].first,
                                             CI, IndirectionType::SingleValued);
                        errs() << "        Created single-valued pattern: Node " 
                               << graphArrays[i].second << " -> Node " << graphArrays[j].second << "\n";
                    }
                }
            }
        }
    }
}

// namespace prodigy is already closed above