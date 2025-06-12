#!/bin/bash

echo "=== Testing Prodigy Element Size Detection ==="

# Compile the test program to LLVM IR
echo "Compiling test_element_size_detection.c to LLVM IR..."
clang -O0 -emit-llvm -S test_element_size_detection.c -o test_element_size_detection.ll

# Apply the Prodigy pass
echo "Applying Prodigy pass..."
opt -enable-new-pm=0 -load ../build/libProdigyPass.so -prodigy test_element_size_detection.ll -S -o test_element_size_detection_inst.ll 2>&1 | tee element_size_output.log

# Count detected allocations and their element sizes
echo ""
echo "=== Summary of detected allocations ==="
echo "Total allocations found:"
grep "Found allocation:" element_size_output.log | wc -l

echo ""
echo "=== Element size detection results ==="
grep -A 2 "Found allocation:" element_size_output.log | grep -E "(Element size:|Pattern:|SCEV:|Inferred|Calloc:|Using default:)" | sort | uniq -c | sort -nr

echo ""
echo "=== Detailed allocation analysis ==="
# Show each allocation with its inferred element size
grep -A 3 "Found allocation:" element_size_output.log

echo ""
echo "=== Indirection patterns detected ==="
grep "Found single-valued indirection pattern:" element_size_output.log | wc -l

echo ""
echo "=== Node registrations ==="
grep "Inserted registerNode call" element_size_output.log | grep "element_size="

# Compile to executable (optional)
echo ""
echo "Compiling instrumented code..."
clang test_element_size_detection_inst.ll ../build/libProdigyRuntime.so -o test_element_size_detection_inst -lm

# Run the instrumented program
echo ""
echo "Running instrumented program..."
LD_LIBRARY_PATH=../build:$LD_LIBRARY_PATH ./test_element_size_detection_inst 2>&1 | grep -E "(Registering node:|element_size=)" 