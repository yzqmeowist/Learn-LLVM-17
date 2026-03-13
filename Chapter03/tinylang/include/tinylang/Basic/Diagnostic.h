#ifndef TINYLANG_BASIC_DIAGNOSTIC_H
#define TINYLANG_BASIC_DIAGNOSTIC_H

#include "tinylang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

namespace tinylang {

namespace diag {
enum { // 包含所有消息 ID
#define DIAG(ID, Level, Msg) ID,
#include "tinylang/Basic/Diagnostic.def"
};
} // namespace diag

class DiagnosticsEngine {
  static const char *getDiagnosticText(unsigned DiagID); // 根据消息 ID 返回对应的消息文本字符串
  static SourceMgr::DiagKind
  getDiagnosticKind(unsigned DiagID); // 根据消息 ID 返回其严重级别

  SourceMgr &SrcMgr; // 访问源文件缓冲区，打印消息
  unsigned NumErrors; // 累计错误数量

public:
  DiagnosticsEngine(SourceMgr &SrcMgr)
      : SrcMgr(SrcMgr), NumErrors(0) {}

  unsigned numErrors() { return NumErrors; }

  /* SMLoc: 错误在源代码中的位置
     DiagID: 诊断消息的枚举 ID
     Arguments: 提供消息文本中所需的实际值
  */ 
  template <typename... Args>
  void report(SMLoc Loc, unsigned DiagID,
              Args &&... Arguments) {
    std::string Msg =
        llvm::formatv(getDiagnosticText(DiagID),
                      std::forward<Args>(Arguments)...)
            .str();
    SourceMgr::DiagKind Kind = getDiagnosticKind(DiagID);
    SrcMgr.PrintMessage(Loc, Kind, Msg);
    NumErrors += (Kind == SourceMgr::DK_Error);
  }
};

} // namespace tinylang

#endif