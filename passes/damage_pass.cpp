#include "damage_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "components/global_state_component.h"
#include "components/damage_text_component.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// 残影飘字：仅客户端渲染，消费 ctx->damage_texts（玩家命中请求），
// 用世界空间 billboard 文字（始终面向相机）向上漂浮并渐隐。不参与逻辑判定。
void damage_pass(Context* ctx, float dt) {
	if (!ctx->r3d) return;

	static bool g_quad_tried = false;
	static render_mesh_p g_quad = NULL;
	if (!g_quad_tried) {
		g_quad_tried = true;
		g_quad = r3d_mesh_quad(ctx->r3d);
	}
	if (!g_quad) return;

	static constexpr float DUR_NORMAL = 0.75f;
	static constexpr float DUR_KILL   = 1.05f;

	// 1) 消费逻辑层命中请求，创建飘字实体
	for (auto& q : ctx->damage_texts) {
		DamageTextComponent fx;
		fx.x = fp_to_float(q.x);
		fx.z = fp_to_float(q.z);
		fx.kill = q.kill;
		fx.amount = q.amount;
		fx.base_lifetime = q.kill ? DUR_KILL : DUR_NORMAL;
		fx.lifetime = fx.base_lifetime;
		auto e = ctx->ecs.create();
		ctx->ecs.emplace<DamageTextComponent>(e, fx);
	}
	ctx->damage_texts.clear();

	// 2) 生命周期递减，到点销毁
	{
		std::vector<entt::entity> to_kill;
		auto v = ctx->ecs.view<DamageTextComponent>();
		for (auto e : v) {
			auto& d = v.get<DamageTextComponent>(e);
			d.lifetime -= dt;
			if (d.lifetime <= 0.0f) to_kill.push_back(e);
		}
		for (auto e : to_kill) ctx->ecs.destroy(e);
	}

	// 3) 渲染：世界坐标投影到屏幕，固定像素字号向上漂浮 + 渐隐（不受相机距离/角度影响）
	auto v = ctx->ecs.view<DamageTextComponent>();
	for (auto e : v) {
		auto& d = v.get<DamageTextComponent>(e);
		float p = d.base_lifetime > 0.0f ? 1.0f - d.lifetime / d.base_lifetime : 1.0f;
		if (p < 0.0f) p = 0.0f;
		if (p > 1.0f) p = 1.0f;
		float fade = 1.0f - p;                 // 渐隐
		float rise = 1.4f + 1.2f * p;          // 从敌人头部向上飘

		// 世界坐标投影到屏幕像素（固定字号，任何相机角度/距离下清晰可读）
		vec3_t world = { d.x, rise, d.z };
		float sx, sy;
		if (!r3d_world_to_screen(ctx->r3d, world, &sx, &sy)) continue;
		int winW, winH;
		r3d_get_window_size(ctx->r3d, &winW, &winH);

		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", d.amount);
		float fontSize = d.kill ? 26.0f : 18.0f;   // 击杀爆字更大
		// 渐隐改为字号随生命周期缩小（100%→15%）淡出，而不是改变颜色：
		// 字号在绘制时缩放、不参与字号纹理缓存键，改颜色才会触发每帧重新烘焙 GPU 纹理（造成命中卡顿）。
		fontSize *= (1.0f - p) * 0.85f + 0.15f;
		if (fontSize < 1.0f) fontSize = 1.0f;
		// 数字宽度估算（ASCII 半宽）用于居中
		float textW = (float)strlen(buf) * fontSize * 0.55f;
		float px = sx - textW * 0.5f;
		float py = sy - fontSize * 0.5f;

		// 颜色保持固定（只区分击杀/非击杀），避免每帧因颜色变化重新烘焙纹理
		Uint8 cr = 255;
		Uint8 cg = d.kill ? 80  : 205;         // 击杀红/橙，普通亮黄
		Uint8 cb = d.kill ? 50  : 60;
		r3d_font_draw_2d_color(ctx->r3d, g_quad, buf, px, py, fontSize, (float)winW, (float)winH, cr, cg, cb);
	}
}