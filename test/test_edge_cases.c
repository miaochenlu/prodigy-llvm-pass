#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 测试边缘情况和特殊访问模式
void test_edge_cases() {
    printf("Testing edge cases for size inference:\n");
    printf("=====================================\n\n");
    
    // 1. 使用memset的char数组
    char* buffer = (char*)malloc(256);
    memset(buffer, 0, 256);
    printf("1. Buffer with memset: %p (should remain byte array)\n", buffer);
    
    // 2. 结构体数组 - 通过结构体成员访问
    struct Node {
        int id;
        float value;
        int next;
    };
    struct Node* nodes = (struct Node*)malloc(10 * sizeof(struct Node));
    printf("2. Struct array: %p (expecting 10 elements of 12 bytes)\n", nodes);
    
    // 3. 使用非单位步长访问的数组
    int* sparse_array = (int*)malloc(100 * sizeof(int));
    printf("3. Sparse access array: %p (accessed with stride 3)\n", sparse_array);
    
    // 4. 混合访问模式 - 既有直接访问也有间接访问
    int* mixed_access = (int*)malloc(50 * sizeof(int));
    int* indices = (int*)malloc(10 * sizeof(int));
    printf("4. Mixed access pattern: %p\n", mixed_access);
    
    // 5. 小数组但有对齐要求（例如SIMD）
    float* aligned_floats = (float*)malloc(16 * sizeof(float));  // 64字节，适合AVX-512
    printf("5. Aligned float array: %p (16 floats for SIMD)\n", aligned_floats);
    
    // 使用这些分配
    
    // 1. Buffer - 字符串操作
    strcpy(buffer, "Hello, World!");
    strcat(buffer, " Testing edge cases.");
    
    // 2. 结构体数组 - 访问成员
    for (int i = 0; i < 10; i++) {
        nodes[i].id = i;
        nodes[i].value = i * 1.5f;
        nodes[i].next = (i + 1) % 10;
    }
    
    // 3. 稀疏访问（步长为3）
    for (int i = 0; i < 33; i++) {
        sparse_array[i * 3] = i * i;  // 访问索引0, 3, 6, 9...
    }
    
    // 4. 混合访问模式
    // 初始化indices
    for (int i = 0; i < 10; i++) {
        indices[i] = i * 5;  // 0, 5, 10, 15...
    }
    // 直接访问
    for (int i = 0; i < 50; i++) {
        mixed_access[i] = i;
    }
    // 间接访问
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += mixed_access[indices[i]];  // A[B[i]]模式
    }
    
    // 5. SIMD风格的访问（一次处理多个元素）
    for (int i = 0; i < 16; i += 4) {
        // 模拟SIMD加载（一次处理4个float）
        float v0 = aligned_floats[i];
        float v1 = aligned_floats[i+1];
        float v2 = aligned_floats[i+2];
        float v3 = aligned_floats[i+3];
        
        // 模拟SIMD操作
        aligned_floats[i] = v0 * 2.0f;
        aligned_floats[i+1] = v1 * 2.0f;
        aligned_floats[i+2] = v2 * 2.0f;
        aligned_floats[i+3] = v3 * 2.0f;
    }
    
    // 打印一些结果
    printf("\nResults:\n");
    printf("Buffer content: '%s'\n", buffer);
    printf("First node: id=%d, value=%.2f, next=%d\n", 
           nodes[0].id, nodes[0].value, nodes[0].next);
    printf("Sparse array[0]: %d, [3]: %d, [6]: %d\n", 
           sparse_array[0], sparse_array[3], sparse_array[6]);
    printf("Indirect access sum: %d\n", sum);
    printf("First aligned float: %.2f\n", aligned_floats[0]);
    
    // 清理
    free(buffer);
    free(nodes);
    free(sparse_array);
    free(mixed_access);
    free(indices);
    free(aligned_floats);
}

// 测试循环展开的情况
void test_loop_unrolling() {
    printf("\nTesting loop unrolling patterns:\n");
    printf("================================\n\n");
    
    int* array = (int*)malloc(32 * sizeof(int));
    
    // 手动展开的循环（编译器也可能生成类似代码）
    for (int i = 0; i < 32; i += 4) {
        array[i] = i;
        array[i+1] = i+1;
        array[i+2] = i+2;
        array[i+3] = i+3;
    }
    
    printf("Loop unrolled array: sum = %d\n", array[0] + array[31]);
    
    free(array);
}

int main() {
    test_edge_cases();
    test_loop_unrolling();
    return 0;
} 