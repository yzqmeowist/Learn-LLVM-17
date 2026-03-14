#include "tinylang/CodeGen/CGProcedure.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/Casting.h"

using namespace tinylang;

void CGProcedure::writeLocalVariable(llvm::BasicBlock *BB,
                                     Decl *Decl,
                                     llvm::Value *Val) {
  assert(BB && "Basic block is nullptr");
  assert(
      (llvm::isa<VariableDeclaration>(Decl) ||
       llvm::isa<FormalParameterDeclaration>(Decl)) &&
      "Declaration must be variable or formal parameter");
  assert(Val && "Value is nullptr");
  CurrentDef[BB].Defs[Decl] = Val; // 更新 CurrentDef 中的指定基本块 BB 中的指定声明 Decl 的当前值为 Val
}

llvm::Value *
CGProcedure::readLocalVariable(llvm::BasicBlock *BB,
                               Decl *Decl) {
  assert(BB && "Basic block is nullptr");
  assert(
      (llvm::isa<VariableDeclaration>(Decl) ||
       llvm::isa<FormalParameterDeclaration>(Decl)) &&
      "Declaration must be variable or formal parameter");
  auto Val = CurrentDef[BB].Defs.find(Decl);
  if (Val != CurrentDef[BB].Defs.end())
    return Val->second; // 先在当前基本块的 Defs 中查找该变量，找到时说明该变量在当前块内被定义过，直接返回该值
  return readLocalVariableRecursive(BB, Decl); // 没找到该变量的定义时，变量的值可能来自于前驱基本块，递归搜索到前驱块中寻找
}

llvm::Value *CGProcedure::readLocalVariableRecursive(
    llvm::BasicBlock *BB, Decl *Decl) {
  llvm::Value *Val = nullptr;
  if (!CurrentDef[BB].Sealed) { // 当前块尚未密封，且变量在当前块内未定义时，必须插入空 phi 作为临时值使用
    // Add incomplete phi for variable.
    llvm::PHINode *Phi = addEmptyPhi(BB, Decl);
    CurrentDef[BB].IncompletePhis[Phi] = Decl; // 记住这个空 phi 指令，等该基本块之后被密封时再用收集到的真实值更新该 phi 指令
    Val = Phi; // 将该空 phi 指令作为变量的当前值返回并使用
  } else if (auto *PredBB = BB->getSinglePredecessor()) {
    // Only one predecessor.
    Val = readLocalVariable(PredBB, Decl); // 当前块已密封但只有一个前驱块：查找该前驱块内的值
  } else {
    // Create empty phi instruction to break potential
    // cycles.
    // 当前块已密封但有多个前驱块：前驱块都已就绪，可以立即进行合并
    llvm::PHINode *Phi = addEmptyPhi(BB, Decl); // 先插入空 phi 指令
    writeLocalVariable(BB, Decl, Phi); // 将该 phi 指令记录为变量在当前块的定义
    Val = addPhiOperands(BB, Decl, Phi); // 遍历所有前驱块并收集其中该变量的值，并将其作为操作数填入该 phi 指令中作为返回
  }
  writeLocalVariable(BB, Decl, Val);
  return Val;
}

// 在指定基本块开头创建一个没有操作数的 phi 指令
llvm::PHINode *
CGProcedure::addEmptyPhi(llvm::BasicBlock *BB, Decl *Decl) {
  return BB->empty()
             ? llvm::PHINode::Create(mapType(Decl), 0, "",
                                     BB)
             : llvm::PHINode::Create(mapType(Decl), 0, "",
                                     &BB->front());
}

llvm::Value *CGProcedure::addPhiOperands(
    llvm::BasicBlock *BB, Decl *Decl, llvm::PHINode *Phi) {
  for (auto *PredBB : llvm::predecessors(BB)) // 遍历所有前驱块并获取变量在块中的值
    Phi->addIncoming(readLocalVariable(PredBB, Decl),
                     PredBB); // 将 value-PredBB 的组合作为一对操作数添加到 phi 指令中
  return optimizePhi(Phi);
}

