#ifndef PRODIGY_RUNTIME_H
#define PRODIGY_RUNTIME_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Prodigy运行时API - 这些函数会被插入到目标程序中

// 注册DIG节点
// base_addr: 数据结构基地址
// num_elements: 元素数量
// element_size: 每个元素的大小(字节)
// node_id: 节点ID
void registerNode(void* base_addr, uint64_t num_elements, uint32_t element_size, uint32_t node_id);

// 注册遍历边(单值或范围间接访问)
// src_addr: 源数据结构地址
// dest_addr: 目标数据结构地址
// edge_type: 边类型 (0=单值, 1=范围)
void registerTravEdge(void* src_addr, void* dest_addr, uint32_t edge_type);

// 注册触发边
// trigger_addr: 触发数据结构地址
// prefetch_params: 预取参数(编码了预取距离等信息)
void registerTrigEdge(void* trigger_addr, uint32_t prefetch_params);

#ifdef __cplusplus
}
#endif

#endif // PRODIGY_RUNTIME_H 