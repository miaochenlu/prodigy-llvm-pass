# Makefile for building Prodigy LLVM Pass

LLVM_CONFIG := llvm-config
CXX := $(shell $(LLVM_CONFIG) --bindir)/clang++
# Filter out unsupported flags for LLVM 3.4.2
CXXFLAGS_RAW := $(shell $(LLVM_CONFIG) --cxxflags)
CXXFLAGS := $(filter-out -fstack-protector-strong,$(CXXFLAGS_RAW)) -fPIC -Wall -Wextra -g
LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags) -shared

# Build directory
BUILD_DIR := ../build

# Include directories
INCLUDES = -I../include -I.

# Source and object files
SOURCES := ProdigyPass.cpp ProdigyRuntime.cpp IndirectionDetector.cpp ElementSizeInference.cpp BasePointerTracker.cpp DIGInsertion.cpp
OBJECTS := $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)
TARGET := $(BUILD_DIR)/ProdigyPass.so

# Default target
all: $(BUILD_DIR) $(TARGET)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build shared library
$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^

# Compile source files
$(BUILD_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# Phony targets
.PHONY: all clean

$(BUILD_DIR)/ProdigyPass.o: ProdigyPass.cpp ProdigyPass.h ProdigyTypes.h IndirectionDetector.h ElementSizeInference.h BasePointerTracker.h AllocInfo.h DIGInsertion.h
$(BUILD_DIR)/ProdigyRuntime.o: ProdigyRuntime.cpp 
$(BUILD_DIR)/IndirectionDetector.o: IndirectionDetector.cpp IndirectionDetector.h ProdigyTypes.h AllocInfo.h
$(BUILD_DIR)/ElementSizeInference.o: ElementSizeInference.cpp ElementSizeInference.h AllocInfo.h
$(BUILD_DIR)/BasePointerTracker.o: BasePointerTracker.cpp BasePointerTracker.h AllocInfo.h
$(BUILD_DIR)/DIGInsertion.o: DIGInsertion.cpp DIGInsertion.h AllocInfo.h BasePointerTracker.h ProdigyTypes.h

test: $(TARGET)
	opt -load $(TARGET) -prodigy -S test.ll -o test_opt.ll 