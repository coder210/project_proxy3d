#include "static_collider_pass.h"
#include <joy/joy.h>
#include <cmath>
#include "../context.h"
#include "components/transform_component.h"
#include "components/static_mesh_component.h"
#include "components/player_component.h"
#include "components/global_state_component.h"

// 墙"越远越模糊"（距离淡出）：按障碍中心到本地玩家的 XZ 距离，把填充/描边 alpha 从近处全不透明
// 线性降到远处近乎透明。配合半透明的迷雾，远处墙既被雾罩又自身变淡，形成"渐远渐隐"的观感，
// 近处墙保持清晰。纯渲染改动，不影响寻路/碰撞/帧同步。
#ifndef WALL_DIST_NEAR
#define WALL_DIST_NEAR  4.0f    // 4m 内全不透明
#endif
#ifndef WALL_DIST_FAR
#define WALL_DIST_FAR   28.0f   // 28m 外淡到最深
#endif
#ifndef WALL_FILL_MIN_A
#define WALL_FILL_MIN_A 96      // 最远填充保留的最浅 alpha
#endif
#ifndef WALL_STROKE_MIN_A
#define WALL_STROKE_MIN_A 55    // 最远描边保留的最浅 alpha
#endif

void static_collider_pass(Context* ctx) {
	if (!ctx->r3d) return;

	// 定位本地玩家位置（距离淡出基准）；无本地玩家（观战/服务端）则全部不透明
	const auto& gs = ctx->ecs.ctx().get<GlobalState>();
	int follow_conv = (ctx->local_conv > 0) ? (int)ctx->local_conv : gs.camera_follow_conv;
	float px = 0, pz = 0;
	bool haveP = false;
	if (follow_conv >= 0) {
		auto pv = ctx->ecs.view<TransformComponent, PlayerComponent>();
		for (auto e : pv) {
			if (pv.get<PlayerComponent>(e).conv != follow_conv) continue;
			auto& t = pv.get<TransformComponent>(e);
			px = t.position.x; pz = t.position.z; haveP = true;
			break;
		}
	}

	// 静态碰撞体（填充 + 描边双 pass）
	auto static_view = ctx->ecs.view<TransformComponent, StaticMeshComponent>();
	for (auto e : static_view) {
		auto& t = static_view.get<TransformComponent>(e);
		auto& sm = static_view.get<StaticMeshComponent>(e);
		if (!sm.mesh) continue;
		mat44_t m = mat44_identity();
		float sx = t.scale.x, sy = t.scale.y, sz = t.scale.z;
		if (sm.roll != 0.0f) {
			float cr = cosf(sm.roll), sr = sinf(sm.roll);
			m.m5 = cr * sy; m.m6 = sr * sy;
			m.m9 = -sr * sz; m.m10 = cr * sz;
		} else if (t.yaw != 0.0f) {
			float c = cosf(t.yaw), s = sinf(t.yaw);
			m.m0 = sx * c;  m.m2 = sx * -s; m.m5 = sy;
			m.m8 = sz * s;  m.m10 = sz * c;
		} else {
			m.m0 = sx; m.m5 = sy; m.m10 = sz;
		}
		m.m12 = t.position.x; m.m13 = t.position.y; m.m14 = t.position.z;

		jcolor_t fill = sm.color, stroke = sm.stroke_color;
		if (haveP) {
			float dx = t.position.x - px, dz = t.position.z - pz;
			float d = sqrtf(dx * dx + dz * dz);
			float a = (d - WALL_DIST_NEAR) / (WALL_DIST_FAR - WALL_DIST_NEAR);
			if (a < 0.0f) a = 0.0f; else if (a > 1.0f) a = 1.0f;
			fill.a   = (uint8_t)(255.0f - (255.0f - WALL_FILL_MIN_A)   * a);
			stroke.a = (uint8_t)(WALL_STROKE_MIN_A + (255.0f - WALL_STROKE_MIN_A) * (1.0f - a));
		}
		r3d_draw_mesh_solid_basis(ctx->r3d, sm.mesh, &m, fill);
		if (sm.wire_mesh)
			r3d_draw_wire_basis(ctx->r3d, sm.wire_mesh, &m, stroke);
	}
}