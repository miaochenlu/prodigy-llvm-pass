#include <stdio.h>
#include <stdlib.h>

// 模拟BFS-like访问模式
// work_queue -> offset_list -> edge_list -> visited_list

void test_bfs_pattern() {
    int N = 10;
    
    // 模拟work queue (应该有trigger edge，因为没有入边)
    int* work_queue = (int*)malloc(N * sizeof(int));
    
    // 模拟offset list
    int* offset_list = (int*)malloc(N * sizeof(int));
    
    // 模拟edge list
    int* edge_list = (int*)malloc(N * 2 * sizeof(int));
    
    // 模拟visited list
    int* visited_list = (int*)malloc(N * sizeof(int));
    
    // 初始化数据
    for (int i = 0; i < N; i++) {
        work_queue[i] = i;
        offset_list[i] = i * 2;
        visited_list[i] = 0;
    }
    
    for (int i = 0; i < N * 2; i++) {
        edge_list[i] = i % N;
    }
    
    // 模拟BFS遍历模式
    // work_queue -> offset_list -> edge_list -> visited_list
    int sum = 0;
    for (int i = 0; i < N; i++) {
        int vertex = work_queue[i];           // 从work queue读取
        int offset = offset_list[vertex];     // single-valued indirection
        int neighbor = edge_list[offset];     // single-valued indirection
        sum += visited_list[neighbor];        // single-valued indirection
    }
    
    printf("BFS pattern sum: %d\n", sum);
    
    free(work_queue);
    free(offset_list);
    free(edge_list);
    free(visited_list);
}

void test_isolated_arrays() {
    // 创建一些孤立的数组（都应该有trigger edge）
    int* array1 = (int*)malloc(20 * sizeof(int));
    int* array2 = (int*)malloc(20 * sizeof(int));
    
    // 初始化
    for (int i = 0; i < 20; i++) {
        array1[i] = i;
        array2[i] = i * 2;
    }
    
    // 简单使用，没有间接访问
    int sum = 0;
    for (int i = 0; i < 20; i++) {
        sum += array1[i] + array2[i];
    }
    
    printf("Isolated arrays sum: %d\n", sum);
    
    free(array1);
    free(array2);
}

int main() {
    printf("Testing Trigger Edge Detection\n");
    printf("==============================\n\n");
    
    test_bfs_pattern();
    test_isolated_arrays();
    
    return 0;
} 