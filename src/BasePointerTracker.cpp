#include "BasePointerTracker.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include <queue>

using namespace llvm;

namespace prodigy {

Value* BasePointerTracker::getBasePointer(Value *ptr) {
    errs() << "    getBasePointer: starting with " << *ptr << "\n";
    
    // Debug: show current registrations
    const auto& registrations = getRegisteredPointers();
    if (registrations.size() < 20) { // Only print if small number
        errs() << "      Current registrations: " << registrations.size() << "\n";
        for (const auto& pair : registrations) {
            errs() << "        " << pair.first << " -> Node " << pair.second << "\n";
        }
    }
    
    // First check if this value is already registered
    if (ptrToNodeId.find(ptr) != ptrToNodeId.end()) {
        errs() << "    -> Already registered!\n";
        return ptr;
    }
    
    // Handle GlobalVariable - check if there's a store to this global
    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(ptr)) {
        errs() << "    -> Is a GlobalVariable\n";
        
        // Look for stores to this global variable
        for (User *U : GV->users()) {
            if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
                if (SI->getPointerOperand() == GV) {
                    Value *StoredValue = SI->getValueOperand();
                    errs() << "       Found store: " << *SI << "\n";
                    errs() << "       Stored value: " << *StoredValue << "\n";
                    
                    // If the stored value is a registered allocation, return it
                    if (ptrToNodeId.find(StoredValue) != ptrToNodeId.end()) {
                        errs() << "       -> Stored value is registered!  Aliasing GV to same node.\n";
                        // record alias
                        uint32_t nid = ptrToNodeId[StoredValue];
                        registerPointer(ptr, nid);
                        return StoredValue;
                    }
                    
                    // Otherwise, recursively get base pointer of stored value
                    return getBasePointer(StoredValue);
                }
            }
        }
        