// 优化 phi 指令
llvm::Value *CGProcedure::optimizePhi(llvm::PHINode *Phi) {
  llvm::Value *Same = nullptr;
  for (llvm::Value *V : Phi->incoming_values()) { // 遍历指令的所有操作数值，找出是否所有的操作数都相同
    if (V == Same || V == Phi)
      continue;
    if (Same && V != Same) // 有一个块内操作数不一致的情况，必须保留原指令
      return Phi;
    Same = V;
  }
  if (Same == nullptr) // 没有任何操作数，用 Undef 来替换
    Same = llvm::UndefValue::get(Phi->getType());
  // Collect phi instructions using this one.
  llvm::SmallVector<llvm::PHINode *, 8> CandidatePhis;
  for (llvm::Use &U : Phi->uses()) { // 遍历原 phi 指令的所有 uses, 找到使用者是其他 phi 指令的情况并将这些指令收集到 CandidatePhis 中
    if (auto *P =
            llvm::dyn_cast<llvm::PHINode>(U.getUser()))
      if (P != Phi)
        CandidatePhis.push_back(P);
  }
  Phi->replaceAllUsesWith(Same); // 将指令的所有使用处替换为新值 Same
  Phi->eraseFromParent(); // 安全地从所在基本块删除指令
  for (auto *P : CandidatePhis)  // 遍历递归优化
    optimizePhi(P);
  return Same;
}

// 密封基本块：在其所有前驱基本块已知的情况下
void CGProcedure::sealBlock(llvm::BasicBlock *BB) {
  assert(!CurrentDef[BB].Sealed &&
         "Attempt to seal already sealed block");
  for (auto PhiDecl : CurrentDef[BB].IncompletePhis) {
    addPhiOperands(BB, PhiDecl.second, PhiDecl.first); // 遍历所有占位的空 phi 指令并填入操作数
  }
  CurrentDef[BB].IncompletePhis.clear();
  CurrentDef[BB].Sealed = true;
}

void CGProcedure::writeVariable(llvm::BasicBlock *BB,
                                Decl *D, llvm::Value *Val) {
  if (auto *V = llvm::dyn_cast<VariableDeclaration>(D)) {
    if (V->getEnclosingDecl() == Proc)
      writeLocalVariable(BB, D, Val);
    else if (V->getEnclosingDecl() ==
             CGM.getModuleDeclaration()) {
      Builder.CreateStore(Val, CGM.getGlobal(D));
    } else
      llvm::report_fatal_error(
          "Nested procedures not yet supported");
  } else if (auto *FP =
                 llvm::dyn_cast<FormalParameterDeclaration>(
                     D)) {
    if (FP->isVar()) {
      Builder.CreateStore(Val, FormalParams[FP]);
    } else
      writeLocalVariable(BB, D, Val);
  } else
    llvm::report_fatal_error("Unsupported declaration");
}

llvm::Value *CGProcedure::readVariable(llvm::BasicBlock *BB,
                                       Decl *D) {
  if (auto *V = llvm::dyn_cast<VariableDeclaration>(D)) {
    if (V->getEnclosingDecl() == Proc)
      return readLocalVariable(BB, D);
    else if (V->getEnclosingDecl() ==
             CGM.getModuleDeclaration()) {
      return Builder.CreateLoad(mapType(D),
                                CGM.getGlobal(D));
    } else
      llvm::report_fatal_error(
          "Nested procedures not yet supported");
  } else if (auto *FP =
                 llvm::dyn_cast<FormalParameterDeclaration>(
                     D)) {
    if (FP->isVar()) {
      return Builder.CreateLoad(mapType(FP, false),
                                FormalParams[FP]);
    } else
      return readLocalVariable(BB, D);
  } else
    llvm::report_fatal_error("Unsupported declaration");
}

