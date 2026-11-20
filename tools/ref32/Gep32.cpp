// SonicAdventureRe-nx: the first half of lowering the decomp's 32-bit game pointers to integers.
//
// The decomp keeps the game's pointers 32 bits wide with clang's __ptr32, which clang supports
// only for x86. Its refs.py lowers every load, store and cast of one (a "reference", a
// `ptr addrspace(271)`) to host pointers once the game's 4 GB domain lies above address 0, but it
// leaves address arithmetic (getelementptr), comparisons, phis, selects and returns on references
// for the x86 backend, which knows their address space is 32 bits wide. No AArch64 backend does.
//
// This pass rewrites each getelementptr on a reference as integer arithmetic on its 32 bits, with
// the offset the x86 data layout gives (the game's layout: a reference is 4 bytes):
//
//   %q = getelementptr T, ptr addrspace(271) %p, idx...
// becomes
//   %p.i = ptrtoint ptr addrspace(271) %p to i32
//   %q.i = add i32 %p.i, <offset of idx... in T>
//   %q   = inttoptr i32 %q.i to ptr addrspace(271)
//
// so that what is left of the address space is a 32-bit token that ref32_int.py can retype as i32.
// It runs on the x86-64 IR, before the triple changes, so the data layout it reads is the game's.
//
//   opt -load-pass-plugin=Gep32.so -passes=gep32 IN.ll -S -o OUT.ll
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

constexpr unsigned kRef = 271;

bool is_ref(Type* t) {
    auto* p = dyn_cast<PointerType>(t);
    return p && p->getAddressSpace() == kRef;
}

// The byte offset of a GEP's indices as an i32 value, built before `at`.
Value* offset_of(GEPOperator* gep, IRBuilder<>& b, const DataLayout& dl) {
    Type* i32 = b.getInt32Ty();
    Value* total = ConstantInt::get(i32, 0);
    for (gep_type_iterator gti = gep_type_begin(gep), e = gep_type_end(gep); gti != e; ++gti) {
        Value* idx = gti.getOperand();
        if (StructType* st = gti.getStructTypeOrNull()) {
            const uint64_t field = cast<ConstantInt>(idx)->getZExtValue();
            const uint64_t off = dl.getStructLayout(st)->getElementOffset(field);
            if (off)
                total = b.CreateAdd(total, ConstantInt::get(i32, off));
            continue;
        }
        const uint64_t size = gti.getSequentialElementStride(dl);
        Value* i = b.CreateSExtOrTrunc(idx, i32);
        if (size != 1)
            i = b.CreateMul(i, ConstantInt::get(i32, size));
        total = b.CreateAdd(total, i);
    }
    return total;
}

struct Gep32 : PassInfoMixin<Gep32> {
    PreservedAnalyses run(Module& m, ModuleAnalysisManager&) {
        const DataLayout& dl = m.getDataLayout();
        if (dl.getPointerSizeInBits(kRef) != 32) {
            errs() << "gep32: the module's data layout doesn't make addrspace(271) 32 bits wide\n";
            report_fatal_error("gep32: not the x86 layout");
        }
        bool changed = false;
        for (Function& f : m) {
            SmallVector<GetElementPtrInst*, 64> geps;
            for (Instruction& i : instructions(f))
                if (auto* g = dyn_cast<GetElementPtrInst>(&i))
                    if (is_ref(g->getPointerOperandType()))
                        geps.push_back(g);
            for (GetElementPtrInst* g : geps) {
                if (g->getType()->isVectorTy())
                    report_fatal_error("gep32: a vector of references");
                IRBuilder<> b(g);
                Value* base = b.CreatePtrToInt(g->getPointerOperand(), b.getInt32Ty(),
                                               g->getName() + ".base");
                Value* sum = b.CreateAdd(base, offset_of(cast<GEPOperator>(g), b, dl),
                                         g->getName() + ".at");
                Value* ref = b.CreateIntToPtr(sum, g->getType());
                ref->takeName(g);
                g->replaceAllUsesWith(ref);
                g->eraseFromParent();
                changed = true;
            }
        }
        // A constant GEP on a reference (a static's field address) has no instruction to replace:
        // the integer pass would see a getelementptr with an i32 base. Refuse it, naming it.
        // Constants are shared (a DAG), so each is visited once: walking them as trees blew up on
        // sonic.c's tables.
        SmallPtrSet<const Constant*, 32> seen;
        for (GlobalVariable& gv : m.globals()) {
            if (!gv.hasInitializer())
                continue;
            SmallVector<const Constant*, 16> work{gv.getInitializer()};
            while (!work.empty()) {
                const Constant* c = work.pop_back_val();
                if (!seen.insert(c).second || isa<GlobalValue>(c))
                    continue;
                if (auto* ce = dyn_cast<ConstantExpr>(c))
                    if (ce->getOpcode() == Instruction::GetElementPtr &&
                        is_ref(ce->getOperand(0)->getType())) {
                        errs() << "gep32: a constant getelementptr on a reference in @"
                               << gv.getName() << "\n";
                        report_fatal_error("gep32: constant reference arithmetic");
                    }
                for (const Use& u : c->operands())
                    if (auto* oc = dyn_cast<Constant>(u.get()))
                        work.push_back(oc);
            }
        }
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

}  // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "gep32", "0.1", [](PassBuilder& pb) {
                pb.registerPipelineParsingCallback(
                    [](StringRef name, ModulePassManager& mpm, ArrayRef<PassBuilder::PipelineElement>) {
                        if (name != "gep32")
                            return false;
                        mpm.addPass(Gep32());
                        return true;
                    });
            }};
}
