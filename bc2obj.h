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
#include <vector>
#include <queue>
#include <unistd.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Program.h>
#include <llvm/LTO/LTOModule.h>
#include <llvm/LTO/LTOCodeGenerator.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Object/Archive.h>
#include "llvm-compat.h"
#include "cpucount.h"

using namespace llvm;

extern cl::opt<bool> GenerateDebugSymbols;
extern cl::opt<bool> DisableOptimizations;
extern cl::opt<bool> DisableInlinePass;
extern cl::opt<bool> DisableGVNPass;
extern cl::opt<bool> DisableVectorizationPass;
extern cl::list<std::string> LLVMOpts;
extern cl::opt<std::string> Target;
extern cl::opt<bool> PIC;
extern cl::opt<bool> PIE;
extern cl::opt<std::string> CPU;
extern cl::opt<std::string> Attrs;
extern cl::list<std::string> BitCodeFiles;
extern cl::opt<std::string> OutDir;
extern cl::opt<int> NumJobs;

// Misc

#ifdef _WIN32
#define ONUNIX(b) do { } while (0)
#else
#define ONUNIX(b) do { b; } while (0)
#endif

const char *getFileName(const char *Path);

// Jobs

#ifdef WIN32
#define childExit(c) do { OK = !c; } while (0)
#else
#define childExit(c) do { _exit(c); } while (0)
#endif

extern int ActiveJobs;
pid_t forkProcess(bool wait = true, bool *OK = nullptr);
int waitForChild(const pid_t pid);
bool waitForJob();
bool waitForJobs();

// Classes

class BitCodeArchive {
public:
  BitCodeArchive(const std::string &Path, bool &OK);
  ~BitCodeArchive();

  const object::Archive &getArchive() { return *Archive; }
  static std::string getObjName(const llvm::object::Archive::child_iterator &child);

private:
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf;
  object::Archive *Archive;
};

class BitCodeModule {
  friend class NativeCodeGenerator;

public:
  BitCodeModule(const std::string &Path, bool &OK);
  BitCodeModule(const std::string &Path, StringRef Data, bool &OK);
  ~BitCodeModule();

private:
  void check(const std::string &errMsg, const std::string &Path, bool &OK);

  bool isNativeObjectFile;
  TargetOptions TargetOpts;
  LTOModule *Module;
};

class NativeCodeGenerator {
public:
  NativeCodeGenerator(const std::string &Path, bool &OK,
                      bool &isNativeObjectFile);

  NativeCodeGenerator(const std::string &Path, StringRef Data, bool &OK,
                      bool &isNativeObjectFile);

  const char *getObjFileName() const { return getFileName(Path.c_str()); }

  bool generateNativeCode();
  bool generateNativeCodeMemory();

  bool writeCodeToDisk(const std::string &Dir);

  struct Code;
  const Code &getCode() { return code; }

  const char *getOutputPath() { return OutPath.c_str(); }

  struct Code {
    const void *Code;
    size_t Length;
  };

private:
  const char *getDefaultTargetCPU();
  bool setupCodeGenOpts();
  void setOutPutPath();

  std::string Path;
  std::string OutPath;
  BitCodeModule BCModule;
  LTOCodeGenerator CodeGen;
  StringRef Data;
  Code code;
};
