/*
  Copyright (c) 2015 Thomas Poechtrager (t.poechtrager@gmail.com)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
 */

#ifndef LLVM_VERSION_MAJOR
#include <clang/Basic/Version.inc>
#define LLVM_VERSION_MAJOR CLANG_VERSION_MAJOR
#define LLVM_VERSION_MINOR CLANG_VERSION_MINOR
#endif

#define LLVM_VERSION_GE(major, minor)                                          \
  ((LLVM_VERSION_MAJOR * 10000 + LLVM_VERSION_MINOR * 100) >=                  \
   (major * 10000 + minor * 100))

#define LLVM_VERSION_LT(major, minor)                                          \
  ((LLVM_VERSION_MAJOR * 10000 + LLVM_VERSION_MINOR * 100) <                   \
   (major * 10000 + minor * 100))

#define LLVM_VERSION_EQ(major, minor)                                          \
  ((LLVM_VERSION_MAJOR * 10000 + LLVM_VERSION_MINOR * 100) ==                  \
   (major * 10000 + minor * 100))

#ifdef _WIN32
#define PATH_DIV '\\'
#else
#define PATH_DIV '/'
#endif

#if LLVM_VERSION_LT(3, 6)
#define compile_to_file(name, disableOpt, disableInline, disableGVNLoadPRE,    \
                        disableVectorization, errMsg)                          \
  compile_to_file(name, disableOpt, disableInline, disableGVNLoadPRE, errMsg)

#define compile(length, disableOpt, disableInline, disableGVNLoadPRE,          \
                disableVectorization, errMsg)                                  \
  compile(length, disableOpt, disableInline, disableGVNLoadPRE, errMsg)
#elif LLVM_VERSION_GE(3, 7)
#define compile_to_file(name, disableOpt, disableInline, disableGVNLoadPRE,    \
                        disableVectorization, errMsg)                          \
  compile_to_file(name, disableInline, disableGVNLoadPRE,                      \
                  disableVectorization, errMsg)

#define compile(length, disableOpt, disableInline, disableGVNLoadPRE,          \
                disableVectorization, errMsg)                                  \
  compile(disableInline, disableGVNLoadPRE, disableVectorization, errMsg)
#endif

#if LLVM_VERSION_GE(3, 6)
#define addModule(Module, errMsg) addModule(Module)
#endif

static auto moveMemBuffer = [](auto &Buf) {
#if LLVM_VERSION_GE(3, 5)
  return std::move(Buf);
#else
  return Buf.take();
#endif
};

static auto getMemBuffer = [](auto &Buf) {
#if LLVM_VERSION_GE(3, 6)
  return Buf.get()->getMemBufferRef();
#else
  return moveMemBuffer(Buf);
#endif
};

#if LLVM_VERSION_GE(3, 6)
namespace llvm {
namespace sys {
static inline std::string FindProgramByName(const std::string &name) {
  auto Prog = findProgramByName(name);
  return Prog ? *Prog : std::string();
}
}
}
#endif
