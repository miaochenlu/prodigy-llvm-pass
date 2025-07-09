#include "ElementSizeInference.h"
// #include "llvm/IR/PatternMatch.h"  // Not available in LLVM 3.4.2
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/raw_ostream.h"
#include <queue>
#include <map>

using namespace llvm;
// using namespace llvm::PatternMatch;  // Not available in LLVM 3.4.2

namespace prodigy {

void ElementSizeInference::inferElementSize(AllocInfo& info) {
    CallInst *CI = info.allocCall;
    Function *Callee = CI->getCalledFunction();
    if (!Callee) return;
    
    StringRef FuncName = Callee->getName();
    
    if (FuncName == "malloc") {
        inferElementSizeFromMalloc(info);
    } else if (FuncName == "calloc") {
        inferElementSizeFromCalloc(info);
    } else if (FuncName == "_Znwm" || FuncName == "_Znam") {
        inferElementSizeFromNew(info);
    }
}

void ElementSizeInference::inferElementSizeFromMalloc(AllocInfo& info) {
    Value *sizeArg = info.allocCall->getArgOperand(0);
    
    // Try multiple strategies in order of reliability
    if (analyzeAllocationArgument(sizeArg, info)) {
        return;  // Pattern matching succeeded
    }
    
    if (analyzeUsagePatterns(info)) {
        return;  // Usage pattern analysis succeeded
    }
    
    if (analyzeSCEVPatterns(info)) {
        return;  // SCEV analysis succeeded
    }
    
    // Default: treat as byte array
    info.elementSize = ConstantInt::get(Type::getInt32Ty(info.allocCall->getContext()), 1);
    info.numElements = sizeArg;
    errs() << "  Using default: byte array\n";
}

void ElementSizeInference::inferElementSizeFromCalloc(AllocInfo& info) {
    Value *countArg = info.allocCall->getArgOperand(0);
    Value *sizeArg = info.allocCall->getArgOperand(1);
    
    info.numElements = countArg;
    info.elementSize = sizeArg;
    
    // Extract constant values if available
    if (ConstantInt *CI = dyn_cast<ConstantInt>(sizeArg)) {
        info.constantElementSize = CI->getSExtValue();
        
        // Infer type from size
        switch (info.constantElementSize) {
            case 1: info.inferredElementType = Type::getInt8Ty(info.allocCall->getContext()); break;
            case 2: info.inferredElementType = Type::getInt16Ty(info.allocCall->getContext()); break;
            case 4: info.inferredElementType = Type::getInt32Ty(info.allocCall->getContext()); break;
            case 8: info.inferredElementType = Type::getInt64Ty(info.allocCall->getContext()); break;
        }
    }
    
    if (ConstantInt *CI = dyn_cast<ConstantInt>(countArg)) {
        info.constantNumElements = CI->getSExtValue();
    }
    
    errs() << "  Calloc: " << info.constantNumElements << " elements of " 
           << info.constantElementSize << " bytes\n";
}

void ElementSizeInference::inferElementSizeFromNew(AllocInfo& info) {
    Value *sizeArg = info.allocCall->getArgOperand(0);
    
    // Handle select instructions (conditional size)
    if (SelectInst *SI = dyn_cast<SelectInst>(sizeArg)) {
        // Try both branches of select
        Value *TrueVal = SI->getTrueValue();
        Value *FalseVal = SI->getFalseValue();
        
        // Use the non-negative value if one is -1
        if (ConstantInt *TrueCI = dyn_cast<ConstantInt>(TrueVal)) {
            if (TrueCI->getSExtValue() == -1 && FalseVal) {
                sizeArg = FalseVal;
            }
        }
        if (ConstantInt *FalseCI = dyn_cast<ConstantInt>(FalseVal)) {
            if (FalseCI->getSExtValue() == -1 && TrueVal) {
                sizeArg = TrueVal;
            }
        }
    }
    
    // Check if constant size
    if (ConstantInt *CI = dyn_cast<ConstantInt>(sizeArg)) {
        int64_t size = CI->getSExtValue();
        
        // Common object sizes
        if (size % 8 == 0 && size <= 256) {
            info.elementSize = CI;
            info.numElements = ConstantInt::get(Type::getInt64Ty(info.allocCall->getContext()), 1);
            info.constantElementSize = size;
            errs() << "  New: single object of " << size << " bytes\n";
            return;
        }
    }
    
    // Otherwise use malloc analysis
    info.numElements = sizeArg;  // Store the size argument even if complex
    inferElementSizeFromMalloc(info);
}

bool ElementSizeInference::analyzeAllocationArgument(Value *sizeArg, AllocInfo& info) {
    // Pattern 1: n * constant (most common) 
    // Note: Pattern matching not available in LLVM 3.4.2, use manual analysis
    if (BinaryOperator *MulOp = dyn_cast<BinaryOperator>(sizeArg)) {
        if (MulOp->getOpcode() == Instruction::Mul) {
            Value *LHS = MulOp->getOperand(0);
            Value *RHS = MulOp->getOperand(1);
            ConstantInt *CI = nullptr;
            Value *Count = nullptr;
            
            if ((CI = dyn_cast<ConstantInt>(RHS))) {
                Count = LHS;
            } else if ((CI = dyn_cast<ConstantInt>(LHS))) {
                Count = RHS;
            }
        
        if (CI) {
            int64_t size = CI->getSExtValue();
            
            // Common element sizes
            if (size == 1 || size == 2 || size == 4 || size == 8 || 
                size == 12 || size == 16 || size == 24 || size == 32) {
                info.elementSize = CI;
                info.numElements = Count;
                info.constantElementSize = size;
                
                errs() << "  Pattern: count * " << size << " (likely element size)\n";
                return true;
            }
        }
        }
    }
    
    // Pattern 2: count << shift (for power-of-2 sizes)
    if (BinaryOperator *ShlOp = dyn_cast<BinaryOperator>(sizeArg)) {
        if (ShlOp->getOpcode() == Instruction::Shl) {
            Value *Count = ShlOp->getOperand(0);
            Value *ShiftAmount = ShlOp->getOperand(1);
            if (ConstantInt *CI = dyn_cast<ConstantInt>(ShiftAmount)) {
                int64_t shift = CI->getSExtValue();
                int64_t elemSize = 1LL << shift;
                
                info.elementSize = ConstantInt::get(Type::getInt32Ty(sizeArg->getContext()), elemSize);
                info.numElements = Count;
                info.constantElementSize = elemSize;
                
                errs() << "  Pattern: count << " << shift << " (element size = " << elemSize << ")\n";
                return true;
            }
        }
    }
    
    return false;
}

bool ElementSizeInference::analyzeUsagePatterns(AllocInfo& info) {
    // Collect all memory access operations
    std::vector<GetElementPtrInst*> geps;
    std::vector<LoadInst*> loads;
    std::vector<StoreInst*> stores;
    std::map<Type*, int> typeFrequency;
    
    // BFS traversal of use chain
    std::queue<Value*> worklist;
    std::set<Value*> visited;
    worklist.push(info.basePtr);
    
    while (!worklist.empty()) {
        Value *V = worklist.front();
        worklist.pop();
        
        if (!visited.insert(V).second) continue;
        
        for (Value::use_iterator UI = V->use_begin(), UE = V->use_end(); UI != UE; ++UI) {
            User *U = *UI;
            if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
                geps.push_back(GEP);
                worklist.push(GEP);
            } else if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
                loads.push_back(LI);
                typeFrequency[LI->getType()]++;
            } else if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
                if (SI->getPointerOperand() == V) {
                    stores.push_back(SI);
                    typeFrequency[SI->getValueOperand()->getType()]++;
                }
            } else if (isa<BitCastInst>(U) || isa<PtrToIntInst>(U) || isa<IntToPtrInst>(U)) {
                worklist.push(U);
            }
        }
    }
    
    // Strategy 1: Most frequent access type
    Type *mostFrequentType = nullptr;
    int maxFreq = 0;
    for (const auto &pair : typeFrequency) {
        if (pair.second > maxFreq) {
            maxFreq = pair.second;
            mostFrequentType = pair.first;
        }
    }
    
    if (mostFrequentType && maxFreq >= 2) {
        uint64_t typeSize = DL->getTypeStoreSize(mostFrequentType);
        
        // Verify this size makes sense
        if (info.numElements) {
            if (ConstantInt *TotalSize = dyn_cast<ConstantInt>(info.numElements)) {
                int64_t totalBytes = TotalSize->getSExtValue();
                if (totalBytes >= typeSize && totalBytes % typeSize == 0) {
                    info.elementSize = ConstantInt::get(Type::getInt32Ty(info.allocCall->getContext()), typeSize);
                    info.numElements = ConstantInt::get(Type::getInt64Ty(info.allocCall->getContext()), totalBytes / typeSize);
                    info.constantElementSize = typeSize;
                    info.constantNumElements = totalBytes / typeSize;
                    
                    errs() << "    Inferred from repeated stores:\n";
                    errs() << "      Element size: " << typeSize << " bytes\n";
                    errs() << "      Element type: " << *mostFrequentType << "\n";
                    errs() << "      Number of elements: " << (totalBytes / typeSize) << "\n";
                    return true;
                }
            }
        }
    }
    
    // Strategy 2: Analyze GEP stride patterns
    if (!geps.empty() && analyzeGEPStrides(geps, info)) {
        return true;
    }
    
    // Strategy 3: Loop analysis
    if (analyzeLoopPatterns(info, loads, stores)) {
        return true;
    }
    
    return false;
}

