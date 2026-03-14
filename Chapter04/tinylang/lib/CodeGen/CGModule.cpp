#include "tinylang/CodeGen/CGModule.h"
#include "tinylang/CodeGen/CGProcedure.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"

using namespace tinylang;

void CGModule::initialize() {
  VoidTy = llvm::Type::getVoidTy(getLLVMCtx());
  Int1Ty = llvm::Type::getInt1Ty(getLLVMCtx());
  Int32Ty = llvm::Type::getInt32Ty(getLLVMCtx());
  Int64Ty = llvm::Type::getInt64Ty(getLLVMCtx());
  Int32Zero =
      llvm::ConstantInt::get(Int32Ty, 0, /*isSigned*/ true);
}

llvm::Type *CGModule::convertType(TypeDeclaration *Ty) {
  if (Ty->getName() == "INTEGER")
    return Int64Ty;
  if (Ty->getName() == "BOOLEAN")
    return Int1Ty;
  llvm::report_fatal_error("Unsupported type");
}

// 名称改编：解决跨模块的名称冲突
std::string CGModule::mangleName(Decl *D) {
  std::string Mangled("_t");
  llvm::SmallVector<llvm::StringRef, 4> Parts;
  for (; D; D = D->getEnclosingDecl())
    Parts.push_back(D->getName());
  while (!Parts.empty()) {
    llvm::StringRef Name = Parts.pop_back_val();
    Mangled.append(
        llvm::Twine(Name.size()).concat(Name).str());
  }
  return Mangled;
}

llvm::GlobalObject *CGModule::getGlobal(Decl *D) {
  return Globals[D];
}

void CGModule::run(ModuleDeclaration *Mod) {
  for (auto *Decl : Mod->getDecls()) {
    if (auto *Var =
            llvm::dyn_cast<VariableDeclaration>(Decl)) {
      // Create global variables: 遇到一个变量声明时，创建一个 llvm::GlobalVariable 对象，对应一个 IR 模块中的全局变量
      llvm::GlobalVariable *V = new llvm::GlobalVariable(
          *M, convertType(Var->getType()),
          /*isConstant=*/false,
          llvm::GlobalValue::PrivateLinkage, nullptr,
          mangleName(Var));
      Globals[Var] = V; // 记录到 Globals 映射中以便后续代码根据变量声明查找对应的 LLVM 全局值
    } else if (auto *Proc =
                   llvm::dyn_cast<ProcedureDeclaration>(
                       Decl)) {
      CGProcedure CGP(*this); // 遇到函数声明时，创建 CGProcedure 实例来构造函数并加入模块中
      CGP.run(Proc);
    }
  }
}
