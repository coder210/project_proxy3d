#ifndef COLLIDER_COMPONENT_H
#define COLLIDER_COMPONENT_H
#include <joy/jmath.h>

// 最大顶点数（OBB 箱子 8、斜放长方体 8、三角棱柱 6）
#define COLLIDER_MAX_VERTS 8

struct ColliderComponent {
    enum ShapeType : int { BOX = 0, SPHERE = 1, CAPSULE = 2, OBB = 3, MESH = 4 };
    ShapeType type = BOX;
    vec3f_t half_size = { fp_from_float(0.3f), fp_from_float(0.6f), fp_from_float(0.3f) };
    vec3f_t rotation = { fp_zero(), fp_zero(), fp_zero() };  // OBB 欧拉角(绕 X/Y/Z 弧度)
    fp_t radius = fp_from_float(0.3f);
    fp_t height = fp_from_float(1.2f);
    // MESH 类型：内嵌真实世界顶点（与渲染几何一致），gjk3df 借用指针不负责释放
    vec3f_t verts[COLLIDER_MAX_VERTS];
    int verts_num = 0;
};

#endif
