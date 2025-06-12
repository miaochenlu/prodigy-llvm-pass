#include "../include/ProdigyRuntime.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <unordered_map>

// 简单的运行时实现，用于测试和调试
// 实际实现中，这些信息会被传递给硬件预取器

struct NodeInfo {
    void* base_addr;
    uint64_t num_elements;
    uint32_t element_size;
    uint32_t node_id;
};

struct EdgeInfo {
    void* src_addr;
    void* dest_addr;
    uint32_t edge_type;
};

// 全局存储
static std::vector<NodeInfo> registered_nodes;
static std::vector<EdgeInfo> registered_edges;
static std::unordered_map<void*, uint32_t> addr_to_node_id;

void registerNode(void* base_addr, uint64_t num_elements, uint32_t element_size, uint32_t node_id) {
    NodeInfo node = {base_addr, num_elements, element_size, node_id};
    registered_nodes.push_back(node);
    addr_to_node_id[base_addr] = node_id;
    
    std::cout << "[Prodigy Runtime] Registered Node:\n";
    std::cout << "  Node ID: " << node_id << "\n";
    std::cout << "  Base Address: " << base_addr << "\n";
    std::cout << "  Num Elements: " << num_elements << "\n";
    std::cout << "  Element Size: " << element_size << " bytes\n";
    std::cout << "  Total Size: " << (num_elements * element_size) << " bytes\n";
    std::cout << std::endl;
}

void registerTravEdge(void* src_addr, void* dest_addr, uint32_t edge_type) {
    EdgeInfo edge = {src_addr, dest_addr, edge_type};
    registered_edges.push_back(edge);
    
    const char* edge_type_str = "";
    switch (edge_type) {
        case 0: edge_type_str = "SINGLE_VALUED (w0)"; break;
        case 1: edge_type_str = "RANGED (w1)"; break;
        case 2: edge_type_str = "TRIGGER (w2)"; break;
        default: edge_type_str = "UNKNOWN"; break;
    }
    
    std::cout << "[Prodigy Runtime] Registered Edge:\n";
    std::cout << "  Type: " << edge_type_str << "\n";
    std::cout << "  Source Address: " << src_addr;
    
    // 尝试找到对应的节点ID
    auto src_it = addr_to_node_id.find(src_addr);
    if (src_it != addr_to_node_id.end()) {
        std::cout << " (Node " << src_it->second << ")";
    }
    
    std::cout << "\n  Dest Address: " << dest_addr;
    
    auto dest_it = addr_to_node_id.find(dest_addr);
    if (dest_it != addr_to_node_id.end()) {
        std::cout << " (Node " << dest_it->second << ")";
    }
    
    std::cout << "\n" << std::endl;
}

void registerTrigEdge(void* trigger_addr, uint32_t prefetch_params) {
    std::cout << "[Prodigy Runtime] Registered Trigger Edge:\n";
    std::cout << "  Trigger Address: " << trigger_addr;
    
    auto it = addr_to_node_id.find(trigger_addr);
    if (it != addr_to_node_id.end()) {
        std::cout << " (Node " << it->second << ")";
    }
    
    std::cout << "\n  Prefetch Parameters: 0x" << std::hex << prefetch_params << std::dec;
    std::cout << "\n" << std::endl;
}

// 辅助函数：打印DIG摘要
__attribute__((destructor))
void printDIGSummary() {
    std::cout << "\n[Prodigy Runtime] DIG Summary:\n";
    std::cout << "  Total Nodes: " << registered_nodes.size() << "\n";
    std::cout << "  Total Edges: " << registered_edges.size() << "\n";
    
    // 统计边类型
    int single_valued = 0, ranged = 0, trigger = 0;
    for (const auto& edge : registered_edges) {
        switch (edge.edge_type) {
            case 0: single_valued++; break;
            case 1: ranged++; break;
            case 2: trigger++; break;
        }
    }
    
    std::cout << "  Edge Types:\n";
    std::cout << "    Single-valued: " << single_valued << "\n";
    std::cout << "    Ranged: " << ranged << "\n";
    std::cout << "    Trigger: " << trigger << "\n";
    std::cout << std::endl;
} 