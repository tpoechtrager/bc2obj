### bc2obj: LLVM Bitcode To Native Object Converter ###
-------------------------------------------------------

#### REQUIREMENTS: ####

`llvm-3.5+`, `clang or g++`, `make`

#### USAGE: ####

`./bc2obj 1.o 2.o 3.o [...]`

#### SUPPORTED OPTIONS ####

    -out-dir                          : specifiy an output directory (native/ by default)
    -generate-debug-symbols           : generate debug symbols
    -disable-optimizations            : disable optimizations
    -disable-inline-pass              : disable the inline pass
    -disable-gvn-pass                 : disable the gvn pass
    -disable-vectorization-pass       : disable the vectorization pass
    -pic                              : generate position independent code (shared libraries)
    -pie                              : generate position independent code (executables)

#### SUPPORTED TARGETS ####

This tool supports all targets that are supported by your LLVM installation.
