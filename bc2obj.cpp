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

#include "bc2obj.h"

// Misc

const char *getFileName(const char *Path) {
  const char *FileName = std::strrchr(Path, PATH_DIV);
  return FileName ? FileName + 1 : Path;
}

// Jobs

int ActiveJobs;

pid_t forkProcess(bool wait, bool *OK) {
#ifndef _WIN32
  pid_t pid = fork();

  if (pid > 0) {
    if (wait) {
      bool V = waitForChild(pid) > 0;
      if (OK)
        *OK = V;
    }
  } else if (pid < 0) {
    std::cerr << "fork() failed" << std::endl;
    std::abort();
  }

  return pid;
#else
  if (OK)
    *OK = true;
  return 0;
#endif
}

int waitForChild(const pid_t pid) {
#ifndef _WIN32
  int status;

  if (waitpid(-1, &status, 0) == -1) {
    std::cerr << "waitpid() failed" << std::endl;
    std::abort();
  }

  if (WIFSIGNALED(status)) {
    std::cerr << "uncaught signal: " << strsignal(WTERMSIG(status))
              << std::endl;
    return -1;
  }

  if (WIFEXITED(status)) {
    int EC = WEXITSTATUS(status);

    if (EC)
      return -2;
  }
#endif
  return 1;
}

bool waitForJob() {
  bool OK = true;
  if (ActiveJobs >= NumJobs) {
    OK = waitForChild(-1) > 0;
    ActiveJobs--;
  }
  return OK;
}

bool waitForJobs() {
  bool OK = true;
  while (ActiveJobs > 0) {
    if (waitForChild(-1) <= 0)
      OK = false;
    ActiveJobs--;
  }
  ActiveJobs = 0;
  return OK;
}

// BitCodeArchive -> Public

BitCodeArchive::BitCodeArchive(const std::string &Path, bool &OK)
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

BitCodeArchive::~BitCodeArchive() { delete Archive; }

std::string
BitCodeArchive::getObjName(const llvm::object::Archive::child_iterator &child) {
  llvm::StringRef ObjName;
  llvm::ErrorOr<llvm::StringRef> Name = child->getName();
  if (Name.getError())
    ObjName = "<unknown>";
  else
    ObjName = Name.get();
  return std::string(ObjName.data(), ObjName.size());
}

// BitCodeModule -> Public

BitCodeModule::BitCodeModule(const std::string &Path, bool &OK)
    : isNativeObjectFile(false) {
  std::string errMsg;
  Module = LTOModule::createFromFile(Path.c_str(), TargetOpts, errMsg);
  check(errMsg, Path, OK);
  setTriple(OK);
}

BitCodeModule::BitCodeModule(const std::string &Path, StringRef Data, bool &OK)
    : isNativeObjectFile(false) {
  std::string errMsg;
  Module =
      LTOModule::createFromBuffer(Data.data(), Data.size(), TargetOpts, errMsg);
  check(errMsg, Path, OK);
  setTriple(OK);
}

BitCodeModule::~BitCodeModule() { delete Module; }

// BitCodeModule -> Private

void BitCodeModule::check(const std::string &errMsg, const std::string &Path,
                          bool &OK) {
  if (!(OK = !!Module)) {
    if (errMsg == "Bitcode section not found in object file") {
      isNativeObjectFile = true;
      return;
    }

    std::cerr << Path << ": " << errMsg << std::endl;
  }
}

void BitCodeModule::setTriple(bool &OK) {
  if (!OK)
    return;

  TripleStr = Module->getTargetTriple();

  if (TripleStr.empty()) {
    OK = false;
    return;
  }

  Triple = llvm::Triple(TripleStr);
}

// NativeCodeGenerator -> Public

NativeCodeGenerator::NativeCodeGenerator(const std::string &Path, bool &OK,
                                         bool &isNativeObjectFile)
    : Path(Path), BCModule(Path, OK) {
  setOutPutPath();
  isNativeObjectFile = BCModule.isNativeObjectFile;

  if (isNativeObjectFile)
    OK = true;
}

NativeCodeGenerator::NativeCodeGenerator(const std::string &Path,
                                         StringRef Data, bool &OK,
                                         bool &isNativeObjectFile)
    : Path(Path), BCModule(Path, Data, OK), Data(Data) {
  setOutPutPath();
  isNativeObjectFile = BCModule.isNativeObjectFile;

  if (isNativeObjectFile)
    OK = true;
}

bool NativeCodeGenerator::generateNativeCode() {
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
    errmsg(Path << ":" << errMsg);
    return false;
  }

  if (sys::fs::rename(name, OutPath)) {
    if (errno != EXDEV || sys::fs::copy_file(name, OutPath) ||
      sys::fs::remove(name)) {
      errmsg("cannot rename " << name << " to " << OutPath);
      return false;
    }
  }

  return true;
}

