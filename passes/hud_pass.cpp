#include "hud_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "../layers/debug_layer.h"
#include "../layers/gameplay_controls_layer.h"
#include "../layers/pause_menu_layer.h"

void hud_pass(Context* ctx) {
	if (!ctx->r3d) return;

	// 消费战斗音效请求（逻辑层写入，每帧清空；纯客户端本地表现）
	for (auto& s : ctx->sfx_requests) {
		audio_shot_p shot = NULL;
		switch (s.type) {
		case SFX_MELEE:  shot = ctx->sfx_melee;  break;
		case SFX_BULLET: shot = ctx->sfx_bullet; break;
		case SFX_HIT:    shot = ctx->sfx_hit;    break;
		case SFX_KILL:   shot = ctx->sfx_kill;   break;
		case SFX_WAVE:   shot = ctx->sfx_wave;   break;
		}
		if (shot) audio_shot_play(shot);
	}
	ctx->sfx_requests.clear();

	// 调试层 HUD
	static bool g_quad_tried = false;
	static render_mesh_p g_hud_quad = NULL;
	if (!g_quad_tried) {
		g_quad_tried = true;
		g_hud_quad = r3d_mesh_quad(ctx->r3d);
	}
	debug_layer_render_hud(ctx, g_hud_quad);

	// 虚拟操作层（方向键/攻击键）叠加在最上层
	gameplay_controls_layer_render(g_gameplay_controls);

	// 暂停/设置面板（右上角 || 按钮 + 菜单/设置/确认）画在最上层
	if (g_pause_menu) pause_menu_layer_render(g_pause_menu);
}