LLVMCONFIG= llvm-config
INSTALLPREFIX ?= /usr/local
WIN32CROSS ?=
LN= ln -sf

ifeq ($(WIN32CROSS), 1)
	CXX= i686-w64-mingw32-g++
	LDFLAGS+= -static-libgcc -static-libstdc++
	LLVMCONFIG= /usr/i686-w64-mingw32/bin/i686-w64-mingw32-llvm-config-host
	EXESUFFIX=.exe
	LN= cp -r
endif

ifeq ($(WIN64CROSS), 1)
    CXX= x86_64-w64-mingw32-g++
    LDFLAGS+= -static-libgcc -static-libstdc++
    LLVMCONFIG= /usr/x86_64-w64-mingw32/bin/x86_64-w64-mingw32-llvm-config-host
    EXESUFFIX=.exe
    LN= cp -r
endif

override CXXFLAGS+= $(shell $(LLVMCONFIG) --cxxflags)

# Make this tool compile with g++
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-Wcovered-switch-default//g')

# Remove "incompatible" options
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-frtti//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-fPIC//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-std=c++0z//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-std=c++11//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-march=native//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-O1//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-O2//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-O3//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-Os//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-Oz//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-Og//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-g//g')
override CXXFLAGS+= -fno-rtti -std=c++1y -O3 #-O0 -g

override LDFLAGS+= $(shell $(LLVMCONFIG) --ldflags --libs --system-libs)

override VERSION= $(shell $(LLVMCONFIG) --version | sed 's/svn//g')

SRCS= main.cpp bc2obj.cpp cpucount.cpp
OBJS= $(subst .cpp,.o,$(SRCS))

BIN= bc2obj-$(VERSION)$(EXESUFFIX)
BINLINK= bc2obj$(EXESUFFIX)

all: bc2obj

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

bc2obj: $(OBJS)
	$(CXX) $(OBJS) -o $(BIN) $(LDFLAGS)
	$(LN) $(BIN) $(BINLINK)

install: all
	mkdir -p $(INSTALLPREFIX)/bin
	cp $(BIN) $(BINLINK) $(INSTALLPREFIX)/bin

.PHONY: clean bc2obj

clean:
	rm -f $(BIN) $(BINLINK) $(OBJS)
