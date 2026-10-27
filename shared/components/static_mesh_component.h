#ifndef STATIC_MESH_COMPONENT_H
#define STATIC_MESH_COMPONENT_H
#include <joy/joy.h>

// 静态碰撞体/装饰物的渲染信息（服务器本地渲染，不发给客户端）
struct StaticMeshComponent {
    render_mesh_p mesh = nullptr;      // 渲染用 mesh（cube/sphere/ramp）
    render_mesh_p wire_mesh = nullptr; // 描边用线框网格（"填充+描边"画风）
    jcolor_t color = { 255, 255, 255, 255 };       // 填充色
    jcolor_t stroke_color = { 30, 30, 35, 255 };   // 描边色
    float roll = 0.0f;                 // 绕 Z 轴旋转角（弧度），斜放长方体斜坡用；绕 Y 用 TransformComponent::yaw
};

#endif
