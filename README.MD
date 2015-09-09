### bc2obj: LLVM Bitcode To Native Object Converter ###
-------------------------------------------------------

#### REQUIREMENTS: ####

`llvm-3.5+`, `clang++ or g++`, `make`

#### USAGE: ####

`./bc2obj 1.o 2.o 3.o 4.a [...]`

#### SUPPORTED OPTIONS ####

    -out-dir                          : specify an output directory (default: native/)
    -generate-debug-symbols           : generate debug symbols
    -disable-inline-pass              : disable the inline pass
    -disable-gvn-pass                 : disable the gvn pass
    -target=<val>                     : override the module target triple
    -llvm <opts>                      : set llvm options (i.e. -llvm -inline-threshold=1000)
    -pic                              : generate position independent code (shared libraries)
    -pie                              : generate position independent code (executables)
    -cpu=<val>                        : cpu to generate code for
    -attrs=<val>                      : codegen attributes (+sse,+sse2,+mmx,...)
    -ar=<val>                         : archiver to use (default: llvm-ar)
    -j<val>                           : use <val> jobs
    
    SOME OPTIONS ARE VERSION SPECIFIC:

    LLVM <= 3.6:

    -disable-optimizations            : disable optimizations

    LLVM >= 3.6:

    -disable-vectorization-pass       : disable the vectorization pass
    
    LLVM >= 3.7:
    
    -O<val>                           : optimization level (default: 2)


#### SUPPORTED TARGETS ####

This tool supports all targets that are supported by your LLVM installation.