// 获取变量对应的 LLVM IR 类型
llvm::Type *CGProcedure::mapType(Decl *Decl,
                                 bool HonorReference) {
  // 传入的声明是形式参数声明时，检查该参数是否按引用传递
  if (auto *FP = llvm::dyn_cast<FormalParameterDeclaration>(
          Decl)) {
    if (FP->isVar() && HonorReference)
      return llvm::PointerType::get(CGM.getLLVMCtx(),
                                    /*AddressSpace=*/0);
    return CGM.convertType(FP->getType());
  }
  if (auto *V = llvm::dyn_cast<VariableDeclaration>(Decl))
    return CGM.convertType(V->getType());
  return CGM.convertType(llvm::cast<TypeDeclaration>(Decl));
}

llvm::FunctionType *CGProcedure::createFunctionType(
    ProcedureDeclaration *Proc) {
  llvm::Type *ResultTy = CGM.VoidTy;
  if (Proc->getRetType()) {
    ResultTy = mapType(Proc->getRetType());
  }
  auto FormalParams = Proc->getFormalParams();
  llvm::SmallVector<llvm::Type *, 8> ParamTypes;
  for (auto FP : FormalParams) {
    llvm::Type *Ty = mapType(FP);
    ParamTypes.push_back(Ty);
  }
  return llvm::FunctionType::get(ResultTy, ParamTypes,
                                 /*IsVarArgs=*/false);
}

llvm::Function *
CGProcedure::createFunction(ProcedureDeclaration *Proc,
                            llvm::FunctionType *FTy) {
  llvm::Function *Fn = llvm::Function::Create(
      Fty, llvm::GlobalValue::ExternalLinkage,
      CGM.mangleName(Proc), CGM.getModule());
  // Give parameters a name.
  for (auto Pair : llvm::enumerate(Fn->args())) {
    llvm::Argument &Arg = Pair.value();
    FormalParameterDeclaration *FP =
        Proc->getFormalParams()[Pair.index()];
    if (FP->isVar()) {
      llvm::AttrBuilder Attr(CGM.getLLVMCtx());
      llvm::TypeSize Sz =
          CGM.getModule()->getDataLayout().getTypeStoreSize(
              CGM.convertType(FP->getType()));
      Attr.addDereferenceableAttr(Sz);
      Attr.addAttribute(llvm::Attribute::NoCapture);
      Arg.addAttrs(Attr);
    }
    Arg.setName(FP->getName());
  }
  return Fn;
}

llvm::Value *
CGProcedure::emitInfixExpr(InfixExpression *E) {
  llvm::Value *Left = emitExpr(E->getLeft());
  llvm::Value *Right = emitExpr(E->getRight());
  llvm::Value *Result = nullptr;
  switch (E->getOperatorInfo().getKind()) {
  case tok::plus:
    Result = Builder.CreateNSWAdd(Left, Right);
    break;
  case tok::minus:
    Result = Builder.CreateNSWSub(Left, Right);
    break;
  case tok::star:
    Result = Builder.CreateNSWMul(Left, Right);
    break;
  case tok::kw_DIV:
    Result = Builder.CreateSDiv(Left, Right);
    break;
  case tok::kw_MOD:
    Result = Builder.CreateSRem(Left, Right);
    break;
  case tok::equal:
    Result = Builder.CreateICmpEQ(Left, Right);
    break;
  case tok::hash:
    Result = Builder.CreateICmpNE(Left, Right);
    break;
  case tok::less:
    Result = Builder.CreateICmpSLT(Left, Right);
    break;
  case tok::lessequal:
    Result = Builder.CreateICmpSLE(Left, Right);
    break;
  case tok::greater:
    Result = Builder.CreateICmpSGT(Left, Right);
    break;
  case tok::greaterequal:
    Result = Builder.CreateICmpSGE(Left, Right);
    break;
  case tok::kw_AND:
    Result = Builder.CreateAnd(Left, Right);
    break;
  case tok::kw_OR:
    Result = Builder.CreateOr(Left, Right);
    break;
  case tok::slash:
    // Divide by real numbers not supported.
    LLVM_FALLTHROUGH;
  default:
    llvm_unreachable("Wrong operator");
  }
  return Result;
}

