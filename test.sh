#!/bin/bash

# Quick test script for Prodigy

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== Prodigy Test Runner ===${NC}"

# Build first
echo -e "${YELLOW}Building Prodigy...${NC}"
./build.sh || exit 1

# Run tests
echo -e "\n${YELLOW}Running element size detection test...${NC}"
cd test
./run_element_size_test.sh

echo -e "\n${GREEN}Testing complete!${NC}" 