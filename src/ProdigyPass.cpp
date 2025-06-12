#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Constants.h"

#include "../include/ProdigyDIG.h"
#include <unordered_map>
#include <vector>

using namespace llvm;

namespace {

class ProdigyPass : public ModulePass {
public:
    static char ID;
    
    ProdigyPass() : ModulePass(ID) {}
    
    bool runOnModule(Module &M) override {
        errs() << "Running Prodigy Pass on module: " << M.getName() << "\n";
        
        // 初始化运行时函数
        initializeRuntimeFunctions(M);
        
        // 遍历所有函数
        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            
            errs() << "Analyzing function: " << F.getName() << "\n";
            
            // Phase 1: 识别内存分配
            identifyAllocations(F);
            
            // Phase 2: 识别单值间接访问模式
            identifySingleValuedIndirections(F);
            
            // Phase 3: 识别范围间接访问模式 (后续实现)
            // identifyRangedIndirections(F);
            
            // Phase 4: 插入运行时API调用
            insertRuntimeCalls(F);
        }
        
        return modified;
    }
    
    void getAnalysisUsage(AnalysisUsage &AU) const override {
        AU.addRequired<LoopInfoWrapperPass>();
        AU.addRequired<ScalarEvolutionWrapperPass>();
        AU.addRequired<MemorySSAWrapperPass>();
    }

private:
    bool modified = false;
    
    // 运行时函数声明
    Function *registerNodeFunc = nullptr;
    Function *registerTravEdgeFunc = nullptr;
    Function *registerTrigEdgeFunc = nullptr;
    
    // 分配信息
    struct AllocInfo {
        CallInst *allocCall;
        Value *basePtr;
        Value *numElements;
        Value *elementSize;
        uint32_t nodeId;
    };
    
    // 间接访问信息
    struct IndirectionInfo {
        Instruction *srcLoad;      // 源加载指令
        Instruction *destAccess;   // 目标访问指令
        Value *srcBase;           // 源基地址
        Value *destBase;          // 目标基地址
        prodigy::EdgeType type;   // 间接访问类型
    };
    
    std::vector<AllocInfo> allocations;
    std::vector<IndirectionInfo> indirections;
    std::unordered_map<Value*, uint32_t> ptrToNodeId;
    uint32_t nextNodeId = 0;
    
    void initializeRuntimeFunctions(Module &M) {
        LLVMContext &Ctx = M.getContext();
        
        // registerNode(void* base_addr, uint64_t num_elements, uint32_t element_size, uint32_t node_id)
        FunctionType *registerNodeTy = FunctionType::get(
            Type::getVoidTy(Ctx),
            {Type::getInt8PtrTy(Ctx), Type::getInt64Ty(Ctx), 
             Type::getInt32Ty(Ctx), Type::getInt32Ty(Ctx)},
            false
        );
        registerNodeFunc = cast<Function>(
            M.getOrInsertFunction("registerNode", registerNodeTy).getCallee()
        );
        
        // registerTravEdge(void* src_addr, void* dest_addr, uint32_t edge_type)
        FunctionType *registerTravEdgeTy = FunctionType::get(
            Type::getVoidTy(Ctx),
            {Type::getInt8PtrTy(Ctx), Type::getInt8PtrTy(Ctx), Type::getInt32Ty(Ctx)},
            false
        );
        registerTravEdgeFunc = cast<Function>(
            M.getOrInsertFunction("registerTravEdge", registerTravEdgeTy).getCallee()
        );
        
        // registerTrigEdge(void* trigger_addr, uint32_t prefetch_params)
        FunctionType *registerTrigEdgeTy = FunctionType::get(
            Type::getVoidTy(Ctx),
            {Type::getInt8PtrTy(Ctx), Type::getInt32Ty(Ctx)},
            false
        );
        registerTrigEdgeFunc = cast<Function>(
            M.getOrInsertFunction("registerTrigEdge", registerTrigEdgeTy).getCallee()
        );
    }
    
