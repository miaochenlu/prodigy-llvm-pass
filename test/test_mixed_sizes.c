#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 测试混合大小的内存分配
void test_various_allocations() {
    printf("Testing various allocation patterns:\n");
    printf("===================================\n\n");
    
    // 1. 明确的int数组 - 应该推断为4字节元素
    int* int_array = (int*)malloc(100 * sizeof(int));  // 400字节
    printf("1. int_array: %p (expecting 100 elements of 4 bytes)\n", int_array);
    
    // 2. 明确的char数组 - 应该推断为1字节元素
    char* char_array = (char*)malloc(50 * sizeof(char));  // 50字节
    printf("2. char_array: %p (expecting 50 elements of 1 byte)\n", char_array);
    
    // 3. 明确的double数组 - 应该推断为8字节元素
    double* double_array = (double*)malloc(10 * sizeof(double));  // 80字节
    printf("3. double_array: %p (expecting 10 elements of 8 bytes)\n", double_array);
    
    // 4. 模糊的大小 - 120字节可以是30个int或15个double
    void* ambiguous1 = malloc(120);  
    printf("4. ambiguous1: %p (120 bytes - could be 30 ints or 15 doubles)\n", ambiguous1);
    
    // 5. 奇数大小 - 只能是字节数组
    void* odd_size = malloc(77);  
    printf("5. odd_size: %p (77 bytes - should be byte array)\n", odd_size);
    
    // 6. 小分配 - 可能是单个元素
    void* small_alloc = malloc(4);  
    printf("6. small_alloc: %p (4 bytes - could be 1 int or 4 chars)\n", small_alloc);
    
    // 7. 结构体数组的大小（假设结构体大小为12字节）
    struct Point { int x, y, z; };
    struct Point* points = (struct Point*)malloc(5 * sizeof(struct Point));  // 60字节
    printf("7. points: %p (expecting 5 elements of 12 bytes)\n", points);
    
    // 使用这些分配的内存来确保推断逻辑能看到使用模式
    
    // 使用int数组
    for (int i = 0; i < 100; i++) {
        int_array[i] = i;
    }
    
    // 使用char数组
    strcpy(char_array, "Hello, World!");
    
    // 使用double数组
    for (int i = 0; i < 10; i++) {
        double_array[i] = i * 3.14;
    }
    
    // 使用ambiguous1作为int数组
    int* int_view = (int*)ambiguous1;
    for (int i = 0; i < 30; i++) {
        int_view[i] = i * 2;
    }
    
    // 使用odd_size作为字节数组
    unsigned char* byte_view = (unsigned char*)odd_size;
    for (int i = 0; i < 77; i++) {
        byte_view[i] = i % 256;
    }
    
    // 使用small_alloc作为int
    int* single_int = (int*)small_alloc;
    *single_int = 42;
    
    // 使用结构体数组
    for (int i = 0; i < 5; i++) {
        points[i].x = i;
        points[i].y = i * 2;
        points[i].z = i * 3;
    }
    
    // 打印一些值来防止优化器移除代码
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += int_array[i];
    }
    printf("\nSum of first 10 ints: %d\n", sum);
    printf("First char: '%c'\n", char_array[0]);
    printf("First double: %.2f\n", double_array[0]);
    printf("Single int value: %d\n", *single_int);
    
    // 清理
    free(int_array);
    free(char_array);
    free(double_array);
    free(ambiguous1);
    free(odd_size);
    free(small_alloc);
    free(points);
}

// 测试间接访问模式
void test_indirect_patterns() {
    printf("\nTesting indirect access patterns:\n");
    printf("=================================\n\n");
    
    // 创建一个索引数组和一个数据数组
    int* indices = (int*)malloc(20 * sizeof(int));
    int* data = (int*)malloc(50 * sizeof(int));
    
    // 初始化
    for (int i = 0; i < 20; i++) {
        indices[i] = i * 2;  // 0, 2, 4, 6, ...
    }
    for (int i = 0; i < 50; i++) {
        data[i] = i * i;
    }
    
    // 执行间接访问 A[B[i]]
    int sum = 0;
    for (int i = 0; i < 20; i++) {
        int idx = indices[i];
        if (idx < 50) {
            sum += data[idx];  // 间接访问模式
        }
    }
    
    printf("Indirect access sum: %d\n", sum);
    
    free(indices);
    free(data);
}

int main() {
    test_various_allocations();
    test_indirect_patterns();
    return 0;
} 