        // No store found, return the global variable itself
        errs() << "    -> No store found, returning global variable\n";
        return ptr;
    }
    
    // Handle GEP instructions - including struct member access
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
        errs() << "    -> Following GEP pointer operand\n";
        
        // Check if this is a struct member access (typically has constant indices)
        bool isStructAccess = false;
        if (GEP->getNumIndices() >= 2) {
            // First index is usually 0 for struct access
            if (ConstantInt *FirstIdx = dyn_cast<ConstantInt>(GEP->getOperand(1))) {
                if (FirstIdx->isZero()) {
                    isStructAccess = true;
                    errs() << "      Detected struct/class member access\n";
                }
            }
        }
        
        // For struct member access, we need to track the member field
        if (isStructAccess) {
            Value *StructPtr = GEP->getPointerOperand();
            
            // If the struct pointer itself is a load, trace it
            if (LoadInst *StructLoad = dyn_cast<LoadInst>(StructPtr)) {
                errs() << "      Struct pointer is loaded from: " << *StructLoad->getPointerOperand() << "\n";
                
                // Look for stores to the location this struct pointer was loaded from
                Value *LoadedFrom = StructLoad->getPointerOperand();
                Module *M = GEP->getModule();
                
                // Search for stores of registered allocations to struct members
                for (Function &F : *M) {
                    for (BasicBlock &BB : F) {
                        for (Instruction &I : BB) {
                            if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
                                // Check if storing to a GEP that matches our pattern
                                if (GetElementPtrInst *StoreGEP = dyn_cast<GetElementPtrInst>(SI->getPointerOperand())) {
                                    if (areGEPsSimilar(GEP, StoreGEP)) {
                                        Value *StoredValue = SI->getValueOperand();
                                        
                                        // Check if the stored value is registered
                                        if (isRegistered(StoredValue)) {
                                            errs() << "      Found registered allocation stored to similar struct member\n";
                                            return StoredValue;
                                        }
                                        
                                        // Recursively check stored value
                                        Value *Base = getBasePointer(StoredValue);
                                        if (Base && isRegistered(Base)) {
                                            return Base;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Continue with regular GEP handling
        return getBasePointer(GEP->getPointerOperand());
    }
    
    // Handle LoadInst - trace back through stores
    if (LoadInst *LI = dyn_cast<LoadInst>(ptr)) {
        errs() << "    -> Is a LoadInst, checking stores to its operand\n";
        
        // Try to find what was stored to this location
        Value *LoadedFrom = LI->getPointerOperand();
        errs() << "       LoadedFrom: " << *LoadedFrom << "\n";
        
        // If loading from a global variable, check stores to that global
        if (GlobalVariable *GV = dyn_cast<GlobalVariable>(LoadedFrom)) {
            for (User *U : GV->users()) {
                if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
                    if (SI->getPointerOperand() == GV) {
                        Value *StoredValue = SI->getValueOperand();
                        errs() << "       Found store: " << *SI << "\n";
                        errs() << "       Stored value: " << *StoredValue << "\n";
                        
                        // Check if stored value is registered
                        if (ptrToNodeId.find(StoredValue) != ptrToNodeId.end()) {
                            errs() << "       -> Stored value is registered!\n";
                            return StoredValue;
                        }
                        
                        // Recursively get base pointer
                        return getBasePointer(StoredValue);
                    }
                }
            }
        }
        
        // If loading from a GEP, check if it's accessing a struct field
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(LoadedFrom)) {
            // This might be loading from a struct field (e.g., g->offsets)
            Value *StructPtr = GEP->getPointerOperand();
            
            errs() << "       Loading from GEP (struct field access?)\n";
            errs() << "       GEP: " << *GEP << "\n";
            errs() << "       StructPtr: " << *StructPtr << "\n";
            
            // If loading from a struct field, find all stores to this field
            // across the entire module
            Module *M = LI->getModule();
            for (Function &F : *M) {
                for (BasicBlock &BB : F) {
                    for (Instruction &I : BB) {
                        if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
                            // Check if storing to a similar GEP
                            if (GetElementPtrInst *StoreGEP = dyn_cast<GetElementPtrInst>(SI->getPointerOperand())) {
                                if (areGEPsSimilar(GEP, StoreGEP)) {
                                    Value *StoredValue = SI->getValueOperand();
                                    errs() << "       Found similar store: " << *SI << "\n";
                                    errs() << "       Stored value: " << *StoredValue << "\n";
                                    
                                    if (ptrToNodeId.find(StoredValue) != ptrToNodeId.end()) {
                                        errs() << "       -> Stored value is registered!\n";
                                        return StoredValue;
                                    }
                                    
                                    // Recursively check
                                    Value *Base = getBasePointer(StoredValue);
                                    if (Base && isRegistered(Base)) {
                                        errs() << "       -> Found base allocation!\n";
                                        return Base;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Original logic for simple loads
        for (User *U : LoadedFrom->users()) {
            if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
                if (SI->getPointerOperand() == LoadedFrom) {
                    Value *StoredVal = SI->getValueOperand();
                    errs() << "       Found store: " << *SI << "\n";
                    errs() << "       Stored value: " << *StoredVal << "\n";
                    
                    // Check if the stored value is a known allocation
                    if (isRegistered(StoredVal)) {
                        errs() << "       -> Stored value is a known allocation!  Aliasing alloca to same node.\n";
                        registerPointer(LoadedFrom, getNodeId(StoredVal));
                        return StoredVal;
                    }
                    // Recursively check the stored value
                    Value *Base = getBasePointer(StoredVal);
                    if (Base && isRegistered(Base)) {
                        errs() << "       -> Recursively found allocation!\n";
                        return Base;
                    }
                }
            }
        }
        
        // If LoadedFrom is an alloca, look for stores in the same function
        if (AllocaInst *AI = dyn_cast<AllocaInst>(LoadedFrom)) {
            errs() << "       LoadedFrom is an alloca, searching all stores in function\n";
            Function *F = AI->getFunction();
            for (BasicBlock &BB : *F) {
                for (Instruction &I : BB) {
                    if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
                        if (SI->getPointerOperand() == AI) {
                            Value *StoredVal = SI->getValueOperand();
                            errs() << "       Found store to alloca: " << *SI << "\n";
                            if (isRegistered(StoredVal)) {
                                errs() << "       -> Stored value is a known allocation!  Aliasing alloca to same node.\n";
                                registerPointer(AI, getNodeId(StoredVal));
                                return StoredVal;
                            }
                        }
                    }
                }
            }
        }
    }
    
    errs() << "    -> Returning original pointer\n";
    return ptr;
}

Value* BasePointerTracker::findStructFieldAllocation(GetElementPtrInst *FieldGEP) {
    // This is a simplified heuristic for finding allocations stored to struct fields
    // In real implementation, we'd need more sophisticated tracking
    
    // Get the struct type and field index
    if (FieldGEP->getNumIndices() >= 2) {
        // Usually struct field access has at least 2 indices
        if (ConstantInt *FieldIdx = dyn_cast<ConstantInt>(FieldGEP->getOperand(2))) {
            uint64_t fieldIndex = FieldIdx->getZExtValue();
            
            // Look through allocations to find ones that might match
            // This is a heuristic - in real implementation we'd track stores more precisely
            // For now, we'll just return nullptr as we don't have access to globalAllocations here
            errs() << "       Trying to find allocation for struct field " << fieldIndex << "\n";
        }
    }
    
    return nullptr;
}

bool BasePointerTracker::areGEPsSimilar(GetElementPtrInst *GEP1, GetElementPtrInst *GEP2) {
    // Check if both GEPs access the same struct field
    
    // Must have same number of indices
    if (GEP1->getNumIndices() != GEP2->getNumIndices()) {
        return false;
    }
    
    // For struct field access, typically have 2 indices: [0][field_index]
    if (GEP1->getNumIndices() >= 2) {
        // Check if all constant indices match
        for (unsigned i = 1; i < GEP1->getNumOperands(); ++i) {
            ConstantInt *CI1 = dyn_cast<ConstantInt>(GEP1->getOperand(i));
            ConstantInt *CI2 = dyn_cast<ConstantInt>(GEP2->getOperand(i));
            
            if (CI1 && CI2) {
                if (CI1->getZExtValue() != CI2->getZExtValue()) {
                    return false;
                }
            } else if (CI1 || CI2) {
                // One is constant, other is not
                return false;
            }
            // If both are non-constant, we can't easily compare
        }
        
        // All constant indices match
        return true;
    }
    
    return false;
}

} // namespace prodigy 