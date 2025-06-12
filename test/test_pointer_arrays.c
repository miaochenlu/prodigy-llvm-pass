#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 测试指针数组和复杂的内存访问模式
void test_pointer_arrays() {
    printf("Testing pointer arrays:\n");
    printf("======================\n\n");
    
    // 1. 指针数组 - 应该推断为8字节元素（64位系统）
    int** ptr_array = (int**)malloc(20 * sizeof(int*));  // 160字节
    printf("1. Pointer array: %p (expecting 20 elements of 8 bytes)\n", ptr_array);
    
    // 为每个指针分配内存
    for (int i = 0; i < 20; i++) {
        ptr_array[i] = (int*)malloc(10 * sizeof(int));
        for (int j = 0; j < 10; j++) {
            ptr_array[i][j] = i * 10 + j;
        }
    }
    
    // 2. 混合类型的结构体数组
    typedef struct {
        int id;
        double value;
        char padding[4];  // 对齐到16字节
    } AlignedStruct;
    
    AlignedStruct* structs = (AlignedStruct*)malloc(8 * sizeof(AlignedStruct));  // 128字节
    printf("2. Aligned struct array: %p (expecting 8 elements of 16 bytes)\n", structs);
    
    // 3. 使用memcpy的数组（应该保持字节数组）
    char* src_buffer = (char*)malloc(100);
    char* dst_buffer = (char*)malloc(100);
    
    // 初始化源缓冲区
    for (int i = 0; i < 100; i++) {
        src_buffer[i] = i % 256;
    }
    
    // 使用memcpy
    memcpy(dst_buffer, src_buffer, 100);
    printf("3. Memcpy buffers: src=%p, dst=%p (should remain byte arrays)\n", src_buffer, dst_buffer);
    
    // 4. 短整型数组
    short* short_array = (short*)malloc(30 * sizeof(short));  // 60字节
    printf("4. Short array: %p (expecting 30 elements of 2 bytes)\n", short_array);
    
    // 5. Long数组（8字节）
    long* long_array = (long*)malloc(15 * sizeof(long));  // 120字节
    printf("5. Long array: %p (expecting 15 elements of 8 bytes)\n", long_array);
    
    // 使用这些数组
    
    // 访问指针数组（两级间接访问）
    int sum = 0;
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 10; j++) {
            sum += ptr_array[i][j];  // 两级间接访问
        }
    }
    
    // 使用结构体数组
    for (int i = 0; i < 8; i++) {
        structs[i].id = i;
        structs[i].value = i * 3.14159;
    }
    
    // 使用短整型数组
    for (int i = 0; i < 30; i++) {
        short_array[i] = i * 2;
    }
    
    // 使用long数组
    for (int i = 0; i < 15; i++) {
        long_array[i] = i * 1000000L;
    }
    
    // 打印结果
    printf("\nResults:\n");
    printf("Pointer array sum: %d\n", sum);
    printf("First struct: id=%d, value=%.2f\n", structs[0].id, structs[0].value);
    printf("First short: %d\n", short_array[0]);
    printf("First long: %ld\n", long_array[0]);
    printf("Buffer comparison: %s\n", 
           memcmp(src_buffer, dst_buffer, 100) == 0 ? "equal" : "different");
    
    // 清理
    for (int i = 0; i < 20; i++) {
        free(ptr_array[i]);
    }
    free(ptr_array);
    free(structs);
    free(src_buffer);
    free(dst_buffer);
    free(short_array);
    free(long_array);
}

// 测试特殊的访问模式
void test_special_patterns() {
    printf("\nTesting special access patterns:\n");
    printf("================================\n\n");
    
    // 1. 斐波那契访问模式（非规则步长）
    int* fib_array = (int*)malloc(100 * sizeof(int));
    int fib_a = 0, fib_b = 1;
    
    // 访问斐波那契索引
    while (fib_b < 100) {
        fib_array[fib_b] = fib_b * fib_b;
        int temp = fib_a + fib_b;
        fib_a = fib_b;
        fib_b = temp;
    }
    
    printf("Fibonacci pattern array: first few values = %d, %d, %d\n",
           fib_array[1], fib_array[2], fib_array[3]);
    
    // 2. 反向访问（从后往前）
    int* reverse_array = (int*)malloc(50 * sizeof(int));
    for (int i = 49; i >= 0; i--) {
        reverse_array[i] = 49 - i;
    }
    
    printf("Reverse access array: last=%d, first=%d\n", 
           reverse_array[49], reverse_array[0]);
    
    free(fib_array);
    free(reverse_array);
}

int main() {
    test_pointer_arrays();
    test_special_patterns();
    return 0;
} 