#!/bin/bash

# Build script for Prodigy LLVM Pass

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Building Prodigy LLVM Pass ===${NC}"

# Check if CMake is available
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: CMake is not installed${NC}"
    exit 1
fi

# Use system compiler to avoid libstdc++ version issues
export CC=/usr/bin/gcc
export CXX=/usr/bin/g++

# Create build directory
mkdir -p build
cd build

# Configure
echo -e "${YELLOW}Configuring with CMake...${NC}"
cmake .. || {
    echo -e "${RED}CMake configuration failed${NC}"
    exit 1
}

# Build
echo -e "${YELLOW}Building...${NC}"
make -j$(nproc) || {
    echo -e "${RED}Build failed${NC}"
    exit 1
}

echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}Output files:${NC}"
echo "  - libProdigyPass.so (LLVM pass plugin)"
echo "  - libProdigyRuntime.so (Runtime library)"

# Return to project root
cd .. 