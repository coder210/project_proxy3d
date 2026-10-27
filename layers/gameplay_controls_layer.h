#ifndef GAMEPLAY_CONTROLS_LAYER_3D_H
#define GAMEPLAY_CONTROLS_LAYER_3D_H
#include <joy/joy.h>
#include "../context.h"

// 虚拟操作层（仿 project_proxy2d 的 gameplay_controls_layer）：
// 左下方向键(W/S/A/D) + 右下攻击键，鼠标/触屏点击映射到 current_input_mask / attack_triggered
struct gameplay_controls_layer;
typedef struct gameplay_controls_layer* gameplay_controls_layer_p;

// 当前场景的活动操作层（game_scene 创建/销毁，hud_pass 绘制）
extern gameplay_controls_layer_p g_gameplay_controls;

gameplay_controls_layer_p gameplay_controls_layer_create(Context* ctx);
void gameplay_controls_layer_handle_event(gameplay_controls_layer_p self, const void* ev);
void gameplay_controls_layer_render(gameplay_controls_layer_p self);
void gameplay_controls_layer_destroy(gameplay_controls_layer_p self);

#endif
