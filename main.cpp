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

#include <iostream>
#include <string>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/LTO/LTOModule.h>
#include <llvm/LTO/LTOCodeGenerator.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Object/Archive.h>
#include "llvm-compat.h"

using namespace llvm;

namespace {

cl::opt<bool> GenerateDebugSymbols("generate-debug-symbols",
                                   cl::desc("generate debug symbols"),
                                   cl::init(false));

cl::opt<bool> DisableOptimizations("disable-optimizations",
                                   cl::desc("disable optimizations"),
                                   cl::init(false));

cl::opt<bool> DisableInlinePass("disable-inline-pass",
                                cl::desc("disable inline pass"),
                                cl::init(false));

cl::opt<bool> DisableGVNPass("disable-gvn-pass", cl::desc("disable gvn pass"),
                             cl::init(false));
cl::opt<bool> DisableVectorizationPass("disable-vectorization-pass",
                                       cl::desc("disable vectorization pass"),
                                       cl::init(false));

cl::opt<bool> PIC("pic", cl::desc("generate position independent code"),
                  cl::init(false));

cl::opt<bool> PIE("pie", cl::desc("generate position independent code"),
                  cl::init(false));

cl::list<std::string> BitCodeFiles(cl::Sink, cl::OneOrMore);

cl::opt<std::string> OutDir("out-dir", cl::desc("output directory"),
                            cl::init("native"));

class BitCodeArchive {
public:
  BitCodeArchive(const char *Path, bool &OK)
      : Buf(MemoryBuffer::getFile(Path, -1, false)) {
    if (Buf.getError()) {
      std::cerr << Path << ": cannot open archive" << std::endl;
      OK = false;
      return;
    }

    std::error_code EC;
    Archive = new object::Archive(getMemBuffer(Buf.get()), EC);

    if (EC) {
      std::cerr << Path << ": invalid archive" << std::endl;
      OK = false;
      return;
    }

    OK = true;
  }

  ~BitCodeArchive() { delete Archive; }

  const object::Archive &getArchive() { return *Archive; }

private:
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf;
  object::Archive *Archive;
};

class BitCodeModule {
  friend class NativeCodeGenerator;

public:
  BitCodeModule(const std::string &Path, bool &OK) : isNativeObjectFile(false) {
    std::string errMsg;
    Module = LTOModule::createFromFile(Path.c_str(), TargetOpts, errMsg);

    if (!(OK = !!Module)) {
      if (errMsg == "Bitcode section not found in object file") {
        isNativeObjectFile = true;
        return;
      }

      std::cerr << Path << ": " << errMsg << std::endl;
    }
  }

  ~BitCodeModule() { delete Module; }

private:
  bool isNativeObjectFile;
  TargetOptions TargetOpts;
  LTOModule *Module;
};

class NativeCodeGenerator {
public:
  NativeCodeGenerator(const std::string &Path, bool &OK,
                      bool &isNativeObjectFile)
      : Path(Path), BCModule(Path, OK) {
    OutPath = OutDir;
    OutPath += PATH_DIV;
    OutPath += getObjFileName();

    isNativeObjectFile = BCModule.isNativeObjectFile;

    if (!OK) {
      if (isNativeObjectFile) {
        if (sys::fs::copy_file(Path, OutPath)) {
          std::cerr << "cannot copy " << Path << " to " << OutPath << std::endl;
          isNativeObjectFile = false;
          return;
        }
      }

      return;
    }

    LTOModule *Module = BCModule.Module;
    std::string errMsg;

    if (!CodeGen.addModule(Module, errMsg)) {
      OK = false;
      return;
    }

    uint32_t NumSymbols = Module->getSymbolCount();

    for (uint32_t I = 0; I < NumSymbols; ++I)
      CodeGen.addMustPreserveSymbol(Module->getSymbolName(I));

    OK = true;
    return;
  }

  bool generateNativeCode() {
    std::string errMsg;
    const char *name;

    setupCodeGenOpts();

    if (!CodeGen.compile_to_file(&name, DisableOptimizations, DisableInlinePass,
                                 DisableGVNPass, DisableVectorizationPass,
                                 errMsg)) {
      std::cerr << Path << ":" << errMsg << std::endl;
      return false;
    }

    if (sys::fs::rename(name, OutPath)) {
      std::cerr << "cannot rename " << name << " to " << OutPath << std::endl;
      return false;
    }

    return true;
  }

  const char *getOutputPath() { return OutPath.c_str(); }

private:
  void setupCodeGenOpts() {
    if (PIC)
      CodeGen.setCodePICModel(LTO_CODEGEN_PIC_MODEL_DYNAMIC);

    if (PIE)
      CodeGen.setCodePICModel(LTO_CODEGEN_PIC_MODEL_STATIC);

    CodeGen.setDebugInfo(GenerateDebugSymbols ? LTO_DEBUG_MODEL_DWARF
                                              : LTO_DEBUG_MODEL_NONE);
  }

  const char *getObjFileName() {
    const char *p = Path.c_str();
    const char *FileName = std::strrchr(p, PATH_DIV);
    return FileName ? FileName + 1 : p;
  }

  std::string Path;
  std::string OutPath;
  BitCodeModule BCModule;
  LTOCodeGenerator CodeGen;
};

bool isArchive(const char *Path) {
  const char *Ext = std::strrchr(Path, '.');
  if (!Ext)
    return false;
  return !strcmp(Ext, ".a");
}

bool processFile(const std::string &File) {
  bool isFile;

  if (sys::fs::is_regular_file(File, isFile) || !isFile) {
    std::cerr << File << ": is not a file" << std::endl;
    return false;
  }

  if (isArchive(File.c_str())) {
    std::cerr << File << ": archives are not supported yet" << std::endl;
    return false;
  }

  bool OK;
  bool isNativeObjectFile;

  NativeCodeGenerator NCodeGen(File, OK, isNativeObjectFile);

  if (OK || isNativeObjectFile) {
    std::cout << "codegen'ing " << File << " to " << NCodeGen.getOutputPath()
              << std::endl;

    if (isNativeObjectFile)
      return true;
  }

  if (!NCodeGen.generateNativeCode()) {
    std::cerr << "cannot codegen " << File << std::endl;
    return false;
  }

  return true;
}

} // end unnamed namespace

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv,
                              "bitcode to native object file converter\n");

  if (sys::fs::create_directory(OutDir)) {
    std::cerr << "cannot create directory " << OutDir << std::endl;
    return 1;
  }

  if (BitCodeFiles.empty()) {
    std::cerr << "no bitcode files specified" << std::endl;
    return 1;
  }

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  for (auto &BitCodeFile : BitCodeFiles)
    if (!processFile(BitCodeFile))
      return 1;

  return 0;
}
