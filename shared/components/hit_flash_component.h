#ifndef HIT_FLASH_COMPONENT_H
#define HIT_FLASH_COMPONENT_H

// 受击闪烁（纯客户端渲染表现，不参与帧同步）：
// 实体受击时由逻辑 system（仅非服务端）挂上，渲染 pass 递减并在闪烁期间把填充色提亮。
struct HitFlashComponent {
    float timer = 0.0f;   // 剩余闪烁时间（秒）
};

#endif
