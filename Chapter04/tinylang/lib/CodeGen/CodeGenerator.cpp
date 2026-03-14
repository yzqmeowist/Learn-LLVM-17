#include "tinylang/CodeGen/CodeGenerator.h"
#include "tinylang/CodeGen/CGProcedure.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace tinylang;

CodeGenerator *
CodeGenerator::create(llvm::LLVMContext &Ctx,
                      llvm::TargetMachine *TM) {
  return new CodeGenerator(Ctx, TM);
}

std::unique_ptr<llvm::Module>
CodeGenerator::run(ModuleDeclaration *Mod,
                   std::string FileName) {
  std::unique_ptr<llvm::Module> M =
      std::make_unique<llvm::Module>(FileName, Ctx); // 创建 LLVM 模块，即存放所有 IR 代码的容器
  M->setTargetTriple(TM->getTargetTriple().getTriple()); // arch
  M->setDataLayout(TM->createDataLayout()); // 设置 datalayout
  CGModule CGM(M.get()); // 遍历全局声明、生成函数和全集变量 IR
  CGM.run(Mod);
  return M;
}
