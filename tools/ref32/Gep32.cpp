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
#include <cstdlib>

#include "llvm/ADT/DenseMap.h"
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
        if (const char* domain = getenv("GEP32_DOMAIN")) {
            const char* link = getenv("GEP32_LINK_WINDOW");
            DomainSwap swap(m, strtoull(domain, nullptr, 0), link ? strtoull(link, nullptr, 0) : 0);
            changed |= swap.run();
        }
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }

    // The Switch build (GEP32_DOMAIN set): the domain's number given to refs.py is a marker, a
    // 4 GB-aligned number no address or ordinary constant comes near (0x5a5a00000000). A 64-bit
    // constant within 4 GB of it becomes native_domain_base, a symbol the link places at its
    // window's start, plus the offset, so code reaches the domain relative to itself and the
    // loader can move the image to whichever window Horizon leaves free. Inside a 32-bit
    // reference in static data it becomes the link window's number (GEP32_LINK_WINDOW) instead.
    struct DomainSwap {
        Module& m;
        uint64_t base;
        Constant* symbol = nullptr;
        DenseMap<Constant*, Constant*> memo;
        unsigned swapped = 0;

        uint64_t link;
        DomainSwap(Module& mod, uint64_t b, uint64_t l) : m(mod), base(b), link(l) {}

        bool in_window(uint64_t v) const { return v >= base && v - base < (1ULL << 32); }

        Constant* domain() {
            if (!symbol) {
                LLVMContext& cx = m.getContext();
                GlobalVariable* g = m.getNamedGlobal("native_domain_base");
                if (!g)
                    g = new GlobalVariable(m, Type::getInt8Ty(cx), false, GlobalValue::ExternalLinkage,
                                           nullptr, "native_domain_base");
                symbol = ConstantExpr::getPtrToInt(g, Type::getInt64Ty(cx));
            }
            return symbol;
        }

        Constant* at(uint64_t v, Type* t) {
            swapped++;
            return v == base ? domain() : ConstantExpr::getAdd(domain(), ConstantInt::get(t, v - base));
        }

        // c with every marker-window constant as the link window's number instead
        DenseMap<Constant*, Constant*> memo_link;
        Constant* in_link_window(Constant* c) {
            auto it = memo_link.find(c);
            if (it != memo_link.end())
                return it->second;
            Constant* out = c;
            if (auto* ci = dyn_cast<ConstantInt>(c)) {
                if (ci->getBitWidth() == 64 && in_window(ci->getZExtValue())) {
                    out = ConstantInt::get(ci->getType(), ci->getZExtValue() - base + link);
                    swapped++;
                }
            } else if (auto* ce = dyn_cast<ConstantExpr>(c)) {
                SmallVector<Constant*, 8> ops;
                bool any = false;
                for (Use& u : ce->operands()) {
                    auto* o = cast<Constant>(u.get());
                    Constant* r = in_link_window(o);
                    any |= r != o;
                    ops.push_back(r);
                }
                if (any)
                    out = ce->getWithOperands(ops);
            }
            memo_link[c] = out;
            return out;
        }

        Constant* rewrite(Constant* c) {
            auto it = memo.find(c);
            if (it != memo.end())
                return it->second;
            Constant* out = c;
            if (auto* ci = dyn_cast<ConstantInt>(c)) {
                if (ci->getBitWidth() == 64 && in_window(ci->getZExtValue()))
                    out = at(ci->getZExtValue(), ci->getType());
            } else if (auto* cd = dyn_cast<ConstantDataSequential>(c)) {
                if (cd->getElementType()->isIntegerTy(64)) {
                    bool any = false;
                    for (unsigned i = 0; i < cd->getNumElements(); i++)
                        any |= in_window(cd->getElementAsInteger(i));
                    if (any) {
                        SmallVector<Constant*, 16> elems;
                        for (unsigned i = 0; i < cd->getNumElements(); i++)
                            elems.push_back(rewrite(cd->getElementAsConstant(i)));
                        if (isa<ConstantDataArray>(cd))
                            out = ConstantArray::get(cast<ArrayType>(cd->getType()), elems);
                        else
                            report_fatal_error("gep32: a vector of domain addresses");
                    }
                }
            } else if (auto* ce = dyn_cast<ConstantExpr>(c);
                       ce && ce->getOpcode() == Instruction::Trunc && ce->getType()->isIntegerTy() &&
                       ce->getType()->getIntegerBitWidth() <= 32) {
                // a reference in static data, trunc(address - base): its low 32 bits are the same
                // in every 4 GB window, so the marker becomes the link window's number, which the
                // link resolves in 32 bits
                Constant* op = in_link_window(ce->getOperand(0));
                if (op != ce->getOperand(0))
                    out = ConstantExpr::getTrunc(op, ce->getType());
            } else if (!isa<GlobalValue>(c) && (isa<ConstantExpr>(c) || isa<ConstantAggregate>(c))) {
                SmallVector<Constant*, 8> ops;
                bool any = false;
                for (Use& u : c->operands()) {
                    auto* o = cast<Constant>(u.get());
                    Constant* r = rewrite(o);
                    any |= r != o;
                    ops.push_back(r);
                }
                if (any) {
                    if (auto* ce = dyn_cast<ConstantExpr>(c))
                        out = ce->getWithOperands(ops);
                    else if (auto* ca = dyn_cast<ConstantArray>(c))
                        out = ConstantArray::get(ca->getType(), ops);
                    else if (auto* cs = dyn_cast<ConstantStruct>(c))
                        out = ConstantStruct::get(cs->getType(), ops);
                    else
                        out = ConstantVector::get(ops);
                }
            }
            memo[c] = out;
            return out;
        }

        bool run() {
            for (Function& f : m)
                for (Instruction& i : instructions(f)) {
                    if (auto* sw = dyn_cast<SwitchInst>(&i)) {
                        for (auto& cs : sw->cases())
                            if (cs.getCaseValue()->getBitWidth() == 64 &&
                                in_window(cs.getCaseValue()->getZExtValue()))
                                report_fatal_error("gep32: a switch on a domain address");
                        continue;
                    }
                    for (Use& u : i.operands()) {
                        auto* c = dyn_cast<Constant>(u.get());
                        if (!c || isa<GlobalValue>(c))
                            continue;
                        Constant* r = rewrite(c);
                        if (r != c)
                            u.set(r);
                    }
                }
            for (GlobalVariable& gv : m.globals())
                if (gv.hasInitializer()) {
                    Constant* c = gv.getInitializer();
                    Constant* r = rewrite(c);
                    if (r != c)
                        gv.setInitializer(r);
                }
            return swapped != 0;
        }
    };
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
