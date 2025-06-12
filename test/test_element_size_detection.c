#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// Test 1: malloc with multiplication pattern (n * sizeof(type))
void test_malloc_multiplication() {
    printf("\n=== Test 1: malloc(n * sizeof(type)) patterns ===\n");
    
    // Pattern: count * 4 (int array)
    int n = 100;
    int *int_array = (int*)malloc(n * sizeof(int));
    
    // Pattern: count * 8 (double array)
    double *double_array = (double*)malloc(n * sizeof(double));
    
    // Pattern: count * 2 (short array)
    short *short_array = (short*)malloc(n * sizeof(short));
    
    // Pattern: count * 1 (char array)
    char *char_array = (char*)malloc(n * sizeof(char));
    
    // Pattern: count * 8 (pointer array)
    void **ptr_array = (void**)malloc(n * sizeof(void*));
    
    // Initialize and use arrays to generate access patterns
    for (int i = 0; i < n; i++) {
        int_array[i] = i;
        double_array[i] = i * 1.0;
        short_array[i] = (short)i;
        char_array[i] = (char)i;
        ptr_array[i] = NULL;
    }
    
    free(int_array);
    free(double_array);
    free(short_array);
    free(char_array);
    free(ptr_array);
}

// Test 2: calloc patterns (direct element size)
void test_calloc_patterns() {
    printf("\n=== Test 2: calloc(count, size) patterns ===\n");
    
    // calloc directly provides element count and size
    int *int_array = (int*)calloc(200, sizeof(int));
    double *double_array = (double*)calloc(150, sizeof(double));
    
    // Use the arrays
    for (int i = 0; i < 200; i++) {
        int_array[i] = i * 2;
    }
    
    for (int i = 0; i < 150; i++) {
        double_array[i] = i * 3.14;
    }
    
    free(int_array);
    free(double_array);
}

// Test 3: Constant size allocations with usage patterns
void test_constant_size_with_usage() {
    printf("\n=== Test 3: Constant size with usage patterns ===\n");
    
    // Allocate 400 bytes - should infer int array from usage
    int *data1 = (int*)malloc(400);
    for (int i = 0; i < 100; i++) {
        data1[i] = i * i;  // int access pattern
    }
    
    // Allocate 800 bytes - should infer double array from usage
    double *data2 = (double*)malloc(800);
    for (int i = 0; i < 100; i++) {
        data2[i] = i * 0.5;  // double access pattern
    }
    
    // Allocate 200 bytes - should infer short array from usage
    short *data3 = (short*)malloc(200);
    for (int i = 0; i < 100; i++) {
        data3[i] = (short)(i * 2);  // short access pattern
    }
    
    free(data1);
    free(data2);
    free(data3);
}

// Test 4: Shift patterns (count << shift)
void test_shift_patterns() {
    printf("\n=== Test 4: Shift patterns ===\n");
    
    int count = 128;
    
    // count << 2 = count * 4 (int array)
    int *int_array = (int*)malloc(count << 2);
    
    // count << 3 = count * 8 (double/pointer array)
    double *double_array = (double*)malloc(count << 3);
    
    // Use arrays with stride patterns
    for (int i = 0; i < count; i++) {
        int_array[i] = i;
        double_array[i] = i * 2.0;
    }
    
    free(int_array);
    free(double_array);
}

// Test 5: Indirect patterns (A[B[i]])
void test_indirect_patterns() {
    printf("\n=== Test 5: Indirect access patterns ===\n");
    
    int n = 50;
    
    // Index array
    int *indices = (int*)malloc(n * sizeof(int));
    
    // Data arrays of different types
    float *float_data = (float*)malloc(n * sizeof(float));
    int64_t *long_data = (int64_t*)malloc(n * sizeof(int64_t));
    
    // Initialize
    for (int i = 0; i < n; i++) {
        indices[i] = (n - 1) - i;  // Reverse order
        float_data[i] = i * 1.5f;
        long_data[i] = i * 1000;
    }
    
    // Indirect access patterns
    float sum_float = 0.0f;
    int64_t sum_long = 0;
    
    for (int i = 0; i < n; i++) {
        sum_float += float_data[indices[i]];  // A[B[i]] pattern with float
        sum_long += long_data[indices[i]];    // A[B[i]] pattern with int64_t
    }
    
    printf("Float sum: %f, Long sum: %ld\n", sum_float, sum_long);
    
    free(indices);
    free(float_data);
    free(long_data);
}

