LLVMCONFIG= llvm-config
INSTALLPREFIX ?= /usr/local

override CXXFLAGS= $(shell $(LLVMCONFIG) --cxxflags)

# Make this tool compile with g++
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-Wcovered-switch-default//g')

# Remove "incompatible" options
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-frtti//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-std=c++0z//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-std=c++11//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-march=native//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-O1//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-O2//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-O3//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-Os//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-Oz//g')
override CXXFLAGS:= $(shell echo $(CXXFLAGS) | sed 's/-Og//g')

override CXXFLAGS+= -fno-rtti -std=c++1y -O3 -g

override LDFLAGS= $(shell $(LLVMCONFIG) --ldflags --libs --system-libs)

override VERSION= $(shell $(LLVMCONFIG) --version | sed 's/svn//g')

SRCS=main.cpp cpucount.cpp
OBJS=$(subst .cpp,.o,$(SRCS))

BIN= bc2obj-$(VERSION)
BINLINK= bc2obj

all: bc2obj

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

bc2obj: $(OBJS)
	$(CXX) $(OBJS) -o $(BIN) $(LDFLAGS)
	ln -sf $(BIN) $(BINLINK)

install: all
	mkdir -p $(INSTALLPREFIX)/bin
	cp $(BIN) $(BINLINK) $(INSTALLPREFIX)/bin

.PHONY: clean bc2obj

clean:
	rm -f $(BIN) $(BINLINK) $(OBJS)
