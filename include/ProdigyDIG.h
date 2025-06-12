#ifndef PRODIGY_DIG_H
#define PRODIGY_DIG_H

#include <vector>
#include <string>
#include <cstdint>

namespace prodigy {

// DIG节点 - 表示数据结构
struct DIGNode {
    uint32_t node_id;           // 节点唯一标识符
    uint64_t base_addr;         // 基地址
    uint64_t bound_addr;        // 边界地址
    uint32_t data_size;         // 元素大小(字节)
    bool is_trigger;            // 是否是触发节点
    
    DIGNode(uint32_t id, uint64_t base, uint64_t bound, uint32_t size, bool trigger = false)
        : node_id(id), base_addr(base), bound_addr(bound), data_size(size), is_trigger(trigger) {}
};

// DIG边类型
enum class EdgeType {
    SINGLE_VALUED = 0,  // w0: 单值间接访问 (e.g., A[B[i]])
    RANGED = 1,         // w1: 范围间接访问 (e.g., 访问A[B[i]]到A[B[i+1]])
    TRIGGER = 2         // w2: 触发边
};

// DIG边 - 表示数据结构间的访问模式
struct DIGEdge {
    uint64_t src_base_addr;     // 源数据结构基地址
    uint64_t dest_base_addr;    // 目标数据结构基地址
    EdgeType edge_type;         // 边类型
    uint32_t edge_index;        // 边索引
    
    DIGEdge(uint64_t src, uint64_t dest, EdgeType type, uint32_t index = 0)
        : src_base_addr(src), dest_base_addr(dest), edge_type(type), edge_index(index) {}
};

// Data Indirection Graph
class DIG {
private:
    std::vector<DIGNode> nodes;
    std::vector<DIGEdge> edges;
    
public:
    void addNode(const DIGNode& node) {
        nodes.push_back(node);
    }
    
    void addEdge(const DIGEdge& edge) {
        edges.push_back(edge);
    }
    
    const std::vector<DIGNode>& getNodes() const { return nodes; }
    const std::vector<DIGEdge>& getEdges() const { return edges; }
    
    void clear() {
        nodes.clear();
        edges.clear();
    }
};

} // namespace prodigy

#endif // PRODIGY_DIG_H 