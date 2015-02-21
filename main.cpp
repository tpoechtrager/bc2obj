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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
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
  BitCodeArchive(const std::string &Path, bool &OK)
      : Buf(MemoryBuffer::getFile(Path.c_str(), -1, false)) {
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
    check(errMsg, Path, OK);
  }

  BitCodeModule(const std::string &Path, StringRef Data, bool &OK)
      : isNativeObjectFile(false) {
    std::string errMsg;
    Module = LTOModule::createFromBuffer(Data.data(), Data.size(), TargetOpts,
                                         errMsg);
    check(errMsg, Path, OK);
  }

  ~BitCodeModule() { delete Module; }

private:
  void check(const std::string &errMsg, const std::string &Path, bool &OK) {
    if (!(OK = !!Module)) {
      if (errMsg == "Bitcode section not found in object file") {
        isNativeObjectFile = true;
        return;
      }

      std::cerr << Path << ": " << errMsg << std::endl;
    }
  }

  bool isNativeObjectFile;
  TargetOptions TargetOpts;
  LTOModule *Module;
};

class NativeCodeGenerator {
public:
  NativeCodeGenerator(const std::string &Path, bool &OK,
                      bool &isNativeObjectFile)
      : Path(Path), BCModule(Path, OK) {
    setOutPutPath();
    isNativeObjectFile = BCModule.isNativeObjectFile;

    if (isNativeObjectFile)
      OK = true;
  }

  NativeCodeGenerator(const std::string &Path, StringRef Data, bool &OK,
                      bool &isNativeObjectFile)
      : Path(Path), BCModule(Path, Data, OK), Data(Data) {
    setOutPutPath();
    isNativeObjectFile = BCModule.isNativeObjectFile;

    if (isNativeObjectFile)
      OK = true;
  }

  const char *getObjFileName() const { return getFileName(Path.c_str()); }