bool ElementSizeInference::analyzeGEPStrides(const std::vector<GetElementPtrInst*>& geps, AllocInfo& info) {
    std::map<int64_t, int> strideFreq;
    
    // Analyze consecutive GEP indices
    for (size_t i = 0; i < geps.size(); ++i) {
        GetElementPtrInst *GEP1 = geps[i];
        if (GEP1->getNumIndices() != 1) continue;
        
        Value *Idx1 = GEP1->getOperand(1);
        ConstantInt *CI1 = dyn_cast<ConstantInt>(Idx1);
        if (!CI1) continue;
        
        for (size_t j = i + 1; j < geps.size(); ++j) {
            GetElementPtrInst *GEP2 = geps[j];
            if (GEP2->getNumIndices() != 1) continue;
            if (GEP2->getPointerOperand() != GEP1->getPointerOperand()) continue;
            
            Value *Idx2 = GEP2->getOperand(1);
            ConstantInt *CI2 = dyn_cast<ConstantInt>(Idx2);
            if (!CI2) continue;
            
            int64_t stride = std::abs(CI2->getSExtValue() - CI1->getSExtValue());
            if (stride > 0 && stride <= 32) {
                strideFreq[stride]++;
            }
        }
    }
    
    // Find most common stride
    int64_t mostCommonStride = 1;
    int maxStrideFreq = 0;
    for (const auto &pair : strideFreq) {
        if (pair.second > maxStrideFreq) {
            maxStrideFreq = pair.second;
            mostCommonStride = pair.first;
        }
    }
    
    // If stride > 1, might indicate element size
    if (mostCommonStride > 1 && maxStrideFreq >= 2) {
        // Check if this is byte indexing
        bool isByteIndexing = false;
        for (GetElementPtrInst *GEP : geps) {
            // getSourceElementType() not available in LLVM 3.4.2
            // Use pointed-to type instead
            Type *SrcElemTy = GEP->getPointerOperandType()->getPointerElementType();
            if (SrcElemTy->isIntegerTy(8)) {
                isByteIndexing = true;
                break;
            }
        }
        
        if (isByteIndexing) {
            info.elementSize = ConstantInt::get(Type::getInt32Ty(info.allocCall->getContext()), 
                                               mostCommonStride);
            info.constantElementSize = mostCommonStride;
            
            if (info.numElements) {
                if (ConstantInt *TotalSize = dyn_cast<ConstantInt>(info.numElements)) {
                    int64_t totalBytes = TotalSize->getSExtValue();
                    info.numElements = ConstantInt::get(Type::getInt64Ty(info.allocCall->getContext()), 
                                                       totalBytes / mostCommonStride);
                }
            }
            
            errs() << "  Inferred from stride pattern: element size = " << mostCommonStride << "\n";
            return true;
        }
    }
    
    return false;
}

