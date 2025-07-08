#include "DIGInsertion.h"
#include "ProdigyTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"
#include <queue>
#include <unordered_map>
#include <map>

using namespace llvm;
using namespace prodigy;

// DIG function ID definitions (from dig_print.h)
#define BaseOffset32 0
#define BaseOffset64 1
#define PointerBounds32 2
#define PointerBounds64 3
#define TraversalHolder 4
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
#define SquashIfLarger 22
#define SquashIfSmaller 23
#define NeverSquash 24
#define InvalidFunc 25

// Function name macros
#define DIG_FUNC_NAME(func_id) \
    ((func_id) == 0 ? "BaseOffset32" : \
     (func_id) == 1 ? "BaseOffset64" : \
     (func_id) == 2 ? "PointerBounds32" : \
     (func_id) == 3 ? "PointerBounds64" : \
     (func_id) == 4 ? "TraversalHolder" : \
     "InvalidTraversal")

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

#define DIG_SQUASH_NAME(func_id) \
    ((func_id) == 22 ? "SquashIfLarger" : \
     (func_id) == 23 ? "SquashIfSmaller" : \
     (func_id) == 24 ? "NeverSquash" : \
     "InvalidSquash")

namespace prodigy {

DIGInsertion::DIGInsertion() {
    // Default constructor
}

void DIGInsertion::initializeRuntimeFunctions(Module& module) {
    LLVMContext &Ctx = module.getContext();
    
    // Initialize printf function for DIG print mode
    FunctionType *printfTy = FunctionType::get(
        Type::getInt32Ty(Ctx),
        {PointerType::getUnqual(Ctx)},
        true  // variadic
    );
    FunctionCallee printfCallee = module.getOrInsertFunction("printf", printfTy);
    printfFunc = dyn_cast<Function>(printfCallee.getCallee());
    
    // Don't create format strings as global variables - they will be created inline
    // when needed in the actual printf calls
    
    // Create dummy function declarations for compatibility
    FunctionType *registerNodeTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PointerType::getUnqual(Ctx), Type::getInt64Ty(Ctx), 
         Type::getInt32Ty(Ctx), Type::getInt32Ty(Ctx)},
        false
    );
    
    FunctionType *registerTravEdgeTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PointerType::getUnqual(Ctx), PointerType::getUnqual(Ctx), Type::getInt32Ty(Ctx)},
        false
    );
    
    FunctionType *registerTrigEdgeTy = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PointerType::getUnqual(Ctx), Type::getInt32Ty(Ctx)},
        false
    );
    
    registerNodeFunc = Function::Create(registerNodeTy, Function::ExternalLinkage, 
                                      "__dig_print_register_node", &module);
    registerTravEdgeFunc = Function::Create(registerTravEdgeTy, Function::ExternalLinkage,
                                          "__dig_print_register_trav_edge", &module);
    registerTrigEdgeFunc = Function::Create(registerTrigEdgeTy, Function::ExternalLinkage,
                                          "__dig_print_register_trig_edge", &module);
}

void DIGInsertion::insertGlobalDIGHeader(Module& module) {
    // Find the main function
    Function *mainFunc = module.getFunction("main");
    if (!mainFunc) {
        errs() << "Warning: No main function found, skipping global DIG header\n";
        return;
    }
    
    // Check if printf is initialized
    if (!printfFunc) {
        errs() << "Error: printf function not initialized\n";
        return;
    }
    
    BasicBlock &EntryBB = mainFunc->getEntryBlock();
    IRBuilder<> Builder(&*EntryBB.getFirstInsertionPt());
    
    // Insert header comments
    std::string header = "# DIG Configuration for SSSP\n# Generated from Prodigy LLVM Pass\n\n";
    Value *headerVal = Builder.CreateGlobalStringPtr(header);
    Builder.CreateCall(printfFunc, {headerVal});
    
    errs() << "Inserted global DIG header in main function\n";
}

void DIGInsertion::insertDIGHeader(Function &) {
    // Skip individual function headers - we now use a single global header
    return;
}

