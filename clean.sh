#!/bin/bash

# Clean script for Prodigy

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo -e "${RED}Cleaning build artifacts...${NC}"

# Remove build directory
if [ -d "build" ]; then
    rm -rf build
    echo "  Removed build/"
fi

# Remove test artifacts
if [ -d "test" ]; then
    cd test
    rm -f *.ll *.o test_element_size_detection_inst element_size_output.log
    echo "  Cleaned test/"
    cd ..
fi

echo -e "${GREEN}Clean complete!${NC}" 