bool ElementSizeInference::analyzeSCEVPatterns(AllocInfo& info) {
    if (!SE) return false;
    
    // Collect all access instructions
    std::vector<Instruction*> accesses;
    for (Value::use_iterator UI = info.basePtr->use_begin(), UE = info.basePtr->use_end(); UI != UE; ++UI) {
        User *U = *UI;
        if (Instruction *I = dyn_cast<Instruction>(U)) {
            collectAccessInstructions(I, accesses);
        }
    }
    
    // Analyze access patterns
    for (Instruction *I : accesses) {
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I)) {
            if (GEP->getNumIndices() == 1) {
                const SCEV *IndexSCEV = SE->getSCEV(GEP->getOperand(1));
                
                // Check for affine expressions (a*i + b)
                if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(IndexSCEV)) {
                    if (AR->isAffine()) {
                        const SCEV *StepSCEV = AR->getStepRecurrence(*SE);
                        if (const SCEVConstant *Step = dyn_cast<SCEVConstant>(StepSCEV)) {
                            // getAPInt() not available in LLVM 3.4.2, use getValue() instead
                            int64_t stepValue = Step->getValue()->getSExtValue();
                            
                            if (stepValue > 1) {
                                errs() << "  SCEV: Found affine access with step " << stepValue << "\n";
                                
                                info.elementSize = ConstantInt::get(
                                    Type::getInt32Ty(info.allocCall->getContext()), 
                                    stepValue);
                                info.constantElementSize = stepValue;
                                
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }
    
    return false;
}

bool ElementSizeInference::analyzeLoopPatterns(AllocInfo& info, 
                                               const std::vector<LoadInst*>& loads,
                                               const std::vector<StoreInst*>& stores) {
    Function *F = info.allocCall->getParent()->getParent();
    
    // Note: We need LoopInfo analysis to be available
    // For now, we'll use a simplified pattern matching approach
    
    // Check loads for common patterns
    for (LoadInst *LoadI : loads) {
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(LoadI->getPointerOperand())) {
            Type *LoadedType = LoadI->getType();
            uint64_t typeSize = DL->getTypeStoreSize(LoadedType);
            
            // If we see consistent access with a specific type, infer element size
            if (typeSize > 1 && typeSize <= 32) {
                info.elementSize = ConstantInt::get(
                    Type::getInt32Ty(info.allocCall->getContext()), typeSize);
                info.constantElementSize = typeSize;
                info.inferredElementType = LoadedType;
                
                errs() << "  Loop analysis: element size = " << typeSize << "\n";
                return true;
            }
        }
    }
    
    return false;
}

bool ElementSizeInference::isRelatedToInductionVariable(Value* V, PHINode* IndVar) {
    if (V == IndVar) return true;
    
    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(V)) {
        return isRelatedToInductionVariable(BO->getOperand(0), IndVar) ||
               isRelatedToInductionVariable(BO->getOperand(1), IndVar);
    }
    
    if (CastInst *CI = dyn_cast<CastInst>(V)) {
        return isRelatedToInductionVariable(CI->getOperand(0), IndVar);
    }
    
    return false;
}

void ElementSizeInference::collectAccessInstructions(Value* V, std::vector<Instruction*>& accesses) {
    if (!V || isa<Constant>(V)) return;
    
    for (Value::use_iterator UI = V->use_begin(), UE = V->use_end(); UI != UE; ++UI) {
        User *U = *UI;
        if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
            accesses.push_back(LI);
        } else if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
            if (SI->getPointerOperand() == V) {
                accesses.push_back(SI);
            }
        } else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
            accesses.push_back(GEP);
            collectAccessInstructions(GEP, accesses);
        } else if (isa<BitCastInst>(U) || isa<PtrToIntInst>(U)) {
            collectAccessInstructions(U, accesses);
        }
    }
}

void ElementSizeInference::inferFromStructAllocation(AllocInfo& info) {
    Function *F = info.allocCall->getParent()->getParent();
    
    errs() << "  Checking for struct allocation patterns...\n";
    // TODO: Implement struct allocation detection
}

} // namespace prodigy 