void DIGInsertion::insertRuntimeCalls(Function& F, 
                                      const std::vector<AllocInfo>& allocations,
                                      const std::vector<IndirectionInfo>& indirections,
                                      std::unordered_set<EdgeKey, EdgeKeyHash>& registeredEdges) {
    
    if (allocations.empty() && indirections.empty()) {
        return;
    }
    
    // Special handling for main function
    if (F.getName() == "main") {
        // Insert header at the beginning
        insertDIGHeader(F);
        
        // First, insert nodes right after their allocations
        insertNodeRegistrations(F, allocations);
        
        // Find the last node registration to insert edges after
        Instruction* lastNodeRegistration = nullptr;
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                    if (CI->getCalledFunction() && 
                        CI->getCalledFunction()->getName() == "printf") {
                        // Check if this is a node registration
                        if (CI->getNumOperands() >= 5) {
                            lastNodeRegistration = &I;
                        }
                    }
                }
            }
        }
        
        // Insert edges after the last node registration
        if (!indirections.empty() && lastNodeRegistration) {
            insertEdges(F, indirections, registeredEdges, lastNodeRegistration);
        }
        
        // Insert trigger edges last
        insertTriggerEdges(F, allocations, indirections, registeredEdges);
    } else {
        // For non-main functions, insert everything at entry
        insertNodeRegistrations(F, allocations);
        insertEdges(F, indirections, registeredEdges);
        insertTriggerEdges(F, allocations, indirections, registeredEdges);
    }
}

uint32_t DIGInsertion::getTraversalFunctionId(IndirectionType type) {
    switch (type) {
        case IndirectionType::SingleValued:
            // For single-valued indirection, use BaseOffset64
            return BaseOffset64;  // Function ID 1
        case IndirectionType::Ranged:
            // For ranged indirection, use PointerBounds64
            return PointerBounds64;  // Function ID 3
        default:
            return InvalidFunc;
    }
}

uint32_t DIGInsertion::getTriggerFunctionId() {
    // For graph algorithms with irregular access patterns,
    // UpToOffset is more appropriate than StaticOffset_8
    // because it can handle dynamic ranges like offset[i] to offset[i+1]
    return UpToOffset;  // Function ID 5 - better for irregular patterns
    
    // Alternative options:
    // - StaticOffset_8 (ID: 9): Fixed 8-element offset, good for regular strides
    // - StaticUpToOffset_8_16 (ID: 14): Range-based prefetching
    // - TriggerHolder (ID: 13): Placeholder for custom logic
}

uint32_t DIGInsertion::getSquashFunctionId() {
    // Default to NeverSquash
    return NeverSquash;  // Function ID 24
}

uint32_t DIGInsertion::getTriggerFunctionForNode(uint32_t nodeId,
                                                const std::vector<AllocInfo>& allocations,
                                                const std::vector<IndirectionInfo>& indirections) {
    // Calculate the DIG depth from this node
    int depth = calculateDIGDepthFromNode(nodeId, indirections);
    
    // Select trigger function based on depth
    // According to the paper:
    // - For depth >= 4, use look-ahead distance of 1
    // - For smaller depths, use larger look-ahead distances
    if (depth >= 4) {
        return StaticOffset_1;     // Look-ahead of 1
    } else if (depth == 3) {
        return StaticOffset_2;     // Look-ahead of 2
    } else if (depth == 2) {
        return StaticOffset_8;     // Look-ahead of 8
    } else {
        return StaticOffset_16;    // Look-ahead of 16 for shallow DIGs
    }
}

