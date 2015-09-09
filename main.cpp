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

cl::opt<bool> GenerateDebugSymbols("generate-debug-symbols",
                                   cl::desc("generate debug symbols"),
                                   cl::init(false));

#if LLVM_VERSION_LT(3, 7)
cl::opt<bool> DisableOptimizations("disable-optimizations",
                                   cl::desc("disable optimizations"),
                                   cl::init(false));
#endif

cl::opt<bool> DisableInlinePass("disable-inline-pass",
                                cl::desc("disable inline pass"),
                                cl::init(false));

cl::opt<bool> DisableGVNPass("disable-gvn-pass", cl::desc("disable gvn pass"),
                             cl::init(false));

#if LLVM_VERSION_GE(3, 6)
cl::opt<bool> DisableVectorizationPass("disable-vectorization-pass",
                                       cl::desc("disable vectorization pass"),
                                       cl::init(false));
#endif

#if LLVM_VERSION_GE(3, 7)
cl::opt<unsigned> OptLevel("O", cl::desc("optimization level"), cl::init(2),
                           cl::Prefix);
#endif

cl::list<std::string> LLVMOpts(cl::CommaSeparated, "llvm",
                               cl::desc("llvm options"));

cl::opt<std::string> Target("target",
                            cl::desc("override the module target triple"));

cl::opt<bool> PIC("pic", cl::desc("generate position independent code"),
                  cl::init(false));

cl::opt<bool> PIE("pie", cl::desc("generate position independent code"),
                  cl::init(false));

cl::opt<std::string> CPU("cpu", cl::desc("cpu to generate code for"));

cl::opt<std::string> Attrs("attrs", cl::desc("codegen attrs (+sse, ...)"));

cl::opt<std::string> AR("ar", cl::desc("archiver to use (default: llvm-ar)"),
                        cl::init("llvm-ar"));

cl::list<std::string> BitCodeFiles(cl::Sink, cl::OneOrMore);

cl::opt<std::string> OutDir("out-dir", cl::desc("output directory"),
                            cl::init("native"));

cl::opt<int> NumJobs("j", cl::desc("jobs"), cl::init(getCPUCount()),
                     cl::Prefix);

namespace {

bool isArchive(const char *Path) {
  // FIXME: check header
  const char *Ext = std::strrchr(Path, '.');
  if (!Ext)
    return false;
  return !std::strcmp(Ext, ".a");
}

bool createArchive(const char *ArchiveName,
                   const std::vector<std::string> &Files) {
  bool OK;

  if (!forkProcess(true, &OK)) {
    std::string OutputFile = OutDir;
    OutputFile += PATH_DIV;
    OutputFile += ArchiveName;

    msg("generating archive: " << OutputFile);
    std::string Program = sys::FindProgramByName(AR);

    if (Program.empty()) {
      errmsg("unable to find '" << AR << "' in PATH");
      OK = false;
    } else {
      std::vector<const char *> Args;

      Args.push_back(Program.c_str());
      Args.push_back("rcs");
      Args.push_back(OutputFile.c_str());

      for (auto &File : Files)
        Args.push_back(File.c_str());

      Args.push_back(nullptr);

      std::string errMsg;
      bool ExecutionFailed = false;
      OK = sys::ExecuteAndWait(Program.c_str(), Args.data(), nullptr, nullptr,
                               0, 0, &errMsg, &ExecutionFailed) == 0;

      if (ExecutionFailed)
        OK = false;
      else if (!OK)
        errmsg(errMsg);
    }

    childExit(!OK);
  }

  return OK;
}

bool createNativeArchive(const std::string &File) {
  bool OK;
  bool isNativeObjectFile;
  BitCodeArchive BCAr(File, OK);
  const object::Archive &Archive = BCAr.getArchive();

  if (!OK)
    return false;

  SmallVector<char, 32> tmp;

  if (sys::fs::createUniqueDirectory("", tmp)) {
    errmsg("cannot create temporary directory");
    return false;
  }

  std::vector<std::string> Files;
  std::string Path;
  std::string ObjName;

  for (auto Obj = Archive.child_begin(); Obj != Archive.child_end(); ++Obj) {
    auto Buf = Obj->getBuffer();
    ObjName = BitCodeArchive::getObjName(Obj);
    
#if LLVM_VERSION_GE(3, 7)
    OK = !Buf.getError();
    llvm::StringRef StrBuf = *Buf;
#else
    llvm::StringRef &StrBuf = Buf;
#endif

    Path = &tmp[0];
    Path += PATH_DIV;
    Path += ObjName;

    OK = waitForJob();

    if (!OK)
      break;

    msg("codegen'ing " << File << "(" << ObjName << ") to " << Path);

    if (!forkProcess(false)) {
      NativeCodeGenerator NCodeGen(ObjName, StrBuf, OK, isNativeObjectFile);

      if (!OK)
        return false;

      bool OK = NCodeGen.generateNativeCodeMemory() &&
                NCodeGen.writeCodeToDisk(&tmp[0]);
      ONUNIX(NCodeGen.~NativeCodeGenerator());

      childExit(!OK);
    }

    ActiveJobs++;
    Files.push_back(std::move(Path));
  }

  bool V = waitForJobs();

  if (OK)
    OK = V;

  if (OK)
    OK = createArchive(getFileName(File.c_str()), Files);

  Files.push_back(&tmp[0]);

  for (auto &File : Files) {
    if (sys::fs::remove(File.c_str())) {
      errmsg(File << ": cannot remove file");
      OK = false;
    }
  }

  return OK;
}

} // end unnamed namespace

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv,
                              "bitcode to native object file converter\n");
  if (BitCodeFiles.empty()) {
    errmsg("no bitcode files specified");
    return 1;
  }

  for (auto &BCFile : BitCodeFiles) {
    if (!BCFile.empty() && BCFile[0] == '-') {
      errmsg("unknown option: " << BCFile);
      return 1;
    }
  }

  if (sys::fs::create_directory(OutDir)) {
    errmsg("cannot create directory " << OutDir);
    return 1;
  }

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  if (NumJobs <= 0)
    NumJobs = 1;

  ONUNIX(errmsg("using " << NumJobs << " job" << (NumJobs != 1 ? "s" : "")));

  for (auto &BitCodeFile : BitCodeFiles) {
    bool isFile;

    if (sys::fs::is_regular_file(BitCodeFile, isFile) || !isFile) {
      errmsg(BitCodeFile << ": is not a file");
      return 1;
    }

    if (isArchive(BitCodeFile.c_str())) {
      if (!createNativeArchive(BitCodeFile))
        return 1;

      continue;
    }

    if (!waitForJob())
      return 1;

    if (!forkProcess(false)) {
      bool OK;
      bool isNativeObjectFile;

      NativeCodeGenerator NCodeGen(BitCodeFile, OK, isNativeObjectFile);

      if (OK) {
        msg("codegen'ing " << BitCodeFile << " to "
                           << NCodeGen.getOutputPath());

        if (!NCodeGen.generateNativeCode()) {
          errmsg("cannot codegen " << BitCodeFile);
          return 1;
        }
      }

      ONUNIX(NCodeGen.~NativeCodeGenerator());
      childExit(!OK);
    }

    ActiveJobs++;
  }

  return !waitForJobs();
}
