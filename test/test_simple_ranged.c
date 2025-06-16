#include <stdio.h>
#include <stdlib.h>

// 简化的ranged indirection测试
// 模式: 访问edges[offset[v]:offset[v+1]]

void simple_ranged_test() {
    // 简单的CSR结构
    int num_vertices = 4;
    int num_edges = 8;
    
    // 分配数组
    int *offset = (int*)malloc((num_vertices + 1) * sizeof(int));
    int *edges = (int*)malloc(num_edges * sizeof(int));
    
    // 初始化offset数组
    offset[0] = 0;
    offset[1] = 2;
    offset[2] = 5;
    offset[3] = 6;
    offset[4] = 8;
    
    // 初始化edges数组
    edges[0] = 1; edges[1] = 2;
    edges[2] = 0; edges[3] = 2; edges[4] = 3;
    edges[5] = 3;
    edges[6] = 0; edges[7] = 1;
    
    // 核心的ranged indirection模式
    for (int v = 0; v < num_vertices; v++) {
        // 读取范围边界
        int start = offset[v];
        int end = offset[v + 1];
        
        printf("Vertex %d: ", v);
        
        // 使用范围访问edges数组
        for (int j = start; j < end; j++) {
            int neighbor = edges[j];
            printf("%d ", neighbor);
        }
        printf("\n");
    }
    
    free(offset);
    free(edges);
}

int main() {
    printf("Simple Ranged Indirection Test\n");
    printf("==============================\n");
    simple_ranged_test();
    return 0;
} 