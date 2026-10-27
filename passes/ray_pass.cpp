#include "ray_pass.h"
#include <joy/joy.h>
#include <vector>
#include "../context.h"

// 渲染玩家 hitscan 射线：逻辑层（melee_attack_system 射线分支）把请求写入 ctx->ray_effects，
// 本 pass 消费并转换成一段带生命周期的光束。逻辑帧只有 20Hz、渲染 60Hz：若当帧直绘+立即清空，
// 光束只持续 ≤1 渲染帧(≈16ms)，肉眼不可见。因此引入 lifetime 队列持续约 0.25s 再淡出。
void ray_pass(Context* ctx, float dt) {
	if (!ctx->r3d) return;

	// 光束网格：细长柱，局部 +z 为延伸方向
	static render_mesh_p g_beam = NULL;
	if (!g_beam)
		g_beam = r3d_mesh_cube(ctx->r3d, 0.12f, 0.12f, 1.0f);

	static constexpr float DUR = 0.22f;

	struct Beam {
		float x, z, ux, uz, range;
		float life;
	};
	static std::vector<Beam> g_beams;

	// 消费逻辑层请求：以 DUR 寿命入队
	for (auto& q : ctx->ray_effects) {
		g_beams.push_back({
			fp_to_float(q.x), fp_to_float(q.z),
			fp_to_float(q.dir_x), fp_to_float(q.dir_z),
			fp_to_float(q.range), DUR
		});
	}
	ctx->ray_effects.clear();

	// 老化 + 剔除
	for (auto it = g_beams.begin(); it != g_beams.end();) {
		it->life -= dt;
		if (it->life <= 0.0f) it = g_beams.erase(it);
		else ++it;
	}

	// 亮绿色激光（填充 + 描边，随生命周期淡出）
	for (auto& b : g_beams) {
		float fade = b.life / DUR;
		if (fade < 0.0f) fade = 0.0f;
		if (fade > 1.0f) fade = 1.0f;
		float half = b.range * 0.5f;
		float cx = b.x + b.ux * half;
		float cz = b.z + b.uz * half;

		jcolor_t fill   = { 50, 250, 110, (uint8_t)(200 * fade) };
		jcolor_t stroke = { 30, 200, 80,  (uint8_t)(245 * fade) };

		mat44_t m = mat44_identity();
		// 列主序：local +z → 朝向 (m8,m10) 并沿朝向拉伸到 b.range；local +x → 垂直 (m0,m2)
		m.m0 =  b.uz; m.m2 = -b.ux;
		m.m5 = 1.0f;
		m.m8 =  b.ux * b.range; m.m10 = b.uz * b.range;
		m.m12 = cx; m.m13 = 1.0f; m.m14 = cz;   // y 固定在胸口高度

		r3d_draw_mesh_solid_basis(ctx->r3d, g_beam, &m, fill);
		r3d_draw_wire_basis(ctx->r3d, g_beam, &m, stroke);
	}
}