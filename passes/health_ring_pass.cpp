#include "health_ring_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "components/transform_component.h"
#include "components/health_component.h"
#include "components/enemy_component.h"
#include "fog_pass.h"

void health_ring_pass(Context* ctx) {
	if (!ctx->r3d) return;

	// 脚下光环：平铺在角色脚下（XZ 平面），角色站在环中间。
	// 顶点 y=0，通过 model 矩阵统一抬升到实际 y≈0.08/0.12（与最初绿色版本一致）。
	static constexpr float RING_OUT = 0.85f;
	static constexpr float RING_IN  = 0.55f;
	static constexpr int   RING_SEGS = 32;
	static constexpr float RING_Y_BG = 0.08f;   // 背景环实际高度
	static constexpr float RING_Y_FG = 0.12f;   // 前景环实际高度

	auto make_ring = [&](float a0, float span) -> render_mesh_p {
		float verts[RING_SEGS * 6 * 5];
		int vi = 0;
		for (int i = 0; i < RING_SEGS; i++) {
			float a = a0 + (float)i / RING_SEGS * span;
			float b = a0 + (float)(i + 1) / RING_SEGS * span;
			float x0i = sinf(a) * RING_IN,  z0i = cosf(a) * RING_IN;
			float x0o = sinf(a) * RING_OUT, z0o = cosf(a) * RING_OUT;
			float x1i = sinf(b) * RING_IN,  z1i = cosf(b) * RING_IN;
			float x1o = sinf(b) * RING_OUT, z1o = cosf(b) * RING_OUT;
			verts[vi++] = x0i; verts[vi++] = 0; verts[vi++] = z0i; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x1i; verts[vi++] = 0; verts[vi++] = z1i; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x1o; verts[vi++] = 0; verts[vi++] = z1o; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x0i; verts[vi++] = 0; verts[vi++] = z0i; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x1o; verts[vi++] = 0; verts[vi++] = z1o; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x0o; verts[vi++] = 0; verts[vi++] = z0o; verts[vi++] = 0; verts[vi++] = 0;
		}
		return r3d_mesh_custom(ctx->r3d, verts, vi / 5);
	};

	// 背景环（深灰半透明）静态缓存一次
	static bool g_ring_tried = false;
	static render_mesh_p g_ring_bg = NULL;
	if (!g_ring_tried) {
		g_ring_tried = true;
		g_ring_bg = make_ring(0.0f, 6.2831853f);
	}

	// 血量条扇形环：按 HP 比例量化为若干档，mesh 缓存复用
	static constexpr int RING_LEVELS = 64;
	static render_mesh_p g_ring_cache[RING_LEVELS + 1] = { 0 };

	auto hp_view = ctx->ecs.view<TransformComponent, HealthComponent>();
	for (auto e : hp_view) {
		auto& t = hp_view.get<TransformComponent>(e);
		auto& h = hp_view.get<HealthComponent>(e);
		if (h.dead || h.max_hp <= 0) continue;

		// 迷雾视野：敌人不在玩家扇形视野内则连血环一起隐藏（本体已在 entities_pass 隐藏）。
		if (ctx->ecs.all_of<EnemyComponent>(e) &&
			!fog_entity_visible(ctx, t.position.x, t.position.z)) continue;

		float ratio = (float)h.hp / (float)h.max_hp;
		if (ratio < 0.0f) ratio = 0.0f;
		if (ratio > 1.0f) ratio = 1.0f;
		if (ratio <= 0.0f) continue;

		// 背景环：淡墨（浅灰）空血轮廓，符合水墨画风
		if (g_ring_bg) {
			mat44_t mb = mat44_identity();
			mb.m12 = t.position.x; mb.m13 = RING_Y_BG; mb.m14 = t.position.z;
			jcolor_t bgc = { 170, 170, 178, 200 };
			r3d_draw_mesh_solid_basis(ctx->r3d, g_ring_bg, &mb, bgc);
		}

		// 前景血量环：墨色为主（水墨风），低血量时朱砂红警示
		int lvl = (int)(ratio * RING_LEVELS);
		if (lvl > RING_LEVELS) lvl = RING_LEVELS;
		if (lvl < 1) lvl = 1;
		render_mesh_p seg = g_ring_cache[lvl];
		if (!seg) {
			seg = make_ring(-1.5707963f, (float)lvl / RING_LEVELS * 6.2831853f);
			g_ring_cache[lvl] = seg;
		}
		if (seg) {
			mat44_t mf = mat44_identity();
			mf.m12 = t.position.x; mf.m13 = RING_Y_FG; mf.m14 = t.position.z;
			uint8_t cr, cg, cb;
			if (ratio > 0.3f)      { cr = 40;  cg = 40;  cb = 45; }   // 墨色
			else                   { cr = 200; cg = 60;  cb = 50; }   // 低血量朱砂红
			jcolor_t fgc = { cr, cg, cb, 255 };
			r3d_draw_mesh_solid_basis(ctx->r3d, seg, &mf, fgc);
		}
	}
}
