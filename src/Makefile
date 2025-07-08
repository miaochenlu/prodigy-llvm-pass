# Makefile for building Prodigy LLVM Pass

LLVM_CONFIG := llvm-config
CXX := $(shell $(LLVM_CONFIG) --bindir)/clang++
CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags) -fPIC -Wall -Wextra -g
LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags) -shared

# Include directories
INCLUDES = -I../include -I.

# Source and object files
SOURCES := ProdigyPass.cpp ProdigyRuntime.cpp IndirectionDetector.cpp ElementSizeInference.cpp BasePointerTracker.cpp DIGInsertion.cpp
OBJECTS := $(SOURCES:.cpp=.o)
TARGET := ProdigyPass.so

# Default target
all: $(TARGET)

# Build shared library
$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^

# Compile source files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Phony targets
.PHONY: all clean

ProdigyPass.o: ProdigyPass.cpp ProdigyPass.h ProdigyTypes.h IndirectionDetector.h ElementSizeInference.h BasePointerTracker.h AllocInfo.h DIGInsertion.h
ProdigyRuntime.o: ProdigyRuntime.cpp 
IndirectionDetector.o: IndirectionDetector.cpp IndirectionDetector.h ProdigyTypes.h AllocInfo.h
ElementSizeInference.o: ElementSizeInference.cpp ElementSizeInference.h AllocInfo.h
BasePointerTracker.o: BasePointerTracker.cpp BasePointerTracker.h AllocInfo.h
DIGInsertion.o: DIGInsertion.cpp DIGInsertion.h AllocInfo.h BasePointerTracker.h ProdigyTypes.h

test: $(TARGET)
	opt -load-pass-plugin=./$(TARGET) -passes=prodigy -S test.ll -o test_opt.ll 