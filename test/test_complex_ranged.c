#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 测试1: 嵌套的ranged indirection - 多级CSR结构
void test_nested_ranged() {
    printf("\nTest 1: Nested Ranged Indirection\n");
    printf("==================================\n");
    
    // 模拟一个层次化的图结构
    int num_groups = 3;
    int num_vertices = 8;
    int num_edges = 16;
    
    // 第一级：组到顶点的映射
    int *group_offset = (int*)malloc((num_groups + 1) * sizeof(int));
    int *group_vertices = (int*)malloc(num_vertices * sizeof(int));
    
    // 第二级：顶点到边的映射（标准CSR）
    int *vertex_offset = (int*)malloc((num_vertices + 1) * sizeof(int));
    int *edges = (int*)malloc(num_edges * sizeof(int));
    
    // 初始化组结构
    group_offset[0] = 0; group_offset[1] = 3; group_offset[2] = 5; group_offset[3] = 8;
    group_vertices[0] = 0; group_vertices[1] = 1; group_vertices[2] = 2;  // Group 0
    group_vertices[3] = 3; group_vertices[4] = 4;                         // Group 1
    group_vertices[5] = 5; group_vertices[6] = 6; group_vertices[7] = 7;  // Group 2
    
    // 初始化顶点偏移
    vertex_offset[0] = 0;  vertex_offset[1] = 2;  vertex_offset[2] = 4;
    vertex_offset[3] = 6;  vertex_offset[4] = 8;  vertex_offset[5] = 10;
    vertex_offset[6] = 12; vertex_offset[7] = 14; vertex_offset[8] = 16;
    
    // 初始化边
    for (int i = 0; i < num_edges; i++) {
        edges[i] = (i * 3) % num_vertices;
    }
    
    // 嵌套的ranged indirection访问
    for (int g = 0; g < num_groups; g++) {
        printf("Group %d vertices: ", g);
        
        // 第一级ranged indirection
        int group_start = group_offset[g];
        int group_end = group_offset[g + 1];
        
        for (int v_idx = group_start; v_idx < group_end; v_idx++) {
            int vertex = group_vertices[v_idx];
            printf("v%d(", vertex);
            
            // 第二级ranged indirection
            int edge_start = vertex_offset[vertex];
            int edge_end = vertex_offset[vertex + 1];
            
            for (int e_idx = edge_start; e_idx < edge_end; e_idx++) {
                printf("%d", edges[e_idx]);
                if (e_idx < edge_end - 1) printf(",");
            }
            printf(") ");
        }
        printf("\n");
    }
    
    free(group_offset);
    free(group_vertices);
    free(vertex_offset);
    free(edges);
}

// 测试2: 混合single-valued和ranged indirection
void test_mixed_indirection() {
    printf("\nTest 2: Mixed Single-valued and Ranged Indirection\n");
    printf("===================================================\n");
    
    int num_items = 6;
    int num_properties = 10;
    
    // 数据结构
    int *item_property_offset = (int*)malloc((num_items + 1) * sizeof(int));
    int *property_indices = (int*)malloc(num_properties * sizeof(int));
    double *property_values = (double*)malloc(num_properties * sizeof(double));
    
    // 额外的single-valued indirection
    int *item_types = (int*)malloc(num_items * sizeof(int));
    double *type_weights = (double*)malloc(5 * sizeof(double));
    
    // 初始化
    item_property_offset[0] = 0;
    item_property_offset[1] = 2;
    item_property_offset[2] = 3;
    item_property_offset[3] = 6;
    item_property_offset[4] = 7;
    item_property_offset[5] = 9;
    item_property_offset[6] = 10;
    
    for (int i = 0; i < num_properties; i++) {
        property_indices[i] = i;
        property_values[i] = 1.0 + i * 0.5;
    }
    
    for (int i = 0; i < num_items; i++) {
        item_types[i] = i % 5;
    }
    
    for (int i = 0; i < 5; i++) {
        type_weights[i] = 1.0 + i * 0.2;
    }
    
    // 混合访问模式
    for (int item = 0; item < num_items; item++) {
        // Single-valued indirection: 获取类型权重
        int type = item_types[item];
        double weight = type_weights[type];
        
        printf("Item %d (type %d, weight %.1f): ", item, type, weight);
        
        // Ranged indirection: 访问属性
        int prop_start = item_property_offset[item];
        int prop_end = item_property_offset[item + 1];
        
        double sum = 0.0;
        for (int p = prop_start; p < prop_end; p++) {
            int prop_idx = property_indices[p];
            double value = property_values[prop_idx];
            sum += value * weight;  // 组合使用两种indirection的结果
        }
        
        printf("weighted sum = %.2f\n", sum);
    }
    
    free(item_property_offset);
    free(property_indices);
    free(property_values);
    free(item_types);
    free(type_weights);
}

