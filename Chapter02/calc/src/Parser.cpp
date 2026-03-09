#include "Parser.h"

AST *Parser::parse() {
  AST *Res = parseCalc();
  expect(Token::eoi);
  return Res;
}

AST *Parser::parseCalc() {
  Expr *E; // AST 节点指针
  llvm::SmallVector<llvm::StringRef, 8> Vars; // 存储解析到的所有变量名
  if (Tok.is(Token::KW_with)) { // 先检查当前 token 是否为 with -- "with" ident ("," ident)* ":" expr ;
    advance();
    if (expect(Token::ident)) // with 后紧跟 ident
      goto _error;
    Vars.push_back(Tok.getText()); // getText 获取 ident 的文本内容并压入 Vars
    advance();
    while (Tok.is(Token::comma)) { // 允许多个变量：多个 ident 循环结构
      advance();
      if (expect(Token::ident))
        goto _error;
      Vars.push_back(Tok.getText());
      advance();
    }
    if (consume(Token::colon)) // :
      goto _error;
  }
  E = parseExpr(); // expr 解析
  if (expect(Token::eoi)) // 结束
    goto _error;
  if (Vars.empty())
    return E;
  else
    return new WithDecl(Vars, E); // Vars 不为空时创建一个 WithDecl 类型的 AST 节点
_error:
  while (Tok.getKind() != Token::eoi) // panic mode: 检测到语法错误时，分析器连续丢弃输入 token 直到遇到安全 token, 对于 calc 来说是结束符 eoi
    advance();
  return nullptr;
}

Expr *Parser::parseExpr() { // 解析加减法表达式
  Expr *Left = parseTerm();
  while (Tok.isOneOf(Token::plus, Token::minus)) {
    BinaryOp::Operator Op = Tok.is(Token::plus)
                                ? BinaryOp::Plus
                                : BinaryOp::Minus;
    advance();
    Expr *Right = parseTerm();
    Left = new BinaryOp(Op, Left, Right); // 创建 BinaryOp 节点，每次将新建节点赋值给 Left, 从左到右构建表达式树
  }
  return Left;
}

Expr *Parser::parseTerm() { // 解析乘除法表达式，与加减法分离使得乘除法优先于加减法运算
  Expr *Left = parseFactor();
  while (Tok.isOneOf(Token::star, Token::slash)) {
    BinaryOp::Operator Op =
        Tok.is(Token::star) ? BinaryOp::Mul : BinaryOp::Div;
    advance();
    Expr *Right = parseFactor();
    Left = new BinaryOp(Op, Left, Right);
  }
  return Left;
}

Expr *Parser::parseFactor() { // factor: ident | number | "(" expr ")" ;
  Expr *Res = nullptr;
  switch (Tok.getKind()) {
  case Token::number:
    Res = new Factor(Factor::Number, Tok.getText());
    advance(); break;
  case Token::ident:
    Res = new Factor(Factor::Ident, Tok.getText());
    advance(); break;
  case Token::l_paren:
    advance();
    Res = parseExpr();
    if (!consume(Token::r_paren)) break;
  default:
    if (!Res)
      error();
    while (!Tok.isOneOf(Token::r_paren, Token::star,
                        Token::plus, Token::minus,
                        Token::slash, Token::eoi))
      advance();
  }
  return Res;
}
