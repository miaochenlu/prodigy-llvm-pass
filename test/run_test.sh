 #!/bin/bash

# 测试脚本 - Phase 2: Single-valued Indirection

echo "=== Prodigy LLVM Pass Test Script ==="
echo "Phase 2: Testing Single-valued Indirection Detection"
echo

# 设置路径
BUILD_DIR="../build"
PASS_LIB="$BUILD_DIR/libProdigyPass.so"
RUNTIME_LIB="$BUILD_DIR/libProdigyRuntime.so"

# 检查构建目录
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found. Please build the project first."
    echo "Run: mkdir build && cd build && cmake .. && make"
    exit 1
fi

# 检查Pass库
if [ ! -f "$PASS_LIB" ]; then
    echo "Error: ProdigyPass.so not found in $BUILD_DIR"
    exit 1
fi

# 1. 编译测试程序到LLVM IR
echo "Step 1: Compiling test program to LLVM IR..."
clang -O0 -emit-llvm -S test_single_valued.c -o test_single_valued.ll
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile test program"
    exit 1
fi

echo "Generated LLVM IR:"
echo "-------------------"
head -20 test_single_valued.ll
echo "..."
echo

# 2. 运行Prodigy Pass
echo "Step 2: Running Prodigy Pass..."
opt -load-pass-plugin="$PASS_LIB" -passes="prodigy" test_single_valued.ll -S -o test_single_valued_instrumented.ll
if [ $? -ne 0 ]; then
    echo "Error: Failed to run Prodigy Pass"
    exit 1
fi

echo "Pass output saved to: test_single_valued_instrumented.ll"
echo

# 3. 检查插入的调用
echo "Step 3: Checking inserted runtime calls..."
echo "registerNode calls:"
grep -n "call.*registerNode" test_single_valued_instrumented.ll | head -5
echo
echo "registerTravEdge calls:"
grep -n "call.*registerTravEdge" test_single_valued_instrumented.ll | head -5
echo

# 4. 编译并链接运行时库
echo "Step 4: Compiling instrumented code with runtime library..."
clang test_single_valued_instrumented.ll -L"$BUILD_DIR" -lProdigyRuntime -Wl,-rpath,"$BUILD_DIR" -o test_single_valued_instrumented
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile instrumented code"
    exit 1
fi

# 5. 运行测试
echo "Step 5: Running instrumented program..."
echo "======================================="
./test_single_valued_instrumented
echo "======================================="

echo
echo "Test completed successfully!"