int DIGInsertion::calculateDIGDepthFromNode(uint32_t nodeId,
                                          const std::vector<IndirectionInfo>& indirections) {
    // BFS to find the maximum depth from this node
    std::unordered_map<uint32_t, int> nodeDepths;
    std::queue<uint32_t> toVisit;
    
    toVisit.push(nodeId);
    nodeDepths[nodeId] = 0;
    
    int maxDepth = 0;
    
    while (!toVisit.empty()) {
        uint32_t currentNode = toVisit.front();
        toVisit.pop();
        
        // Find all edges from currentNode
        for (const IndirectionInfo &info : indirections) {
            if (info.srcNodeId == currentNode) {
                int newDepth = nodeDepths[currentNode] + 1;
                if (nodeDepths.find(info.destNodeId) == nodeDepths.end() || 
                    nodeDepths[info.destNodeId] > newDepth) {
                    nodeDepths[info.destNodeId] = newDepth;
                    toVisit.push(info.destNodeId);
                    maxDepth = std::max(maxDepth, newDepth);
                }
            }
        }
    }
    
    return maxDepth;
}

void DIGInsertion::insertNodeRegistrations(Function &F, const std::vector<AllocInfo>& allocations) {
    LLVMContext &Ctx = F.getContext();
    
    for (const AllocInfo &info : allocations) {
        if (info.allocCall->getFunction() == &F && !info.registered) {
            IRBuilder<> Builder(info.allocCall->getNextNode());
            
            // One-time guard: use a global i1 flag, print only when first seen.
            Module *M = F.getParent();
            std::string flagName = "__dig_node_done_" + std::to_string(info.nodeId);
            GlobalVariable *printedFlag = M->getGlobalVariable(flagName);
            if (!printedFlag) {
                printedFlag = new GlobalVariable(*M, Type::getInt1Ty(Ctx), /*isConstant*/false,
                                               GlobalValue::InternalLinkage,
                                               ConstantInt::getFalse(Ctx), flagName);
            }

            // Load flag and decide whether to print
            Value *flagVal = Builder.CreateLoad(printedFlag->getValueType(), printedFlag);
            Value *needPrint = Builder.CreateICmpEQ(flagVal, ConstantInt::getFalse(Ctx));

            std::string formatStr = "NODE %d 0x%lx %ld %ld\n";
            Value *formatStrValTrue = Builder.CreateGlobalStringPtr(formatStr);
            Value *formatStrValFalse = Builder.CreateGlobalStringPtr("\0"); // empty string ⇒ no output
            Value *formatStrVal = Builder.CreateSelect(needPrint, formatStrValTrue, formatStrValFalse);

            Value *nodeIdVal = ConstantInt::get(Type::getInt32Ty(Ctx), info.nodeId);
            Value *basePtrInt = Builder.CreatePtrToInt(info.basePtr, Type::getInt64Ty(Ctx));

            // Cast numElements
            Value *numElemsCast;
            Value *numElements = info.numElements ? info.numElements : ConstantInt::get(Type::getInt64Ty(Ctx), 1);
            if (numElements->getType()->isIntegerTy()) {
                numElemsCast = Builder.CreateZExtOrTrunc(numElements, Type::getInt64Ty(Ctx));
            } else if (numElements->getType()->isPointerTy()) {
                numElemsCast = Builder.CreatePtrToInt(numElements, Type::getInt64Ty(Ctx));
            } else {
                numElemsCast = ConstantInt::get(Type::getInt64Ty(Ctx), 1);
            }

            // Cast elementSize
            Value *elemSizeCast;
            Value *elementSize = info.elementSize ? info.elementSize : ConstantInt::get(Type::getInt32Ty(Ctx), 1);
            if (elementSize->getType()->isIntegerTy()) {
                elemSizeCast = Builder.CreateZExtOrTrunc(elementSize, Type::getInt64Ty(Ctx));
            } else if (elementSize->getType()->isPointerTy()) {
                elemSizeCast = Builder.CreatePtrToInt(elementSize, Type::getInt64Ty(Ctx));
            } else {
                elemSizeCast = ConstantInt::get(Type::getInt64Ty(Ctx), 1);
            }

            Builder.CreateCall(printfFunc, {formatStrVal, nodeIdVal, basePtrInt, numElemsCast, elemSizeCast});
            
            // Mark as printed: store true
            Builder.CreateStore(ConstantInt::getTrue(Ctx), printedFlag);
            
            errs() << "Inserted DIG_REGISTER_NODE printf for node " << info.nodeId;
            if (info.constantElementSize > 0) {
                errs() << " (element_size=" << info.constantElementSize << ")";
            }
            errs() << "\n";
            
            const_cast<AllocInfo&>(info).registered = true;
        }
    }
}