bool NativeCodeGenerator::generateNativeCodeMemory() {
  if (BCModule.isNativeObjectFile) {
    code.Code = Data.data();
    code.Length = Data.size();
    return true;
  }

  std::string errMsg;

  if (!setupCodeGenOpts())
    return false;

  auto CodeBuf =
      CodeGen.compile(&code.Length, DisableOptimizations, DisableInlinePass,
                      DisableGVNPass, DisableVectorizationPass, errMsg);

#if LLVM_VERSION_GE(3, 7)
  if (auto *MemBuffer = CodeBuf.get()) {
    code.CodeBuf = std::move(CodeBuf);
    code.Code = MemBuffer;
    code.Length = MemBuffer->getBufferSize();
  } else {
    code.Code = nullptr;
  }
#else
  code.Code = CodeBuf;
#endif

  if (!code.Code) {
    errmsg(Path << ":" << errMsg);
    return false;
  }

  return true;
}

bool NativeCodeGenerator::writeCodeToDisk(const std::string &Dir) {
  std::string Path;

  Path = Dir;
  Path += PATH_DIV;
  Path += getObjFileName();

  int fd;
  if (sys::fs::openFileForWrite(Path, fd, sys::fs::F_RW)) {
    errmsg(Path << ": cannot open file for writing");
    return false;
  }

  bool OK = write(fd, code.Code, code.Length) == (ssize_t)code.Length;

  close(fd);
  return OK;
}

// NativeCodeGenerator -> Private

const char *NativeCodeGenerator::getDefaultTargetCPU() const {
  const auto &Triple = BCModule.Triple;

  if (Triple.isOSDarwin()) {
    switch (Triple.getArch()) {
    case Triple::x86_64:
      return "core2";
    case Triple::x86:
      return "yonah";
    case Triple::aarch64:
      return "cyclone";
    default:
      ;
    }
  } else {
    if (Triple.getArch() == Triple::x86_64) {
      return "x86-64";
    } else if (Triple.getArch() == Triple::x86) {
      if (Triple.getEnvironment() == Triple::Android) {
        return "i686";
      } else {
        switch (Triple.getOS()) {
        case Triple::FreeBSD:
        case Triple::NetBSD:
        case Triple::OpenBSD:
          return "i486";
        case Triple::Haiku:
          return "i586";
        case Triple::Bitrig:
          return "i686";
        default:
          return "pentium4";
        }
      }
    }
  }

  return "";
}

bool NativeCodeGenerator::setupCodeGenOpts() {
  if (!::Target.empty()) {
    bool OK = true;
    BCModule.Module->setTargetTriple(::Target.c_str());
    BCModule.setTriple(OK);
    if (!OK)
      return false;
  }

  std::string errMsg;

  if (!CodeGen.addModule(BCModule.Module, errMsg)) {
    errmsg(errMsg);
    return false;
  }

  uint32_t NumSymbols = BCModule.Module->getSymbolCount();

  for (uint32_t I = 0; I < NumSymbols; ++I) {
    const auto SymAttr = BCModule.Module->getSymbolAttributes(I);
    switch (SymAttr & LTO_SYMBOL_DEFINITION_MASK) {
    case LTO_SYMBOL_DEFINITION_REGULAR:
    case LTO_SYMBOL_DEFINITION_TENTATIVE:
    case LTO_SYMBOL_DEFINITION_WEAK:
      CodeGen.addMustPreserveSymbol(BCModule.Module->getSymbolName(I));
    }
  }

  if (!LLVMOpts.empty()) {
    for (auto LLVMOpt : LLVMOpts)
      CodeGen.setCodeGenDebugOptions(LLVMOpt.c_str());
    CodeGen.parseCodeGenDebugOptions();
  }

  bool isOSWindows = (PIC || PIE) && BCModule.Triple.isOSWindows();

  if (PIC && isOSWindows) {
    errmsg("warning: " << Path << ": '-pic' has no effect for target "
                       << BCModule.TripleStr << '\'');
  } else if (PIE && isOSWindows) {
    errmsg("warning: " << Path << ": '-pie' has no effect for target '"
                       << BCModule.TripleStr << '\'');
  } else {
    if (PIC)
      CodeGen.setCodePICModel(LTO_CODEGEN_PIC_MODEL_DYNAMIC);
    if (PIE)
      CodeGen.setCodePICModel(LTO_CODEGEN_PIC_MODEL_STATIC);
  }

  if (CPU.empty())
    CPU = getDefaultTargetCPU();

  if (!CPU.empty())
    CodeGen.setCpu(CPU.c_str());

  if (!Attrs.empty())
    CodeGen.setAttr(Attrs.c_str());

#if LLVM_VERSION_GE(3, 7)
  if (OptLevel != 2)
    CodeGen.setOptLevel(OptLevel);
#endif

  CodeGen.setDebugInfo(GenerateDebugSymbols ? LTO_DEBUG_MODEL_DWARF
                                            : LTO_DEBUG_MODEL_NONE);

  return true;
}

void NativeCodeGenerator::setOutPutPath() {
  OutPath = OutDir;
  OutPath += PATH_DIV;
  OutPath += getObjFileName();
}