// 测试3: 动态大小的ranged indirection
void test_dynamic_ranged() {
    printf("\nTest 3: Dynamic Ranged Indirection\n");
    printf("===================================\n");
    
    // 动态确定大小
    int n = 5;
    int total_elements = 0;
    
    // 动态生成偏移数组
    int *dynamic_offset = (int*)malloc((n + 1) * sizeof(int));
    dynamic_offset[0] = 0;
    
    for (int i = 1; i <= n; i++) {
        int range_size = (i * 7) % 5 + 1;  // 变化的范围大小
        dynamic_offset[i] = dynamic_offset[i-1] + range_size;
        total_elements = dynamic_offset[i];
    }
    
    // 根据总大小分配数据数组
    double *data = (double*)malloc(total_elements * sizeof(double));
    
    // 初始化数据
    for (int i = 0; i < total_elements; i++) {
        data[i] = 1.0 + i * 0.1;
    }
    
    // Ranged indirection访问
    for (int i = 0; i < n; i++) {
        int start = dynamic_offset[i];
        int end = dynamic_offset[i + 1];
        int range_size = end - start;
        
        printf("Range %d [%d:%d] (size %d): ", i, start, end, range_size);
        
        double avg = 0.0;
        for (int j = start; j < end; j++) {
            avg += data[j];
        }
        avg /= range_size;
        
        printf("average = %.2f\n", avg);
    }
    
    free(dynamic_offset);
    free(data);
}

// 测试4: 条件ranged indirection
void test_conditional_ranged() {
    printf("\nTest 4: Conditional Ranged Indirection\n");
    printf("======================================\n");
    
    int num_nodes = 6;
    int num_edges = 12;
    
    // 标准CSR结构
    int *offset = (int*)malloc((num_nodes + 1) * sizeof(int));
    int *edges = (int*)malloc(num_edges * sizeof(int));
    int *edge_weights = (int*)malloc(num_edges * sizeof(int));
    int *node_flags = (int*)malloc(num_nodes * sizeof(int));
    
    // 初始化
    offset[0] = 0; offset[1] = 2; offset[2] = 4;
    offset[3] = 7; offset[4] = 9; offset[5] = 11;
    offset[6] = 12;
    
    for (int i = 0; i < num_edges; i++) {
        edges[i] = (i * 2) % num_nodes;
        edge_weights[i] = 1 + (i % 5);
    }
    
    // 某些节点有特殊标记
    node_flags[0] = 1; node_flags[1] = 0; node_flags[2] = 1;
    node_flags[3] = 0; node_flags[4] = 1; node_flags[5] = 0;
    
    // 条件性的ranged indirection
    for (int v = 0; v < num_nodes; v++) {
        if (node_flags[v]) {  // 只处理标记的节点
            printf("Processing node %d: ", v);
            
            int start = offset[v];
            int end = offset[v + 1];
            
            int weight_sum = 0;
            for (int e = start; e < end; e++) {
                int neighbor = edges[e];
                int weight = edge_weights[e];
                
                // 进一步的条件检查
                if (weight > 2) {
                    weight_sum += weight;
                    printf("(%d,w%d) ", neighbor, weight);
                }
            }
            
            printf("total weight = %d\n", weight_sum);
        }
    }
    
    free(offset);
    free(edges);
    free(edge_weights);
    free(node_flags);
}

// 测试5: 多个数组之间的ranged indirection链
void test_chained_ranged() {
    printf("\nTest 5: Chained Ranged Indirection\n");
    printf("===================================\n");
    
    // 三级链式结构
    int n1 = 3, n2 = 6, n3 = 10;
    
    // 第一级
    int *level1_offset = (int*)malloc((n1 + 1) * sizeof(int));
    int *level1_data = (int*)malloc(n2 * sizeof(int));
    
    // 第二级
    int *level2_offset = (int*)malloc((n2 + 1) * sizeof(int));
    int *level2_data = (int*)malloc(n3 * sizeof(int));
    
    // 初始化第一级
    level1_offset[0] = 0; level1_offset[1] = 2;
    level1_offset[2] = 4; level1_offset[3] = 6;
    
    level1_data[0] = 0; level1_data[1] = 1;
    level1_data[2] = 2; level1_data[3] = 3;
    level1_data[4] = 4; level1_data[5] = 5;
    
    // 初始化第二级
    level2_offset[0] = 0; level2_offset[1] = 1; level2_offset[2] = 3;
    level2_offset[3] = 5; level2_offset[4] = 7; level2_offset[5] = 9;
    level2_offset[6] = 10;
    
    for (int i = 0; i < n3; i++) {
        level2_data[i] = i * 10;
    }
    
    // 链式ranged indirection
    for (int i = 0; i < n1; i++) {
        printf("Level 1 [%d]: ", i);
        
        // 第一级ranged indirection
        int l1_start = level1_offset[i];
        int l1_end = level1_offset[i + 1];
        
        for (int j = l1_start; j < l1_end; j++) {
            int l2_idx = level1_data[j];
            printf("\n  Level 2 [%d]: ", l2_idx);
            
            // 第二级ranged indirection
            int l2_start = level2_offset[l2_idx];
            int l2_end = level2_offset[l2_idx + 1];
            
            for (int k = l2_start; k < l2_end; k++) {
                printf("%d ", level2_data[k]);
            }
        }
        printf("\n");
    }
    
    free(level1_offset);
    free(level1_data);
    free(level2_offset);
    free(level2_data);
}

int main() {
    printf("Testing Complex Ranged Indirection Patterns\n");
    printf("==========================================\n");
    
    test_nested_ranged();
    test_mixed_indirection();
    test_dynamic_ranged();
    test_conditional_ranged();
    test_chained_ranged();
    
    return 0;
} 