llvm::Value *
CGProcedure::emitPrefixExpr(PrefixExpression *E) {
  llvm::Value *Result = emitExpr(E->getExpr());
  switch (E->getOperatorInfo().getKind()) {
  case tok::plus:
    // Identity - nothing to do.
    break;
  case tok::minus:
    Result = Builder.CreateNeg(Result);
    break;
  case tok::kw_NOT:
    Result = Builder.CreateNot(Result);
    break;
  default:
    llvm_unreachable("Wrong operator");
  }
  return Result;
}

llvm::Value *CGProcedure::emitExpr(Expr *E) {
  if (auto *Infix = llvm::dyn_cast<InfixExpression>(E)) {
    return emitInfixExpr(Infix);
  } else if (auto *Prefix =
                 llvm::dyn_cast<PrefixExpression>(E)) {
    return emitPrefixExpr(Prefix);
  } else if (auto *Var =
                 llvm::dyn_cast<VariableAccess>(E)) {
    auto *Decl = Var->getDecl();
    // With more languages features in place, here you need
    // to add array and record support.
    return readVariable(Curr, Decl);
  } else if (auto *Const =
                 llvm::dyn_cast<ConstantAccess>(E)) {
    return emitExpr(Const->getDecl()->getExpr());
  } else if (auto *IntLit =
                 llvm::dyn_cast<IntegerLiteral>(E)) {
    return llvm::ConstantInt::get(CGM.Int64Ty,
                                  IntLit->getValue());
  } else if (auto *BoolLit =
                 llvm::dyn_cast<BooleanLiteral>(E)) {
    return llvm::ConstantInt::get(CGM.Int1Ty,
                                  BoolLit->getValue());
  }
  llvm::report_fatal_error("Unsupported expression");
}

void CGProcedure::emitStmt(AssignmentStatement *Stmt) {
  auto *Val = emitExpr(Stmt->getExpr());
  writeVariable(Curr, Stmt->getVar(), Val);
}

void CGProcedure::emitStmt(ProcedureCallStatement *Stmt) {
  llvm::report_fatal_error("not implemented");
}

void CGProcedure::emitStmt(IfStatement *Stmt) {
  bool HasElse = Stmt->getElseStmts().size() > 0;

  // Create the required basic blocks.
  llvm::BasicBlock *IfBB = llvm::BasicBlock::Create(
      CGM.getLLVMCtx(), "if.body", Fn);
  llvm::BasicBlock *ElseBB =
      HasElse ? llvm::BasicBlock::Create(CGM.getLLVMCtx(),
                                         "else.body", Fn)
              : nullptr;
  llvm::BasicBlock *AfterIfBB = llvm::BasicBlock::Create(
      CGM.getLLVMCtx(), "after.if", Fn);

  llvm::Value *Cond = emitExpr(Stmt->getCond());
  Builder.CreateCondBr(Cond, IfBB,
                       HasElse ? ElseBB : AfterIfBB);
  sealBlock(Curr);

  setCurr(IfBB);
  emit(Stmt->getIfStmts());
  if (!Curr->getTerminator()) {
    Builder.CreateBr(AfterIfBB);
  }
  sealBlock(Curr);

  if (HasElse) {
    setCurr(ElseBB);
    emit(Stmt->getElseStmts());
    if (!Curr->getTerminator()) {
      Builder.CreateBr(AfterIfBB);
    }
    sealBlock(Curr);
  }
  setCurr(AfterIfBB);
}

