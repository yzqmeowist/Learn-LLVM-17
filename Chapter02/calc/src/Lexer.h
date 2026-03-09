#ifndef LEXER_H
#define LEXER_H

// 指向 C 风格字符串的指针，存储了长度信息，因此指向的字符串不一定需要以零字符结尾
// 适合指向MemoryBuffer管理的内存，安全表示词元的文本内容
#include "llvm/ADT/StringRef.h"

// 对内存块的只读访问，通常用来加载文件内容，会在缓冲区末尾自动添加一个终止零字符\x00以避免每次检查是否越界
#include "llvm/Support/MemoryBuffer.h"

class Lexer;

class Token {
  friend class Lexer;

public:
  enum TokenKind : unsigned short {
    eoi, // end of input
    unknown,
    ident,
    number,
    comma,
    colon,
    plus,
    minus,
    star,
    slash,
    l_paren,
    r_paren,
    KW_with // with
  };

private:
  TokenKind Kind;
  llvm::StringRef Text; // 指向该 token 在源文件缓冲区中所对应的文本的起始位置

public:
  TokenKind getKind() const { return Kind; }
  llvm::StringRef getText() const {
    return Text;
  }

  bool is(TokenKind K) const { return Kind == K; }
  bool isOneOf(TokenKind K1, TokenKind K2) const {
    return is(K1) || is(K2);
  }
  template <typename... Ts>
  bool isOneOf(TokenKind K1, TokenKind K2, Ts... Ks) const {
    return is(K1) || isOneOf(K2, Ks...);
  }
};

class Lexer {
  const char *BufferStart; // 指向源代码起始位置
  const char *BufferPtr; // 指向下一个待处理的字符

public:
  Lexer(const llvm::StringRef &Buffer) {
    BufferStart = Buffer.begin();
    BufferPtr = BufferStart;
  }

  void next(Token &token);

private:
  void formToken(Token &Result, const char *TokEnd,
                 Token::TokenKind Kind); // 由 next 确定当前 token 的种类和文本的结束位置后，构造 token 对象
};
#endif