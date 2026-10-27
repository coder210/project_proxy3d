#include "bullet_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "components/bullet_component.h"

void bullet_pass(Context* ctx) {
	if (!ctx->r3d) return;

	// 子弹网格：短横条，局部 +z 为飞行方向；在 each bullet 处旋转到朝向来绘制
	static render_mesh_p g_bullet = NULL;
	if (!g_bullet)
		g_bullet = r3d_mesh_cube(ctx->r3d, 0.14f, 0.14f, 0.7f);

	jcolor_t p_fill = { 60, 220, 230, 255 };   // 玩家子弹：青
	jcolor_t p_stroke = { 30, 180, 200, 255 };
	jcolor_t e_fill = { 235, 60, 50, 255 };    // 敌人子弹：红
	jcolor_t e_stroke = { 200, 30, 25, 255 };

	auto view = ctx->ecs.view<BulletComponent>();
	for (auto e : view) {
		auto& b = view.get<BulletComponent>(e);
		float ux = fp_to_float(b.dx), uz = fp_to_float(b.dz);
		mat44_t m = mat44_identity();
		// 列主序旋转：local +z → 飞行方向 (m8,m10)，local +x → 垂直 (m0,m2)
		m.m0 =  uz; m.m2 = -ux;
		m.m5 = 1.0f;
		m.m8 =  ux; m.m10 = uz;
		m.m12 = fp_to_float(b.x);
		m.m13 = fp_to_float(b.y);
		m.m14 = fp_to_float(b.z);

		if (b.from_enemy) {
			r3d_draw_mesh_solid_basis(ctx->r3d, g_bullet, &m, e_fill);
			r3d_draw_wire_basis(ctx->r3d, g_bullet, &m, e_stroke);
		} else {
			r3d_draw_mesh_solid_basis(ctx->r3d, g_bullet, &m, p_fill);
			r3d_draw_wire_basis(ctx->r3d, g_bullet, &m, p_stroke);
		}
	}
}