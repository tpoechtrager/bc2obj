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

// BitCodeModule -> Public

BitCodeModule::BitCodeModule(const std::string &Path, bool &OK)
    : isNativeObjectFile(false) {
  std::string errMsg;
  Module = LTOModule::createFromFile(Path.c_str(), TargetOpts, errMsg);
  check(errMsg, Path, OK);
}

BitCodeModule::BitCodeModule(const std::string &Path, StringRef Data, bool &OK)
    : isNativeObjectFile(false) {
  std::string errMsg;
  Module =
      LTOModule::createFromBuffer(Data.data(), Data.size(), TargetOpts, errMsg);
  check(errMsg, Path, OK);
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
    std::cerr << Path << ":" << errMsg << std::endl;
    return false;
  }

  if (sys::fs::rename(name, OutPath)) {
    std::cerr << "cannot rename " << name << " to " << OutPath << std::endl;
    return false;
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

  code.Code =
      CodeGen.compile(&code.Length, DisableOptimizations, DisableInlinePass,
                      DisableGVNPass, DisableVectorizationPass, errMsg);

  if (!code.Code) {
    std::cerr << Path << ":" << errMsg << std::endl;
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
    std::cerr << Path << ": cannot open file for writing" << std::endl;
    return false;
  }

  bool OK = write(fd, code.Code, code.Length) == (ssize_t)code.Length;

  close(fd);
  return OK;
}

// NativeCodeGenerator -> Private

bool NativeCodeGenerator::setupCodeGenOpts() {
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

  if (CPU.empty())
    CodeGen.setCpu(CPU.c_str());

  if (!Attrs.empty())
    CodeGen.setAttr(Attrs.c_str());

  CodeGen.setDebugInfo(GenerateDebugSymbols ? LTO_DEBUG_MODEL_DWARF
                                            : LTO_DEBUG_MODEL_NONE);

  return true;
}

void NativeCodeGenerator::setOutPutPath() {
  OutPath = OutDir;
  OutPath += PATH_DIV;
  OutPath += getObjFileName();
}
