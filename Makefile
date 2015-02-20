LLVMCONFIG= llvm-config

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

BIN= bc2obj-$(VERSION)

all: bc2obj

bc2obj:
	$(CXX) main.cpp $(CXXFLAGS) -o $(BIN) $(LDFLAGS)

.PHONY: clean bc2obj

clean:
	rm -f $(BIN)
