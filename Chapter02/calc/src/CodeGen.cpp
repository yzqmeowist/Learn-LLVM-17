#include "CodeGen.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class ToIRVisitor : public ASTVisitor {
  Module *M; // 指向一个 LLVM 模块（一个独立的编译单元，包含函数、全局变量等，整个程序对应一个模块）
  IRBuilder<> Builder; // IR 构建器，用于生成具体指令
  Type *VoidTy;
  Type *Int32Ty;
  PointerType *PtrTy;
  Constant *Int32Zero;

  Value *V; // 当前计算得到的值
  StringMap<Value *> nameMap; // 变量名到其值的映射表

public:
  ToIRVisitor(Module *M) : M(M), Builder(M->getContext()) {
    VoidTy = Type::getVoidTy(M->getContext());
    Int32Ty = Type::getInt32Ty(M->getContext());
    PtrTy = PointerType::getUnqual(M->getContext());
    Int32Zero = ConstantInt::get(Int32Ty, 0, true);
  }

  void run(AST *Tree) { // 创建 LLVM IR 版本的 main 函数
    FunctionType *MainFty = FunctionType::get(
        Int32Ty, {Int32Ty, PtrTy}, false); // 描述 main 函数的类型（签名）
    Function *MainFn = Function::Create(
        MainFty, GlobalValue::ExternalLinkage, "main", M); // 创建 main 函数
    BasicBlock *BB = BasicBlock::Create(M->getContext(),
                                        "entry", MainFn); // 创建入口基本块
    Builder.SetInsertPoint(BB); // Builder 生成的所有 IR 指令都应该插入 BB 基本块中

    Tree->accept(*this); // 开始访问 AST 根节点

    FunctionType *CalcWriteFnTy =
        FunctionType::get(VoidTy, {Int32Ty}, false);
    Function *CalcWriteFn = Function::Create(
        CalcWriteFnTy, GlobalValue::ExternalLinkage,
        "calc_write", M); // 声明 calc_write 函数
    Builder.CreateCall(CalcWriteFnTy, CalcWriteFn, {V}); // 生成调用指令，将 V 作为参数传给 calc_write

    Builder.CreateRet(Int32Zero); // 生成返回指令
  }

  virtual void visit(Factor &Node) override {
    if (Node.getKind() == Factor::Ident) {
      V = nameMap[Node.getVal()];
    } else {
      int intval;
      Node.getVal().getAsInteger(10, intval);
      V = ConstantInt::get(Int32Ty, intval, true); // 创建 LLVM 的整形常量，再赋值给 V
    }
  };

  virtual void visit(BinaryOp &Node) override {
    Node.getLeft()->accept(*this);
    Value *Left = V;
    Node.getRight()->accept(*this);
    Value *Right = V;
    switch (Node.getOperator()) {
    case BinaryOp::Plus:
      V = Builder.CreateNSWAdd(Left, Right);
      break;
    case BinaryOp::Minus:
      V = Builder.CreateNSWSub(Left, Right);
      break;
    case BinaryOp::Mul:
      V = Builder.CreateNSWMul(Left, Right);
      break;
    case BinaryOp::Div:
      V = Builder.CreateSDiv(Left, Right);
      break;
    }
  };

  virtual void visit(WithDecl &Node) override {
    FunctionType *ReadFty =
        FunctionType::get(Int32Ty, {PtrTy}, false);
    Function *ReadFn = Function::Create(
        ReadFty, GlobalValue::ExternalLinkage, "calc_read",
        M); // 创建外部函数 calc_read 的原型
    for (auto I = Node.begin(), E = Node.end(); I != E;
         ++I) {
      StringRef Var = *I; // 读取变量名

      // Create call to calc_read function.
      Constant *StrText = ConstantDataArray::getString(
          M->getContext(), Var); // 对每个变量名，创建一个包含该变量名的常亮数据数组
      GlobalVariable *Str = new GlobalVariable(
          *M, StrText->getType(),
          /*isConstant=*/true, GlobalValue::PrivateLinkage,
          StrText, Twine(Var).concat(".str")); // 将该数组包装成一个全局变量（通常被标记为私有链接和常量）
      CallInst *Call =
          Builder.CreateCall(ReadFty, ReadFn, {Str}); // 创建 calc_read 调用，将字符串全局变量 Str 传入，提示用户输入该变量的值，返回值存储在 Call 指向的对象中

      nameMap[Var] = Call; // 将返回值存储到 nameMap 中，当使用该变量名时即可在 nameMap 中找到它的值
    }

    Node.getExpr()->accept(*this);
  };
};
} // namespace

void CodeGen::compile(AST *Tree) {
  LLVMContext Ctx; // 创建 LLVM 上下文
  Module *M = new Module("calc.expr", Ctx); // 创建 calc.expr 模块，用于容纳函数和全局变量
  ToIRVisitor ToIR(M);
  ToIR.run(Tree); // 遍历 AST
  M->print(outs(), nullptr); // 将模块生成的 LLVM IR 代码打印
}