void CGProcedure::emitStmt(WhileStatement *Stmt) {
  // The basic block for the condition.
  llvm::BasicBlock *WhileCondBB = llvm::BasicBlock::Create(
      CGM.getLLVMCtx(), "while.cond", Fn);
  // The basic block for the while body.
  llvm::BasicBlock *WhileBodyBB = llvm::BasicBlock::Create(
      CGM.getLLVMCtx(), "while.body", Fn);
  // The basic block after the while statement.
  llvm::BasicBlock *AfterWhileBB = llvm::BasicBlock::Create(
      CGM.getLLVMCtx(), "after.while", Fn);

  Builder.CreateBr(WhileCondBB); // 生成无条件跳转
  sealBlock(Curr); // 验证当前块的前驱是否已经全部确定
  setCurr(WhileCondBB); // 将当前活跃的基本块设置为条件块
  llvm::Value *Cond = emitExpr(Stmt->getCond());
  Builder.CreateCondBr(Cond, WhileBodyBB, AfterWhileBB); //  生成条件分支指令

  setCurr(WhileBodyBB); // 将当前基本块切换到循环体块
  emit(Stmt->getWhileStmts()); // 遍历生成循环体内所有语句的IR
  Builder.CreateBr(WhileCondBB); // 在循环体块末尾插入无条件跳转指令，跳转回条件判断块，从而形成循环
  sealBlock(WhileCondBB);
  sealBlock(Curr);

  setCurr(AfterWhileBB); // 更新当前块以确保循环之后的代码都会插入该块中
}

void CGProcedure::emitStmt(ReturnStatement *Stmt) {
  if (Stmt->getRetVal()) {
    llvm::Value *RetVal = emitExpr(Stmt->getRetVal());
    Builder.CreateRet(RetVal);
  } else {
    Builder.CreateRetVoid();
  }
}

void CGProcedure::emit(const StmtList &Stmts) {
  for (auto *S : Stmts) {
    if (auto *Stmt = llvm::dyn_cast<AssignmentStatement>(S))
      emitStmt(Stmt);
    else if (auto *Stmt =
                 llvm::dyn_cast<ProcedureCallStatement>(S))
      emitStmt(Stmt);
    else if (auto *Stmt = llvm::dyn_cast<IfStatement>(S))
      emitStmt(Stmt);
    else if (auto *Stmt = llvm::dyn_cast<WhileStatement>(S))
      emitStmt(Stmt);
    else if (auto *Stmt =
                 llvm::dyn_cast<ReturnStatement>(S))
      emitStmt(Stmt);
    else
      llvm_unreachable("Unknown statement");
  }
}

void CGProcedure::run(ProcedureDeclaration *Proc) {
  this->Proc = Proc;
  Fty = createFunctionType(Proc);
  Fn = createFunction(Proc, Fty);

  llvm::BasicBlock *BB = llvm::BasicBlock::Create(
      CGM.getLLVMCtx(), "entry", Fn);
  setCurr(BB); // 创建入口基本块并设置为当前块

  for (auto Pair : llvm::enumerate(Fn->args())) {
    llvm::Argument *Arg = &Pair.value();
    FormalParameterDeclaration *FP =
        Proc->getFormalParams()[Pair.index()]; // 记录形参
    // Create mapping FormalParameter -> llvm::Argument for
    // VAR parameters.
    FormalParams[FP] = Arg;
    writeLocalVariable(Curr, FP, Arg); // 在当前块中声明这个参数的初始值
  }

  for (auto *D : Proc->getDecls()) {
    if (auto *Var =
            llvm::dyn_cast<VariableDeclaration>(D)) {
      llvm::Type *Ty = mapType(Var);
      if (Ty->isAggregateType()) {
        llvm::Value *Val = Builder.CreateAlloca(Ty);
        writeLocalVariable(Curr, Var, Val);
      }
    }
  }

  auto Block = Proc->getStmts(); 
  emit(Proc->getStmts()); // 遍历 AST 并为每条语句生成对应的 IR 指令
  if (!Curr->getTerminator()) {
    Builder.CreateRetVoid(); // 检查当前基本块是否有终止指令，如没有则插入一个返回 void 的指令确保每个函数都有一个合法退出点
  }
  sealBlock(Curr);
}
