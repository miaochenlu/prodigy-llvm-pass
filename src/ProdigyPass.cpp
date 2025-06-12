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
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/ConstantFolding.h"

#include "../include/ProdigyDIG.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <algorithm>
#include <queue>
#include <set>

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {

/**
 * @brief Prodigy LLVM Pass - Hardware-Software Co-Design for Prefetching
 * 
 * This pass implements the compiler component of the Prodigy system, which
 * automatically identifies indirect memory access patterns and generates a
 * Data Indirection Graph (DIG) to guide hardware prefetching.
 * 
 * Key features:
 * - Enhanced element size detection for various allocation patterns
 * - Single-valued indirection pattern detection (A[B[i]])
 * - Automatic DIG generation and runtime API instrumentation
 */
class ProdigyPass : public ModulePass {
public:
    static char ID;
    
    ProdigyPass() : ModulePass(ID) {}
    
    bool runOnModule(Module &M) override {
        errs() << "Running Prodigy Pass on module: " << M.getName() << "\n";
        
        // Initialize module-level analysis
        DL = &M.getDataLayout();
        initializeRuntimeFunctions(M);
        
        // Clear global state
        globalAllocations.clear();
        globalPtrToNodeId.clear();
        nextNodeId = 0;
        
        // Phase 1: Collect all allocations across the module
        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            
            if (F.getEntryBlock().size() > 0) {
                SE = &getAnalysis<ScalarEvolutionWrapperPass>(F).getSE();
                collectAllocations(F);
            }
        }
        
        // Phase 2: Analyze each function for indirection patterns
        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            
            errs() << "Analyzing function: " << F.getName() << "\n";
            
            // Clear function-level state
            functionIndirections.clear();
            registeredEdges.clear();
            
            // Identify memory access patterns
            identifySingleValuedIndirections(F);
            
            // Insert runtime API calls
            insertRuntimeCalls(F);
        }
        
        return modified;
    }
    
    void getAnalysisUsage(AnalysisUsage &AU) const override {
        AU.addRequired<LoopInfoWrapperPass>();
        AU.addRequired<ScalarEvolutionWrapperPass>();
        AU.addRequired<MemorySSAWrapperPass>();
        AU.addRequired<DominatorTreeWrapperPass>();
    }

