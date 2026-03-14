#ifndef TINYLANG_CODEGEN_CGPROCEDURE_H
#define TINYLANG_CODEGEN_CGPROCEDURE_H

#include "tinylang/AST/AST.h"
#include "tinylang/CodeGen/CGModule.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Value.h"

namespace llvm {
class Function;
}

namespace tinylang {

class CGProcedure {
  CGModule &CGM;
  llvm::IRBuilder<> Builder;

  llvm::BasicBlock *Curr;

  ProcedureDeclaration *Proc;
  llvm::FunctionType *Fty;
  llvm::Function *Fn;

  /* AST numbering
     BasicBlockDef: 保存单个基本块的特定信息
     Decl *: 指向 AST 中的一个声明，如一个局部变量或函数形参
     llvm::TrackingVH<llvm::Value>: 该声明在当前基本块内的最新值
      llvm::Value 是 SSA 形式中的值的抽象表示（标签），可能会被更改，因此使用 TrackingVH 值句柄跟踪 Value 并在其被替换时自动更新其内部指针
     IncompletePhis: 记录需要后续更新的空 phi 指令
  */
  struct BasicBlockDef {
    // Maps the variable (or formal parameter) to its
    // definition.
    llvm::DenseMap<Decl *, llvm::TrackingVH<llvm::Value>>
        Defs;
    // Set of incompleted phi instructions.
    llvm::DenseMap<llvm::PHINode *, Decl *> IncompletePhis;
    // Block is sealed, that is, no more predecessors will
    // be added.
    unsigned Sealed : 1;

    BasicBlockDef() : Sealed(0) {}
  };

  // 全局映射：将每个基本块关联到对应的 BasicBlockDef 信息
  llvm::DenseMap<llvm::BasicBlock *, BasicBlockDef>
      CurrentDef;

  // 写操作：当一个局部变量在一个基本块内被定义（赋值）时，记录它的新值
  void writeLocalVariable(llvm::BasicBlock *BB, Decl *Decl,
                          llvm::Value *Val);

  // 读操作：在一个基本块中需要读取一个局部变量的值时调用
  llvm::Value *readLocalVariable(llvm::BasicBlock *BB,
                                 Decl *Decl);
  llvm::Value *
  readLocalVariableRecursive(llvm::BasicBlock *BB,
                             Decl *Decl);
  llvm::PHINode *addEmptyPhi(llvm::BasicBlock *BB,
                             Decl *Decl);
  llvm::Value *addPhiOperands(llvm::BasicBlock *BB,
                              Decl *Decl,
                              llvm::PHINode *Phi);
  llvm::Value *optimizePhi(llvm::PHINode *Phi);
  void sealBlock(llvm::BasicBlock *BB);

  llvm::DenseMap<FormalParameterDeclaration *,
                 llvm::Argument *>
      FormalParams;

  void writeVariable(llvm::BasicBlock *BB, Decl *Decl,
                     llvm::Value *Val);
  llvm::Value *readVariable(llvm::BasicBlock *BB,
                            Decl *Decl);

  llvm::Type *mapType(Decl *Decl,
                      bool HonorReference = true);
  llvm::FunctionType *
  createFunctionType(ProcedureDeclaration *Proc);
  llvm::Function *createFunction(ProcedureDeclaration *Proc,
                                 llvm::FunctionType *FTy);

protected:
  void setCurr(llvm::BasicBlock *BB) {
    Curr = BB;
    Builder.SetInsertPoint(Curr);
  }

  llvm::Value *emitInfixExpr(InfixExpression *E);
  llvm::Value *emitPrefixExpr(PrefixExpression *E);
  llvm::Value *emitExpr(Expr *E);

  void emitStmt(AssignmentStatement *Stmt);
  void emitStmt(ProcedureCallStatement *Stmt);
  void emitStmt(IfStatement *Stmt);
  void emitStmt(WhileStatement *Stmt);
  void emitStmt(ReturnStatement *Stmt);
  void emit(const StmtList &Stmts);

public:
  CGProcedure(CGModule &CGM)
      : CGM(CGM), Builder(CGM.getLLVMCtx()),
        Curr(nullptr){};

  void run(ProcedureDeclaration *Proc);
};
} // namespace tinylang
#endif