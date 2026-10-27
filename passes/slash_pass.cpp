#include "slash_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "components/attack_ray_effect_component.h"

void slash_pass(Context* ctx, float dt) {
	if (!ctx->r3d) return;

	static constexpr float FXDUR = 0.22f;
	static constexpr int SEGS = 14;
	static constexpr float HALF = 50.0f * 3.14159265f / 180.0f;

	// 生成拱形光带网格（单位半径，高度中心高两端贴地；经 model 矩阵按攻击距离缩放）
	auto make_band = [&](float r_in, float r_out, float h_max) -> render_mesh_p {
		float verts[SEGS * 6 * 5];
		int vi = 0;
		for (int i = 0; i < SEGS; i++) {
			float a0 = -HALF + (float)i / SEGS * 2.0f * HALF;
			float a1 = -HALF + (float)(i + 1) / SEGS * 2.0f * HALF;
			float h0 = h_max * cosf(fabsf(a0) / HALF * 3.14159265f * 0.5f);
			float h1 = h_max * cosf(fabsf(a1) / HALF * 3.14159265f * 0.5f);
			float x0i = sinf(a0) * r_in, z0i = cosf(a0) * r_in;
			float x0o = sinf(a0) * r_out, z0o = cosf(a0) * r_out;
			float x1i = sinf(a1) * r_in, z1i = cosf(a1) * r_in;
			float x1o = sinf(a1) * r_out, z1o = cosf(a1) * r_out;
			verts[vi++] = x0i; verts[vi++] = h0; verts[vi++] = z0i; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x0o; verts[vi++] = h0; verts[vi++] = z0o; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x1o; verts[vi++] = h1; verts[vi++] = z1o; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x0i; verts[vi++] = h0; verts[vi++] = z0i; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x1o; verts[vi++] = h1; verts[vi++] = z1o; verts[vi++] = 0; verts[vi++] = 0;
			verts[vi++] = x1i; verts[vi++] = h1; verts[vi++] = z1i; verts[vi++] = 0; verts[vi++] = 0;
		}
		return r3d_mesh_custom(ctx->r3d, verts, vi / 5);
	};

	static render_mesh_p g_blade_edge = NULL, g_blade_core = NULL, g_sweep = NULL;
	static bool g_fx_tried = false;
	if (!g_fx_tried) {
		g_fx_tried = true;
		g_blade_edge = make_band(0.58f, 1.0f, 0.85f);   // 淡墨晕染层（外围墨色渐淡）
		g_blade_core = make_band(0.72f, 0.94f, 0.85f);  // 浓墨核心层
		g_sweep      = make_band(0.72f, 1.0f, 0.0f);    // 地面扫掠弧（贴地细带）
	}

	// 消费逻辑层挥击请求，生成刀光特效实体（逻辑 system 不建渲染实体，由本 pass 代劳）
	for (auto& q : ctx->attack_flashes) {
		AttackRayEffectComponent fx;
		fx.x = fp_to_float(q.x);
		fx.z = fp_to_float(q.z);
		fx.dir_x = fp_to_float(q.dir_x);
		fx.dir_z = fp_to_float(q.dir_z);
		fx.range = fp_to_float(q.range);
		fx.is_enemy = q.is_enemy;
		fx.lifetime = FXDUR;
		auto e = ctx->ecs.create();
		ctx->ecs.emplace<AttackRayEffectComponent>(e, fx);
	}
	ctx->attack_flashes.clear();

	// 生命周期（原 effect_lifecycle_system，随渲染职责收拢到此 pass）
	{
		std::vector<entt::entity> to_kill;
		auto v = ctx->ecs.view<AttackRayEffectComponent>();
		for (auto e : v) {
			auto& fx = v.get<AttackRayEffectComponent>(e);
			fx.lifetime -= dt;
			if (fx.lifetime <= 0.0f) to_kill.push_back(e);
		}
		for (auto e : to_kill) ctx->ecs.destroy(e);
	}

	auto fx_view = ctx->ecs.view<AttackRayEffectComponent>();
	for (auto e : fx_view) {
		auto& fx = fx_view.get<AttackRayEffectComponent>(e);
		float p = 1.0f - fx.lifetime / FXDUR;   // 生命周期进度 0→1
		if (p < 0.0f) p = 0.0f;
		if (p > 1.0f) p = 1.0f;
		float fade = 1.0f - 0.6f * p;           // 淡出
		float grow = 1.0f + 0.35f * p;          // 向外展开

		float ux = fx.dir_x, uz = fx.dir_z;   // 刀光朝向单位向量（逻辑帧定点数算出）
		float r = fx.range * grow;
		mat44_t m = mat44_identity();
		// 列主序：光带 mesh 默认朝 +z，经矩阵旋到 dir。中心(0,·,r_in)→世界(ux·r,·,uz·r)
		// 列主序映射：local +z → 世界 (m8, m10)；local +x → 世界 (m0, m2)。
		// 要让刀光朝向 (dir_x, dir_z)：(m0,m2)=右向垂直分量 (uz,-ux)，(m8,m10)=(ux,uz)。
		// 注意 ux 若写反会在 x 轴上镜像，导致刀光与实际命中方向相反。
		m.m0 =  uz * r; m.m2 = -ux * r; m.m5 = 1.0f;
		m.m8 =  ux * r; m.m10 = uz * r;
		m.m12 = fx.x; m.m13 = 0.02f; m.m14 = fx.z;

		// 绘制顺序：地面弧 → 淡墨晕染 → 浓墨核心（核心墨最浓，外围渐淡，如水墨晕染）
		// 敌我区分用色：玩家为水墨灰墨，敌人为黛红墨（均保持墨色晕染画风）
		uint8_t base_r = fx.is_enemy ? 170 : 100;   // 敌人偏红
		uint8_t base_g = fx.is_enemy ?  70 : 100;
		uint8_t base_b = fx.is_enemy ?  70 : 100;
		if (g_sweep) {
			jcolor_t sc = { base_r, base_g, base_b, (uint8_t)(90 * fade) };
			r3d_draw_mesh_solid_basis(ctx->r3d, g_sweep, &m, sc);
		}
		if (g_blade_edge) {
			jcolor_t ec = { (uint8_t)(base_r + 50), (uint8_t)(base_g + 50), (uint8_t)(base_b + 50), (uint8_t)(75 * fade) };
			r3d_draw_mesh_solid_basis(ctx->r3d, g_blade_edge, &m, ec);
		}
		if (g_blade_core) {
			jcolor_t cc = { (uint8_t)(base_r * 0.28f), (uint8_t)(base_g * 0.28f), (uint8_t)(base_b * 0.28f), (uint8_t)(205 * fade) };
			r3d_draw_mesh_solid_basis(ctx->r3d, g_blade_core, &m, cc);
		}
	}
}