private:
    // === Data Structures ===
    
    /**
     * @brief Information about a memory allocation
     */
    struct AllocInfo {
        CallInst *allocCall;          // The allocation call instruction
        Value *basePtr;               // Base pointer returned by allocation
        Value *numElements;           // Number of elements (may be dynamic)
        Value *elementSize;           // Size of each element (may be dynamic)
        uint32_t nodeId;              // Unique node ID in the DIG
        bool registered = false;      // Whether runtime registration is done
        
        // Enhanced metadata for element size inference
        Type *inferredElementType = nullptr;
        int64_t constantElementSize = -1;  // -1 means unknown
        int64_t constantNumElements = -1;  // -1 means unknown
    };
    
    /**
     * @brief Information about an indirect memory access pattern
     */
    struct IndirectionInfo {
        Instruction *srcLoad;         // Source load instruction (B[i])
        Instruction *destAccess;      // Destination access (A[...])
        Value *srcBase;              // Base address of source array
        Value *destBase;             // Base address of destination array
        prodigy::EdgeType type;      // Type of indirection
    };
    
    /**
     * @brief Unique identifier for DIG edges
     */
    struct EdgeKey {
        Value *srcBase;
        Value *destBase;
        prodigy::EdgeType type;
        
        bool operator==(const EdgeKey &other) const {
            return srcBase == other.srcBase && 
                   destBase == other.destBase && 
                   type == other.type;
        }
    };
    
    struct EdgeKeyHash {
        std::size_t operator()(const EdgeKey &k) const {
            return std::hash<void*>()(k.srcBase) ^ 
                   std::hash<void*>()(k.destBase) ^ 
                   std::hash<int>()(static_cast<int>(k.type));
        }
    };
    
    // === Member Variables ===
    
    bool modified = false;
    const DataLayout *DL = nullptr;
    ScalarEvolution *SE = nullptr;
    
    // Runtime function declarations
    Function *registerNodeFunc = nullptr;
    Function *registerTravEdgeFunc = nullptr;
    Function *registerTrigEdgeFunc = nullptr;
    
    // Global allocation tracking
    std::vector<AllocInfo> globalAllocations;
    std::unordered_map<Value*, uint32_t> globalPtrToNodeId;
    uint32_t nextNodeId = 0;
    
    // Function-level tracking
    std::vector<IndirectionInfo> functionIndirections;
    std::unordered_set<EdgeKey, EdgeKeyHash> registeredEdges;
    
    // === Core Methods ===
    
    /**
     * @brief Initialize runtime API function declarations
     */
    void initializeRuntimeFunctions(Module &M) {
        LLVMContext &Ctx = M.getContext();
        
        // registerNode(void* base_addr, uint64_t num_elements, uint32_t element_size, uint32_t node_id)
        FunctionType *registerNodeTy = FunctionType::get(
            Type::getVoidTy(Ctx),
            {PointerType::getUnqual(Ctx), Type::getInt64Ty(Ctx), 
             Type::getInt32Ty(Ctx), Type::getInt32Ty(Ctx)},
            false
        );
        registerNodeFunc = cast<Function>(
            M.getOrInsertFunction("registerNode", registerNodeTy).getCallee()
        );
        
        // registerTravEdge(void* src_addr, void* dest_addr, uint32_t edge_type)
        FunctionType *registerTravEdgeTy = FunctionType::get(
            Type::getVoidTy(Ctx),
            {PointerType::getUnqual(Ctx), PointerType::getUnqual(Ctx), Type::getInt32Ty(Ctx)},
            false
        );
        registerTravEdgeFunc = cast<Function>(
            M.getOrInsertFunction("registerTravEdge", registerTravEdgeTy).getCallee()
        );
        
        // registerTrigEdge(void* trigger_addr, uint32_t prefetch_params)
        FunctionType *registerTrigEdgeTy = FunctionType::get(
            Type::getVoidTy(Ctx),
            {PointerType::getUnqual(Ctx), Type::getInt32Ty(Ctx)},
            false
        );
        registerTrigEdgeFunc = cast<Function>(
            M.getOrInsertFunction("registerTrigEdge", registerTrigEdgeTy).getCallee()
        );
    }
    
    /**
     * @brief Collect all memory allocations in a function
     */
    void collectAllocations(Function &F) {
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
    }
    
    /**
     * @brief Process a memory allocation and infer element size
     */
    void handleAllocation(CallInst *CI) {
        AllocInfo info;
        info.allocCall = CI;
        info.basePtr = CI;
        info.nodeId = nextNodeId++;
        
        // Use enhanced element size inference
        inferElementSizeFromAllocation(info);
        
        // Ensure valid element size and count
        if (!info.elementSize) {
            info.elementSize = ConstantInt::get(Type::getInt32Ty(CI->getContext()), 1);
        }
        if (!info.numElements) {
            info.numElements = CI->getArgOperand(0);
        }
        
        globalAllocations.push_back(info);
        globalPtrToNodeId[CI] = info.nodeId;
        
        // Debug output
        errs() << "Found allocation: " << *CI << " (Node ID: " << info.nodeId << ")\n";
        if (info.constantElementSize > 0) {
            errs() << "  Element size: " << info.constantElementSize << " bytes\n";
        }
        if (info.constantNumElements > 0) {
            errs() << "  Number of elements: " << info.constantNumElements << "\n";
        }
    }
    
    // === Element Size Detection Methods ===
    
    /**
     * @brief Main element size inference dispatcher
     */
    void inferElementSizeFromAllocation(AllocInfo &info) {
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
    
    /**
     * @brief Infer element size from malloc calls using multiple strategies
     */
    void inferElementSizeFromMalloc(AllocInfo &info) {
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
    
    /**
     * @brief Handle calloc which directly provides count and size
     */
    void inferElementSizeFromCalloc(AllocInfo &info) {
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
    
    /**
     * @brief Analyze malloc argument patterns (n*size, n<<shift, etc.)
     */
    bool analyzeAllocationArgument(Value *sizeArg, AllocInfo &info) {
        // Pattern 1: n * constant (most common)
        Value *LHS, *RHS;
        if (match(sizeArg, m_Mul(m_Value(LHS), m_Value(RHS)))) {
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
        
        // Pattern 2: count << shift (for power-of-2 sizes)
        Value *Count;
        Value *ShiftAmount;
        if (match(sizeArg, m_Shl(m_Value(Count), m_Value(ShiftAmount)))) {
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
        
        return false;
    }
    
    /**
     * @brief Analyze how allocated memory is used to infer element size
     */
    bool analyzeUsagePatterns(AllocInfo &info) {
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
            
            for (User *U : V->users()) {
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
            if (ConstantInt *TotalSize = dyn_cast<ConstantInt>(info.numElements)) {
                int64_t totalBytes = TotalSize->getSExtValue();
                if (totalBytes >= typeSize && totalBytes % typeSize == 0) {
                    info.elementSize = ConstantInt::get(Type::getInt32Ty(info.allocCall->getContext()), typeSize);
                    info.numElements = ConstantInt::get(Type::getInt64Ty(info.allocCall->getContext()), 
                                                       totalBytes / typeSize);
                    info.constantElementSize = typeSize;
                    info.inferredElementType = mostFrequentType;
                    
                    errs() << "  Inferred from frequent type: " << *mostFrequentType 
                           << " (size=" << typeSize << ")\n";
                    return true;
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
    
    /**
     * @brief Analyze GEP instruction patterns to detect stride
     */
    bool analyzeGEPStrides(const std::vector<GetElementPtrInst*> &geps, AllocInfo &info) {
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
                Type *SrcElemTy = GEP->getSourceElementType();
                if (SrcElemTy->isIntegerTy(8)) {
                    isByteIndexing = true;
                    break;
                }
            }
            
            if (isByteIndexing) {
                info.elementSize = ConstantInt::get(Type::getInt32Ty(info.allocCall->getContext()), 
                                                   mostCommonStride);
                info.constantElementSize = mostCommonStride;
                
                if (ConstantInt *TotalSize = dyn_cast<ConstantInt>(info.numElements)) {
                    int64_t totalBytes = TotalSize->getSExtValue();
                    info.numElements = ConstantInt::get(Type::getInt64Ty(info.allocCall->getContext()), 
                                                       totalBytes / mostCommonStride);
                }
                
                errs() << "  Inferred from stride pattern: element size = " << mostCommonStride << "\n";
                return true;
            }
        }
        
        return false;
    }
    
    /**
     * @brief Use SCEV analysis for complex access patterns
     */
    bool analyzeSCEVPatterns(AllocInfo &info) {
        if (!SE) return false;
        
        // Collect all access instructions
        std::vector<Instruction*> accesses;
        for (User *U : info.basePtr->users()) {
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
                                int64_t stepValue = Step->getAPInt().getSExtValue();
                                
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
    
    /**
     * @brief Analyze loop patterns for element size inference
     */
    bool analyzeLoopPatterns(AllocInfo &info, 
                           const std::vector<LoadInst*> &loads,
                           const std::vector<StoreInst*> &stores) {
        Function *F = info.allocCall->getFunction();
        LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(*F).getLoopInfo();
        
        // Check loads in loops
        for (LoadInst *LoadI : loads) {
            if (Loop *L = LI.getLoopFor(LoadI->getParent())) {
                PHINode *IndVar = L->getCanonicalInductionVariable();
                if (!IndVar) continue;
                
                // Check if load address uses induction variable
                if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(LoadI->getPointerOperand())) {
                    for (unsigned i = 0; i < GEP->getNumIndices(); ++i) {
                        Value *Idx = GEP->getOperand(i + 1);
                        if (isRelatedToInductionVariable(Idx, IndVar)) {
                            Type *LoadedType = LoadI->getType();
                            uint64_t typeSize = DL->getTypeStoreSize(LoadedType);
                            
                            info.elementSize = ConstantInt::get(
                                Type::getInt32Ty(info.allocCall->getContext()), typeSize);
                            info.constantElementSize = typeSize;
                            info.inferredElementType = LoadedType;
                            
                            errs() << "  Loop analysis: element size = " << typeSize << "\n";
                            return true;
                        }
                    }
                }
            }
        }
        
        return false;
    }
    
    /**
     * @brief Check if a value is related to an induction variable
     */
    bool isRelatedToInductionVariable(Value *V, PHINode *IndVar) {
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
    
    /**
     * @brief Recursively collect memory access instructions
     */
    void collectAccessInstructions(Value *V, std::vector<Instruction*> &accesses) {
        if (!V || isa<Constant>(V)) return;
        
        for (User *U : V->users()) {
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
    
    /**
     * @brief Handle C++ new/new[] operators
     */
    void inferElementSizeFromNew(AllocInfo &info) {
        Value *sizeArg = info.allocCall->getArgOperand(0);
        
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
        inferElementSizeFromMalloc(info);
    }
    
    // === Indirection Pattern Detection ===
    
    /**
     * @brief Identify single-valued indirection patterns (A[B[i]])
     */
    void identifySingleValuedIndirections(Function &F) {
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (LoadInst *OuterLoad = dyn_cast<LoadInst>(&I)) {
                    if (GetElementPtrInst *OuterGEP = dyn_cast<GetElementPtrInst>(OuterLoad->getPointerOperand())) {
                        // Check each index
                        for (unsigned i = 0; i < OuterGEP->getNumIndices(); ++i) {
                            Value *Index = OuterGEP->getOperand(i + 1);
                            
                            // Trace through type conversions
                            LoadInst *InnerLoad = traceToLoad(Index);
                            
                            if (InnerLoad) {
                                // Found A[B[i]] pattern!
                                errs() << "Found single-valued indirection pattern:\n";
                                errs() << "  Inner load: " << *InnerLoad << "\n";
                                errs() << "  Outer access: " << *OuterLoad << "\n";
                                
                                IndirectionInfo info;
                                info.srcLoad = InnerLoad;
                                info.destAccess = OuterLoad;
                                info.type = prodigy::EdgeType::SINGLE_VALUED;
                                
                                // Get base addresses
                                info.srcBase = getBasePointer(InnerLoad->getPointerOperand());
                                info.destBase = getBasePointer(OuterGEP->getPointerOperand());
                                
                                // Only record if both bases are known allocations
                                if (globalPtrToNodeId.find(info.srcBase) != globalPtrToNodeId.end() &&
                                    globalPtrToNodeId.find(info.destBase) != globalPtrToNodeId.end()) {
                                    functionIndirections.push_back(info);
                                } else {
                                    errs() << "  Skipping edge - base addresses not found in allocations\n";
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    /**
     * @brief Trace a value back to a load instruction through type conversions
     */
    LoadInst* traceToLoad(Value *V) {
        while (V) {
            if (LoadInst *LI = dyn_cast<LoadInst>(V)) {
                return LI;
            } else if (SExtInst *SExt = dyn_cast<SExtInst>(V)) {
                V = SExt->getOperand(0);
            } else if (ZExtInst *ZExt = dyn_cast<ZExtInst>(V)) {
                V = ZExt->getOperand(0);
            } else if (TruncInst *Trunc = dyn_cast<TruncInst>(V)) {
                V = Trunc->getOperand(0);
            } else {
                break;
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Get the base pointer by following the pointer chain
     */
    Value* getBasePointer(Value *ptr) {
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
            return getBasePointer(GEP->getPointerOperand());
        }
        if (BitCastInst *BC = dyn_cast<BitCastInst>(ptr)) {
            return getBasePointer(BC->getOperand(0));
        }
        if (PtrToIntInst *PTI = dyn_cast<PtrToIntInst>(ptr)) {
            return getBasePointer(PTI->getOperand(0));
        }
        return ptr;
    }
    
    // === Runtime API Instrumentation ===
    
    /**
     * @brief Insert runtime API calls for DIG construction
     */
    void insertRuntimeCalls(Function &F) {
        // 1. Insert node registrations
        insertNodeRegistrations(F);
        
        // 2. Insert edge registrations
        insertEdgeRegistrations(F);
        
        // 3. Insert trigger edges
        insertTriggerEdges(F);
    }
    
    /**
     * @brief Insert registerNode calls for allocations in this function
     */
    void insertNodeRegistrations(Function &F) {
        for (AllocInfo &info : globalAllocations) {
            if (info.allocCall->getFunction() == &F && !info.registered) {
                IRBuilder<> Builder(info.allocCall->getNextNode());
                
                Value *basePtrCast = info.basePtr;
                Value *numElemsCast = Builder.CreateZExtOrTrunc(
                    info.numElements, Type::getInt64Ty(F.getContext()));
                Value *elemSizeCast = Builder.CreateZExtOrTrunc(
                    info.elementSize, Type::getInt32Ty(F.getContext()));
                Value *nodeIdVal = ConstantInt::get(
                    Type::getInt32Ty(F.getContext()), info.nodeId);
                
                Builder.CreateCall(registerNodeFunc, 
                    {basePtrCast, numElemsCast, elemSizeCast, nodeIdVal});
                
                errs() << "Inserted registerNode call for node " << info.nodeId;
                if (info.constantElementSize > 0) {
                    errs() << " (element_size=" << info.constantElementSize << ")";
                }
                errs() << "\n";
                
                info.registered = true;
                modified = true;
            }
        }
    }
    
    /**
     * @brief Insert registerTravEdge calls for indirection patterns
     */
    void insertEdgeRegistrations(Function &F) {
        // Collect unique edges
        std::unordered_map<EdgeKey, std::vector<Instruction*>, EdgeKeyHash> edgeUses;
        for (const IndirectionInfo &info : functionIndirections) {
            EdgeKey key = {info.srcBase, info.destBase, info.type};
            edgeUses[key].push_back(info.destAccess);
        }
        
        if (edgeUses.empty()) return;
        
        // Find insertion point (after node registrations)
        BasicBlock &EntryBB = F.getEntryBlock();
        Instruction *insertPt = nullptr;
        
        for (Instruction &I : EntryBB) {
            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                if (CI->getCalledFunction() == registerNodeFunc) {
                    insertPt = CI->getNextNode();
                }
            }
        }
        
        if (!insertPt) {
            insertPt = &*EntryBB.getFirstInsertionPt();
        }
        
        IRBuilder<> Builder(insertPt);
        
        // Insert edge registrations
        for (const auto &pair : edgeUses) {
            const EdgeKey &key = pair.first;
            
            if (registeredEdges.find(key) != registeredEdges.end()) {
                continue;  // Already registered
            }
            
            Value *srcCast = key.srcBase;
            Value *destCast = key.destBase;
            Value *edgeTypeVal = ConstantInt::get(Type::getInt32Ty(F.getContext()), 
                                                   static_cast<uint32_t>(key.type));
            
            Builder.CreateCall(registerTravEdgeFunc, {srcCast, destCast, edgeTypeVal});
            
            errs() << "Inserted registerTravEdge call for edge: " 
                   << key.srcBase->getName() << " -> " << key.destBase->getName() 
                   << " at function entry\n";
            
            registeredEdges.insert(key);
            modified = true;
        }
    }
    
    /**
     * @brief Insert trigger edges for nodes without incoming edges
     */
    void insertTriggerEdges(Function &F) {
        // Collect nodes with incoming edges
        std::unordered_set<Value*> nodesWithIncomingEdges;
        
        for (const EdgeKey &edge : registeredEdges) {
            nodesWithIncomingEdges.insert(edge.destBase);
        }
        
        for (const IndirectionInfo &info : functionIndirections) {
            nodesWithIncomingEdges.insert(info.destBase);
        }
        
        // Insert trigger edges for nodes without incoming edges
        for (const AllocInfo &alloc : globalAllocations) {
            if (alloc.allocCall->getFunction() == &F && alloc.registered) {
                if (nodesWithIncomingEdges.find(alloc.basePtr) == nodesWithIncomingEdges.end()) {
                    // Find where to insert (after registerNode call)
                    Instruction *insertPt = nullptr;
                    for (Instruction &I : *alloc.allocCall->getParent()) {
                        if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                            if (CI->getCalledFunction() == registerNodeFunc) {
                                if (CI->getArgOperand(0) == alloc.basePtr) {
                                    insertPt = CI->getNextNode();
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (insertPt) {
                        IRBuilder<> Builder(insertPt);
                        
                        Value *triggerCast = alloc.basePtr;
                        uint32_t prefetchParams = 2; // look-ahead distance
                        Value *paramsVal = ConstantInt::get(
                            Type::getInt32Ty(F.getContext()), prefetchParams);
                        
                        Builder.CreateCall(registerTrigEdgeFunc, {triggerCast, paramsVal});
                        
                        errs() << "Inserted registerTrigEdge call for trigger node: " 
                               << alloc.basePtr->getName() << " (Node " << alloc.nodeId << ")\n";
                        
                        modified = true;
                    }
                }
            }
        }
    }
};

char ProdigyPass::ID = 0;

} // anonymous namespace

// Register the pass
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