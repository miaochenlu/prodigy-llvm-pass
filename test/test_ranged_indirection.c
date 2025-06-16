#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CSR格式的图结构示例
// 演示ranged indirection: edges[offset[v]:offset[v+1]]

// 简单的CSR格式测试
void test_csr_format() {
    printf("Testing CSR Format (Ranged Indirection)\n");
    printf("=======================================\n");
    
    // 创建一个小图: 5个顶点
    // 0 -> 1, 2
    // 1 -> 0, 2, 3
    // 2 -> 3
    // 3 -> 0, 4
    // 4 -> 1, 3
    
    int num_vertices = 5;
    int num_edges = 10;
    
    // CSR格式的offset数组
    int *offset = (int*)malloc((num_vertices + 1) * sizeof(int));
    offset[0] = 0;   // 顶点0的边从索引0开始
    offset[1] = 2;   // 顶点1的边从索引2开始
    offset[2] = 5;   // 顶点2的边从索引5开始
    offset[3] = 6;   // 顶点3的边从索引6开始
    offset[4] = 8;   // 顶点4的边从索引8开始
    offset[5] = 10;  // 结束位置
    
    // edges数组存储实际的邻居
    int *edges = (int*)malloc(num_edges * sizeof(int));
    edges[0] = 1; edges[1] = 2;        // 顶点0的邻居
    edges[2] = 0; edges[3] = 2; edges[4] = 3;  // 顶点1的邻居
    edges[5] = 3;                      // 顶点2的邻居
    edges[6] = 0; edges[7] = 4;        // 顶点3的邻居
    edges[8] = 1; edges[9] = 3;        // 顶点4的邻居
    
    // 遍历每个顶点的邻居 - 这是ranged indirection模式
    for (int v = 0; v < num_vertices; v++) {
        printf("Vertex %d neighbors: ", v);
        
        // Ranged indirection: 访问edges[offset[v]:offset[v+1]]
        int start = offset[v];      // 起始索引
        int end = offset[v + 1];    // 结束索引
        
        for (int j = start; j < end; j++) {
            int neighbor = edges[j];
            printf("%d ", neighbor);
        }
        printf("\n");
    }
    
    free(offset);
    free(edges);
}

// BFS算法使用CSR格式 - 更复杂的ranged indirection示例
void test_bfs_with_csr() {
    printf("\nTesting BFS with CSR Format\n");
    printf("===========================\n");
    
    // 创建一个稍大的图用于BFS
    int num_vertices = 8;
    int num_edges = 18;
    
    // 分配CSR数据结构
    int *offset = (int*)malloc((num_vertices + 1) * sizeof(int));
    int *edges = (int*)malloc(num_edges * sizeof(int));
    int *visited = (int*)calloc(num_vertices, sizeof(int));
    int *queue = (int*)malloc(num_vertices * sizeof(int));
    
    // 构建图结构
    offset[0] = 0;  offset[1] = 2;  offset[2] = 5;  offset[3] = 7;
    offset[4] = 10; offset[5] = 12; offset[6] = 15; offset[7] = 17;
    offset[8] = 18;
    
    // 邻接表
    int edge_data[] = {1, 2, 0, 3, 4, 0, 5, 1, 4, 6, 1, 2, 2, 6, 7, 3, 5, 5};
    memcpy(edges, edge_data, num_edges * sizeof(int));
    
    // BFS从顶点0开始
    int start_vertex = 0;
    int front = 0, rear = 0;
    
    queue[rear++] = start_vertex;
    visited[start_vertex] = 1;
    
    printf("BFS traversal starting from vertex %d:\n", start_vertex);
    
    while (front < rear) {
        int current = queue[front++];
        printf("Visiting vertex %d\n", current);
        
        // Ranged indirection: 访问current的所有邻居
        int neighbor_start = offset[current];
        int neighbor_end = offset[current + 1];
        
        for (int i = neighbor_start; i < neighbor_end; i++) {
            int neighbor = edges[i];
            if (!visited[neighbor]) {
                visited[neighbor] = 1;
                queue[rear++] = neighbor;
                printf("  Adding neighbor %d to queue\n", neighbor);
            }
        }
    }
    
    free(offset);
    free(edges);
    free(visited);
    free(queue);
}

// 稀疏矩阵乘法 - 另一个ranged indirection的例子
void test_sparse_matrix_multiply() {
    printf("\nTesting Sparse Matrix Multiplication\n");
    printf("====================================\n");
    
    // 创建一个3x3的稀疏矩阵
    int num_rows = 3;
    int num_nonzeros = 5;
    
    // CSR格式的稀疏矩阵
    int *row_ptr = (int*)malloc((num_rows + 1) * sizeof(int));
    int *col_idx = (int*)malloc(num_nonzeros * sizeof(int));
    double *values = (double*)malloc(num_nonzeros * sizeof(double));
    
    // 矩阵:
    // [1.0, 0.0, 2.0]
    // [0.0, 3.0, 0.0]
    // [4.0, 0.0, 5.0]
    
    row_ptr[0] = 0; row_ptr[1] = 2; row_ptr[2] = 3; row_ptr[3] = 5;
    
    col_idx[0] = 0; col_idx[1] = 2;  // 第0行的非零元素在列0和列2
    col_idx[2] = 1;                  // 第1行的非零元素在列1
    col_idx[3] = 0; col_idx[4] = 2;  // 第2行的非零元素在列0和列2
    
    values[0] = 1.0; values[1] = 2.0;
    values[2] = 3.0;
    values[3] = 4.0; values[4] = 5.0;
    
    // 向量
    double x[] = {1.0, 2.0, 3.0};
    double y[3] = {0.0, 0.0, 0.0};
    
    // 稀疏矩阵向量乘法 y = A * x
    for (int row = 0; row < num_rows; row++) {
        double sum = 0.0;
        
        // Ranged indirection: 访问第row行的所有非零元素
        for (int idx = row_ptr[row]; idx < row_ptr[row + 1]; idx++) {
            int col = col_idx[idx];
            double val = values[idx];
            sum += val * x[col];
        }
        
        y[row] = sum;
        printf("y[%d] = %.1f\n", row, y[row]);
    }
    
    free(row_ptr);
    free(col_idx);
    free(values);
}

int main() {
    printf("Testing Ranged Indirection Patterns\n");
    printf("===================================\n\n");
    
    test_csr_format();
    test_bfs_with_csr();
    test_sparse_matrix_multiply();
    
    return 0;
} 