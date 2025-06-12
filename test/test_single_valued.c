#include <stdio.h>
#include <stdlib.h>

// 简单的单值间接访问示例
// 模式: result = A[B[i]]
void test_single_valued_indirection() {
    int n = 100;
    
    // 分配数组
    int *B = (int*)malloc(n * sizeof(int));
    int *A = (int*)malloc(n * sizeof(int));
    
    // 初始化数据
    for (int i = 0; i < n; i++) {
        B[i] = (i * 7) % n;  // 随机索引
        A[i] = i * i;        // 数据值
    }
    
    // 单值间接访问模式: A[B[i]]
    int sum = 0;
    for (int i = 0; i < n; i++) {
        int index = B[i];     // 从B数组读取索引
        int value = A[index]; // 使用该索引访问A数组
        sum += value;
    }
    
    printf("Sum: %d\n", sum);
    
    // 释放内存
    free(A);
    free(B);
}

// 更复杂的例子：多级间接访问
void test_multi_level_indirection() {
    int n = 50;
    
    int *C = (int*)malloc(n * sizeof(int));
    int *B = (int*)malloc(n * sizeof(int));
    int *A = (int*)malloc(n * sizeof(int));
    
    // 初始化
    for (int i = 0; i < n; i++) {
        C[i] = (i * 3) % n;
        B[i] = (i * 5) % n;
        A[i] = i;
    }
    
    // 两级间接访问: A[B[C[i]]]
    int result = 0;
    for (int i = 0; i < n; i++) {
        int idx1 = C[i];        // 第一级索引
        int idx2 = B[idx1];     // 第二级索引
        result += A[idx2];      // 最终访问
    }
    
    printf("Multi-level result: %d\n", result);
    
    free(A);
    free(B);
    free(C);
}

int main() {
    printf("Testing Single-Valued Indirection Patterns\n");
    printf("==========================================\n\n");
    
    test_single_valued_indirection();
    test_multi_level_indirection();
    
    return 0;
} 