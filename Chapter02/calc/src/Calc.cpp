#include "CodeGen.h"
#include "Parser.h"
#include "Sema.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

static llvm::cl::opt<std::string>
    Input(llvm::cl::Positional,
          llvm::cl::desc("<input expression>"),
          llvm::cl::init("")); // 静态变量，被绑定为一个位置参数：当用户运行编译器时，输入的表达式通过命令行被读取并存储到该变量中，供后续词法分析器使用

int main(int argc, const char **argv) {
  llvm::InitLLVM X(argc, argv); // 初始化 LLVM 库
  llvm::cl::ParseCommandLineOptions(
      argc, argv, "calc - the expression compiler\n"); // 处理用户在命令行输入的选项

  // lexical -> syntactical -> semantic
  Lexer Lex(Input);
  Parser Parser(Lex);
  AST *Tree = Parser.parse();
  if (!Tree || Parser.hasError()) {
    llvm::errs() << "Syntax errors occured\n";
    return 1;
  }
  Sema Semantic;
  if (Semantic.semantic(Tree)) {
    llvm::errs() << "Semantic errors occured\n";
    return 1;
  }
  CodeGen CodeGenerator;
  CodeGenerator.compile(Tree); // 分析通过后遍历 AST 生成 LLVM IR 并打印
  return 0;
}