// Test 6: Struct arrays
struct Point {
    float x;
    float y;
    float z;
}; // 12 bytes

void test_struct_arrays() {
    printf("\n=== Test 6: Struct array patterns ===\n");
    
    int n = 100;
    
    // Allocate array of structs
    struct Point *points = (struct Point*)malloc(n * sizeof(struct Point));
    
    // Access pattern that shows struct size
    for (int i = 0; i < n; i++) {
        points[i].x = i * 1.0f;
        points[i].y = i * 2.0f;
        points[i].z = i * 3.0f;
    }
    
    // Also test with constant size that's multiple of struct size
    struct Point *points2 = (struct Point*)malloc(1200); // 100 * 12 bytes
    
    for (int i = 0; i < 100; i++) {
        points2[i].x = i * 0.5f;
        points2[i].y = i * 0.5f;
        points2[i].z = i * 0.5f;
    }
    
    free(points);
    free(points2);
}

// Test 7: Loop patterns with different strides
void test_loop_stride_patterns() {
    printf("\n=== Test 7: Loop stride patterns ===\n");
    
    // Allocate as byte array but access with stride
    char *byte_data = (char*)malloc(400);
    
    // Access every 4 bytes (treating as int array)
    for (int i = 0; i < 100; i++) {
        *((int*)(byte_data + i * 4)) = i;
    }
    
    // Another pattern: allocate and access with explicit stride
    void *generic_data = malloc(800);
    
    // Access as double array (8-byte stride)
    for (int i = 0; i < 100; i++) {
        *((double*)((char*)generic_data + i * 8)) = i * 3.14159;
    }
    
    free(byte_data);
    free(generic_data);
}

// Test 8: Mixed access patterns
void test_mixed_patterns() {
    printf("\n=== Test 8: Mixed access patterns ===\n");
    
    // Allocate memory that will be used for different types
    void *buffer = malloc(1024);
    
    // First half as int array
    int *int_part = (int*)buffer;
    for (int i = 0; i < 128; i++) {
        int_part[i] = i;
    }
    
    // Second half as double array
    double *double_part = (double*)((char*)buffer + 512);
    for (int i = 0; i < 64; i++) {
        double_part[i] = i * 2.718;
    }
    
    free(buffer);
}

// Test 9: Dynamic patterns with variables
void test_dynamic_patterns(int count) {
    printf("\n=== Test 9: Dynamic patterns ===\n");
    
    // Pattern with variable
    int *dynamic_array = (int*)malloc(count * sizeof(int));
    
    // Pattern with computation
    double *computed_array = (double*)malloc((count * 2) * sizeof(double));
    
    // Use arrays
    for (int i = 0; i < count; i++) {
        dynamic_array[i] = i;
    }
    
    for (int i = 0; i < count * 2; i++) {
        computed_array[i] = i * 0.5;
    }
    
    free(dynamic_array);
    free(computed_array);
}

// Test 10: Multi-level indirection
void test_multilevel_indirection() {
    printf("\n=== Test 10: Multi-level indirection ===\n");
    
    int n = 30;
    
    // Create chain: A[B[C[i]]]
    int *index_level1 = (int*)malloc(n * sizeof(int));
    int *index_level2 = (int*)malloc(n * sizeof(int));
    double *data = (double*)malloc(n * sizeof(double));
    
    // Initialize
    for (int i = 0; i < n; i++) {
        index_level1[i] = (i * 7) % n;
        index_level2[i] = (i * 13) % n;
        data[i] = i * 100.0;
    }
    
    // Multi-level indirection
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += data[index_level2[index_level1[i]]];
    }
    
    printf("Multi-level sum: %f\n", sum);
    
    free(index_level1);
    free(index_level2);
    free(data);
}

int main() {
    printf("=== Element Size Detection Test Suite ===\n");
    
    test_malloc_multiplication();
    test_calloc_patterns();
    test_constant_size_with_usage();
    test_shift_patterns();
    test_indirect_patterns();
    test_struct_arrays();
    test_loop_stride_patterns();
    test_mixed_patterns();
    test_dynamic_patterns(64);
    test_multilevel_indirection();
    
    printf("\n=== All tests completed ===\n");
    return 0;
} 