    void identifyAllocations(Function &F) {
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                    Function *Callee = CI->getCalledFunction();
                    if (!Callee) continue;
                    
                    StringRef FuncName = Callee->getName();
                    
                    // 检测malloc调用
                    if (FuncName == "malloc") {
                        handleMalloc(CI);
                    }
                    // TODO: 处理calloc, realloc, new等
                }
            }
        }
    }
    
    void handleMalloc(CallInst *CI) {
        // malloc(size) - 需要推断元素数量和大小
        Value *sizeArg = CI->getArgOperand(0);
        
        AllocInfo info;
        info.allocCall = CI;
        info.basePtr = CI;
        info.nodeId = nextNodeId++;
        
        // 简单情况：malloc(n * sizeof(type))
        // TODO: 使用更复杂的分析来推断数组大小和元素大小
        if (BinaryOperator *BO = dyn_cast<BinaryOperator>(sizeArg)) {
            if (BO->getOpcode() == Instruction::Mul) {
                info.numElements = BO->getOperand(0);
                info.elementSize = BO->getOperand(1);
            }
        } else {
            // 默认假设是字节数组
            info.numElements = sizeArg;
            info.elementSize = ConstantInt::get(Type::getInt32Ty(CI->getContext()), 1);
        }
        
        allocations.push_back(info);
        ptrToNodeId[CI] = info.nodeId;
        
        errs() << "Found allocation: " << *CI << " (Node ID: " << info.nodeId << ")\n";
    }
    
    void identifySingleValuedIndirections(Function &F) {
        // 查找 A[B[i]] 模式
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (LoadInst *OuterLoad = dyn_cast<LoadInst>(&I)) {
                    if (GetElementPtrInst *OuterGEP = dyn_cast<GetElementPtrInst>(OuterLoad->getPointerOperand())) {
                        // 检查索引是否来自另一个加载（可能经过类型转换）
                        for (unsigned i = 0; i < OuterGEP->getNumIndices(); ++i) {
                            Value *Index = OuterGEP->getOperand(i + 1);  // GEP indices start at operand 1
                            
                            // 追踪索引的来源，跳过类型转换
                            LoadInst *InnerLoad = nullptr;
                            Value *CurrentValue = Index;
                            
                            // 处理可能的类型转换链
                            while (CurrentValue && !InnerLoad) {
                                if (LoadInst *LI = dyn_cast<LoadInst>(CurrentValue)) {
                                    InnerLoad = LI;
                                } else if (SExtInst *SExt = dyn_cast<SExtInst>(CurrentValue)) {
                                    CurrentValue = SExt->getOperand(0);
                                } else if (ZExtInst *ZExt = dyn_cast<ZExtInst>(CurrentValue)) {
                                    CurrentValue = ZExt->getOperand(0);
                                } else if (TruncInst *Trunc = dyn_cast<TruncInst>(CurrentValue)) {
                                    CurrentValue = Trunc->getOperand(0);
                                } else {
                                    break;
                                }
                            }
                            
                            if (InnerLoad) {
                                // 找到 A[B[i]] 模式！
                                errs() << "Found single-valued indirection pattern:\n";
                                errs() << "  Inner load: " << *InnerLoad << "\n";
                                errs() << "  Outer access: " << *OuterLoad << "\n";
                                
                                IndirectionInfo info;
                                info.srcLoad = InnerLoad;
                                info.destAccess = OuterLoad;
                                info.type = prodigy::EdgeType::SINGLE_VALUED;
                                
                                // 获取基地址
                                info.srcBase = getBasePointer(InnerLoad->getPointerOperand());
                                info.destBase = getBasePointer(OuterGEP->getPointerOperand());
                                
                                indirections.push_back(info);
                            }
                        }
                    }
                }
            }
        }
    }
    
    Value* getBasePointer(Value *ptr) {
        // 简单实现：追踪到分配点
        // TODO: 使用更复杂的别名分析
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
            return getBasePointer(GEP->getPointerOperand());
        }
        if (BitCastInst *BC = dyn_cast<BitCastInst>(ptr)) {
            return getBasePointer(BC->getOperand(0));
        }
        return ptr;
    }
    
    void insertRuntimeCalls(Function &F) {
        if (allocations.empty() && indirections.empty()) return;
        
        // 在函数入口插入节点注册调用
        BasicBlock &EntryBB = F.getEntryBlock();
        IRBuilder<> Builder(&EntryBB, EntryBB.getFirstInsertionPt());
        
        // 注册所有节点
        for (const AllocInfo &info : allocations) {
            // 在分配后立即插入registerNode调用
            Builder.SetInsertPoint(info.allocCall->getNextNode());
            
            Value *basePtrCast = Builder.CreateBitCast(info.basePtr, Type::getInt8PtrTy(F.getContext()));
            Value *numElemsCast = Builder.CreateZExtOrTrunc(info.numElements, Type::getInt64Ty(F.getContext()));
            Value *elemSizeCast = Builder.CreateZExtOrTrunc(info.elementSize, Type::getInt32Ty(F.getContext()));
            Value *nodeIdVal = ConstantInt::get(Type::getInt32Ty(F.getContext()), info.nodeId);
            
            Builder.CreateCall(registerNodeFunc, {basePtrCast, numElemsCast, elemSizeCast, nodeIdVal});
            
            errs() << "Inserted registerNode call for node " << info.nodeId << "\n";
            modified = true;
        }
        
        // 注册所有边
        for (const IndirectionInfo &info : indirections) {
            // 在间接访问之前插入registerTravEdge调用
            Builder.SetInsertPoint(info.destAccess);
            
            Value *srcCast = Builder.CreateBitCast(info.srcBase, Type::getInt8PtrTy(F.getContext()));
            Value *destCast = Builder.CreateBitCast(info.destBase, Type::getInt8PtrTy(F.getContext()));
            Value *edgeTypeVal = ConstantInt::get(Type::getInt32Ty(F.getContext()), 
                                                   static_cast<uint32_t>(info.type));
            
            Builder.CreateCall(registerTravEdgeFunc, {srcCast, destCast, edgeTypeVal});
            
            errs() << "Inserted registerTravEdge call\n";
            modified = true;
        }
    }
};

char ProdigyPass::ID = 0;

} // anonymous namespace

// 注册Pass
static RegisterPass<ProdigyPass> X("prodigy", "Prodigy Hardware Prefetching Pass", false, false);

// LLVM 16暂时不支持新Pass管理器接口，注释掉
#if 0
// 新Pass管理器接口
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