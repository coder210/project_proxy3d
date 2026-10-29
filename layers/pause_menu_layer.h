#ifndef PAUSE_MENU_LAYER_3D_H
#define PAUSE_MENU_LAYER_3D_H
#include <joy/joy.h>
#include "../context.h"

// 暂停/设置面板（仿 joy_topdown_template 的 PauseMenuLayer）：
// 右上角 || 暂停按钮 -> 菜单(继续/设置/返回主菜单/退出) -> 设置页(音乐/音效滑块 + 调试信息开关)。
// 本项目用 r3d（无 jr2d），故用 r3d 原生按钮/滑块/复选框手绘，风格与 gameplay_controls_layer / settings_scene 一致。
struct pause_menu_layer;
typedef struct pause_menu_layer* pause_menu_layer_p;

// 当前场景的活动暂停面板（game_scene 创建/销毁，hud_pass 绘制）
extern pause_menu_layer_p g_pause_menu;

pause_menu_layer_p pause_menu_layer_create(Context* ctx);
// 返回 true 表示事件已被面板消费（菜单打开时应吞掉，避免透传给游戏）
bool pause_menu_layer_handle_event(pause_menu_layer_p self, const void* ev);
// 在 hud_pass 的 r3d_begin/r3d_end 内调用，画在最上层
void pause_menu_layer_render(pause_menu_layer_p self);
void pause_menu_layer_destroy(pause_menu_layer_p self);

#endif