void DIGInsertion::insertEdges(Function &F, 
                               const std::vector<IndirectionInfo>& indirections,
                               std::unordered_set<EdgeKey, EdgeKeyHash>& registeredEdges,
                               Instruction* insertAfter) {
    LLVMContext &Ctx = F.getContext();
    
    errs() << "insertEdges: Processing " << indirections.size() << " indirections\n";
    
    if (indirections.empty()) {
        return;
    }
    
    // Find a good insertion point in the main function
    Function *mainFunc = F.getParent()->getFunction("main");
    if (!mainFunc) {
        errs() << "Warning: No main function found for edge insertion\n";
        return;
    }
    
    // Find the last node registration in main to insert edges after
    Instruction *insertPt = nullptr;
    for (BasicBlock &BB : *mainFunc) {
        for (Instruction &I : BB) {
            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                if (CI->getCalledFunction() && CI->getCalledFunction()->getName() == "printf") {
                    // Check if this is a NODE registration (has 5 args)
                    if (CI->arg_size() >= 5) {
                        insertPt = CI->getNextNode();
                    }
                }
            }
        }
    }
    
    if (!insertPt) {
        // If no node registrations found, insert at beginning of main
        BasicBlock &EntryBB = mainFunc->getEntryBlock();
        insertPt = &*EntryBB.getFirstInsertionPt();
    }
    
    IRBuilder<> Builder(insertPt);
    
    // Process all edges
    int edgeCount = 0;
    for (const IndirectionInfo &info : indirections) {
        EdgeKey key(info.srcBase, info.destBase, info.indirectionType);
        
        if (registeredEdges.find(key) != registeredEdges.end()) {
            continue;  // Already registered
        }
        
        // Skip invalid edges
        if (info.srcNodeId == UINT32_MAX || info.destNodeId == UINT32_MAX) {
            errs() << "  Skipping edge with invalid node IDs\n";
            continue;
        }
        
        // Create format string for edges
        uint32_t funcId = getTraversalFunctionId(info.indirectionType);
        std::string funcName = (funcId < InvalidFunc) ? DIG_FUNC_NAME(funcId) : "Unknown";
        std::string formatStr = "EDGE %d %d %d  # " + funcName + "\n";
        
        Value *formatStrVal = Builder.CreateGlobalStringPtr(formatStr);
        
        Value *srcNodeIdVal = ConstantInt::get(Type::getInt32Ty(Ctx), info.srcNodeId);
        Value *destNodeIdVal = ConstantInt::get(Type::getInt32Ty(Ctx), info.destNodeId);
        Value *funcIdVal = ConstantInt::get(Type::getInt32Ty(Ctx), funcId);
        
        Builder.CreateCall(printfFunc, {formatStrVal, srcNodeIdVal, destNodeIdVal, funcIdVal});
        
        // Record the edge
        registeredEdges.insert(key);
        edgeCount++;
        
        errs() << "  Inserted EDGE: Node " << info.srcNodeId << " -> Node " 
               << info.destNodeId << " (" << funcName << ")\n";
    }
    
    errs() << "insertEdges: Inserted " << edgeCount << " edges\n";
}

