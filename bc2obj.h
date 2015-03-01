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
extern cl::opt<bool> PIC;
extern cl::opt<bool> PIE;
extern cl::opt<std::string> CPU;
extern cl::opt<std::string> Attrs;
extern cl::list<std::string> BitCodeFiles;
extern cl::opt<std::string> OutDir;
extern cl::opt<int> NumJobs;

class BitCodeArchive {
public:
  BitCodeArchive(const std::string &Path, bool &OK);
  ~BitCodeArchive();

  const object::Archive &getArchive() { return *Archive; }

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