  bool generateNativeCode() {
    if (BCModule.isNativeObjectFile) {
      if (sys::fs::copy_file(Path, OutPath)) {
        std::cerr << "cannot copy " << Path << " to " << OutPath << std::endl;
        return false;
      }
      return true;
    }

    std::string errMsg;
    const char *name;

    if (!setupCodeGenOpts())
      return false;

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

  bool generateNativeCodeMemory() {
    if (BCModule.isNativeObjectFile) {
      code.Code = Data.data();
      code.Length = Data.size();
      return true;
    }

    std::string errMsg;

    if (!setupCodeGenOpts())
      return false;

    code.Code =
        CodeGen.compile(&code.Length, DisableOptimizations, DisableInlinePass,
                        DisableGVNPass, DisableVectorizationPass, errMsg);

    if (!code.Code) {
      std::cerr << Path << ":" << errMsg << std::endl;
      return false;
    }

    return true;
  }

  bool writeCodeToDisk(const std::string &Dir, std::string &Path) {
    Path = Dir;
    Path += PATH_DIV;
    Path += getObjFileName();

    int fd;
    if (sys::fs::openFileForWrite(Path, fd, sys::fs::F_RW)) {
      std::cerr << Path << ": cannot open file for writing" << std::endl;
      return false;
    }

    bool OK = write(fd, code.Code, code.Length) == (ssize_t)code.Length;

    close(fd);
    return OK;
  }

  struct Code;
  const Code &getCode() { return code; }

  const char *getOutputPath() { return OutPath.c_str(); }

  struct Code {
    const void *Code;
    size_t Length;
  };

private:
  bool setupCodeGenOpts() {
    std::string errMsg;

    if (!CodeGen.addModule(BCModule.Module, errMsg)) {
      std::cerr << errMsg << std::endl;
      return false;
    }

    uint32_t NumSymbols = BCModule.Module->getSymbolCount();

    for (uint32_t I = 0; I < NumSymbols; ++I)
      CodeGen.addMustPreserveSymbol(BCModule.Module->getSymbolName(I));

    if (PIC)
      CodeGen.setCodePICModel(LTO_CODEGEN_PIC_MODEL_DYNAMIC);

    if (PIE)
      CodeGen.setCodePICModel(LTO_CODEGEN_PIC_MODEL_STATIC);

    CodeGen.setDebugInfo(GenerateDebugSymbols ? LTO_DEBUG_MODEL_DWARF
                                              : LTO_DEBUG_MODEL_NONE);

    return true;
  }

  void setOutPutPath() {
    OutPath = OutDir;
    OutPath += PATH_DIV;
    OutPath += getObjFileName();
  }

  std::string Path;
  std::string OutPath;
  BitCodeModule BCModule;
  LTOCodeGenerator CodeGen;
  StringRef Data;
  Code code;
};

bool isArchive(const char *Path) {
  // FIXME: check header
  const char *Ext = std::strrchr(Path, '.');
  if (!Ext)
    return false;
  return !std::strcmp(Ext, ".a");
}

bool createNativeArchive(
    const char *ArchiveName,
    const std::vector<std::unique_ptr<NativeCodeGenerator>> &Objs) {
  SmallVector<char, 32> tmp;
  bool OK = true;

  if (sys::fs::createUniqueDirectory("", tmp)) {
    std::cerr << "cannot create temporary directory" << std::endl;
    return false;
  }

  std::string Path;
  std::vector<std::string> Files;

  std::cout << "writing native object files to disk (" << &tmp[0] << ")"
            << std::endl;

  for (auto &Obj : Objs) {
    if (!Obj->writeCodeToDisk(&tmp[0], Path)) {
      OK = false;
      break;
    }

    Files.push_back(std::move(Path));
  }

  if (OK) {
    pid_t pid = fork();

    if (pid > 0) {
      int status;

      if (waitpid(pid, &status, 0) == -1)
        std::abort();

      if (WIFSIGNALED(status)) {
        std::cerr << "child exited with signal: " << strsignal(WTERMSIG(status))
                  << std::endl;
        return false;
      }

      if (WIFEXITED(status)) {
        int ec = WEXITSTATUS(status);
        if (ec)
          OK = false;
      }
    } else if (pid < 0) {
      std::abort();
    } else {
      // child
      std::vector<const char *> Args;

      std::string OutputFile = OutDir;
      OutputFile += PATH_DIV;
      OutputFile += ArchiveName;

      Args.push_back("llvm-ar");
      Args.push_back("rcs");
      Args.push_back(OutputFile.c_str());

      for (auto &File : Files)
        Args.push_back(File.c_str());

      Args.push_back(nullptr);

      std::cout << OutputFile << ": generating archive" << std::endl;
      execvp(Args[0], const_cast<char **>(Args.data()));

      std::cerr << "llvm-ar not in PATH?" << std::endl;
      _exit(EXIT_FAILURE);
    }
  }

  Files.push_back(&tmp[0]);

  for (auto &File : Files) {
    if (sys::fs::remove(File.c_str(), false)) {
      std::cerr << File << ": cannot remove file" << std::endl;
      OK = false;
    }
  }

  return OK;
}

bool processFile(const std::string &File) {
  bool isFile;

  if (sys::fs::is_regular_file(File, isFile) || !isFile) {
    std::cerr << File << ": is not a file" << std::endl;
    return false;
  }

  bool OK;
  bool isNativeObjectFile;

  if (isArchive(File.c_str())) {
    bool OK;
    BitCodeArchive BCAr(File, OK);
    const object::Archive &Archive = BCAr.getArchive();

    std::vector<std::unique_ptr<NativeCodeGenerator>> Objs;
    std::string ObjName;

    for (auto Obj = Archive.child_begin(); Obj != Archive.child_end(); ++Obj) {
      StringRef Buf = Obj->getBuffer();
      ObjName = getObjName(Obj);

      std::unique_ptr<NativeCodeGenerator> NCodeGen(
          new NativeCodeGenerator(ObjName, Buf, OK, isNativeObjectFile));

      if (!OK)
        return false;

      std::cout << "codegen'ing " << File << "(" << ObjName << ") to memory"
                << std::endl;

      if (!NCodeGen->generateNativeCodeMemory())
        return false;

      Objs.push_back(std::move(NCodeGen));
    }

    return createNativeArchive(getFileName(File.c_str()), Objs);
  }

  NativeCodeGenerator NCodeGen(File, OK, isNativeObjectFile);

  if (OK)
    std::cout << "codegen'ing " << File << " to " << NCodeGen.getOutputPath()
              << std::endl;

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