void DIGInsertion::insertTriggerEdges(Function &F, const std::vector<AllocInfo>& allocations,
                                    const std::vector<IndirectionInfo>& indirections,
                                    std::unordered_set<EdgeKey, EdgeKeyHash>& registeredEdges) {
    LLVMContext &Ctx = F.getContext();
    
    // Collect nodes with incoming edges
    std::unordered_set<Value*> nodesWithIncomingEdges;
    
    // Collect nodes with incoming edges from registered indirections
    for (const IndirectionInfo &info : indirections) {
        nodesWithIncomingEdges.insert(info.destBase);
    }
    
    // Also check the registered edges set
    for (const EdgeKey &edge : registeredEdges) {
        nodesWithIncomingEdges.insert(edge.destBase);
    }
    
    // Insert trigger edges for nodes without incoming edges
    for (const AllocInfo &alloc : allocations) {
        if (alloc.allocCall->getFunction() == &F && alloc.registered) {
            if (nodesWithIncomingEdges.find(alloc.basePtr) == nodesWithIncomingEdges.end()) {
                // Find where to insert (after node registration)
                Instruction *insertPt = alloc.allocCall->getNextNode();
                
                // Look for the printf call that registered this node
                for (Instruction &I : *alloc.allocCall->getParent()) {
                    if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                        if (CI->getCalledFunction() && CI->getCalledFunction()->getName() == "printf") {
                            // Check if this is a NODE registration
                            if (CI->arg_size() >= 5) {  // NODE format has 5 args
                                insertPt = CI->getNextNode();
                            }
                        }
                    }
                }
                
                if (insertPt) {
                    IRBuilder<> Builder(insertPt);
                    
                    // Add one-time guard for trigger printing
                    Module *M = F.getParent();
                    std::string flagName = "__dig_trigger_done_" + std::to_string(alloc.nodeId);
                    GlobalVariable *printedFlag = M->getGlobalVariable(flagName);
                    if (!printedFlag) {
                        printedFlag = new GlobalVariable(*M, Type::getInt1Ty(Ctx), /*isConstant*/false,
                                                       GlobalValue::InternalLinkage,
                                                       ConstantInt::getFalse(Ctx), flagName);
                    }
                    
                    // Load flag and check if we should print
                    Value *flagVal = Builder.CreateLoad(printedFlag->getValueType(), printedFlag);
                    Value *needPrint = Builder.CreateICmpEQ(flagVal, ConstantInt::getFalse(Ctx));
                    
                    // Create format string for trigger edge
                    uint32_t nodeId = alloc.nodeId;
                    uint32_t triggerFunc = getTriggerFunctionForNode(nodeId, allocations, indirections);
                    uint32_t squashFunc = getSquashFunctionId();
                    
                    std::string formatStr = "TRIGGER %d %d %d %d  # " + 
                                            std::string(DIG_TRIGGER_NAME(triggerFunc)) + ", " +
                                            std::string(DIG_SQUASH_NAME(squashFunc)) + "\n";
                    std::string emptyStr = "";
                    
                    Value *formatStrValTrue = Builder.CreateGlobalStringPtr(formatStr);
                    Value *formatStrValFalse = Builder.CreateGlobalStringPtr(emptyStr);
                    Value *formatStrVal = Builder.CreateSelect(needPrint, formatStrValTrue, formatStrValFalse);
                    
                    Value *srcNodeIdVal = ConstantInt::get(Type::getInt32Ty(Ctx), nodeId);
                    Value *destNodeIdVal = ConstantInt::get(Type::getInt32Ty(Ctx), nodeId);  // Self-edge
                    Value *triggerFuncVal = ConstantInt::get(Type::getInt32Ty(Ctx), triggerFunc);
                    Value *squashFuncVal = ConstantInt::get(Type::getInt32Ty(Ctx), squashFunc);
                    
                    Builder.CreateCall(printfFunc, {formatStrVal, srcNodeIdVal, destNodeIdVal,
                                                    triggerFuncVal, squashFuncVal});
                    
                    // Mark as printed
                    Builder.CreateStore(ConstantInt::getTrue(Ctx), printedFlag);
                    
                    errs() << "Inserted DIG_REGISTER_TRIG_EDGE printf for trigger node: " 
                           << alloc.basePtr->getName() << " (Node " << nodeId << ")\n";
                }
            }
        }
    }
}

} // namespace prodigy 