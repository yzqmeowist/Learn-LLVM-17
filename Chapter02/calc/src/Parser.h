#ifndef PARSER_H
#define PARSER_H

#include "AST.h"
#include "Lexer.h"
#include "llvm/Support/raw_ostream.h" // LLVM 输出流

class Parser {
  Lexer &Lex;
  Token Tok; // look-ahead token
  bool HasError;

  void error() {
    llvm::errs() << "Unexpected: " << Tok.getText() << "\n";
    HasError = true;
  }

  void advance() { Lex.next(Tok); } // 调用 Lexer.next() 将 Tok 更新为下一个 token

  bool expect(Token::TokenKind Kind) { // 检查当前 look-ahead 的 token Tok 的种类是否是 Kind
    if (!Tok.is(Kind)) {
      error();
      return true;
    }
    return false; // 种类匹配
  }

  bool consume(Token::TokenKind Kind) { // 用 expect 检查当前 token 种类
    if (expect(Kind))
      return true; // 消费失败，报错
    advance(); // 成功消费 token
    return false;
  }

  AST *parseCalc();
  Expr *parseExpr();
  Expr *parseTerm();
  Expr *parseFactor();

public:
  Parser(Lexer &Lex) : Lex(Lex), HasError(false) {
    advance(); // 构造时读取第一个 token, Tok 被赋予初始值
  }
  AST *parse(); // 从头开始解析整个输入，如成功则返回指向完整 AST 的指针
  bool hasError() { return HasError; }
};

#endif