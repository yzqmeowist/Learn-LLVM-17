#ifndef TINYLANG_BASIC_TOKENKINDS_H
#define TINYLANG_BASIC_TOKENKINDS_H

#include "llvm/Support/Compiler.h"

namespace tinylang {

/* token的种类：
   KEYWORD(ID, FLAG): 关键字
   PUNCTUATOR(ID, SP): 标点符号
   TOK(ID): 所有词法单元的基础标签 
      关键字是标识符的子集：词法分析器扫描到一组字母时首先将其识别成一个标识符，再
      额外进行一次关键字过滤 keyword filter 以检查该标识符是否恰好是关键字
*/
namespace tok {
enum TokenKind : unsigned short {
#define TOK(ID) ID,
#include "TokenKinds.def"
  NUM_TOKENS
};

const char *getTokenName(TokenKind Kind) LLVM_READNONE;

const char *
getPunctuatorSpelling(TokenKind Kind) LLVM_READNONE;

const char *
getKeywordSpelling(TokenKind Kind) LLVM_READNONE;
} // namespace tok
} // namespace tinylang

#endif