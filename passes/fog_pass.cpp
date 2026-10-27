#include "fog_pass.h"
#include <joy/joy.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "../context.h"
#include "components/transform_component.h"
#include "components/player_component.h"
#include "components/action_state_component.h"
#include "components/global_state_component.h"
#include "components/collider_component.h"
#include "components/logic_position_component.h"
#include "components/static_mesh_component.h"

#ifndef FOG_VIS_RADIUS
#define FOG_VIS_RADIUS 14.0f      // 视野半径（米）：无墙方向可见到此
#endif
#ifndef FOG_NEAR_RADIUS
#define FOG_NEAR_RADIUS 1.8f      // 身周近视角半径（米）：实体可见性判定的近距兜底
#endif
#ifndef FOG_OUTER_RADIUS
#define FOG_OUTER_RADIUS 46.0f    // 雾外径（米）：需盖住整个 64m 地图
#endif
#ifndef FOG_PLANE_Y
#define FOG_PLANE_Y 2.4f          // 雾面高度：高于角色头顶与墙顶，俯视时盖住区外
#endif
#ifndef FOG_MESH_ANGLE
#define FOG_MESH_ANGLE 2.0f       // 边界细分角度（度）：越小边界越圆润，顶点越多
#endif
#ifndef FOG_FEATHER_WIDTH
#define FOG_FEATHER_WIDTH 1.6f    // 可见边界羽化宽度（米）：此范围内雾由透明渐变到全浓，消除硬边/锯齿
#endif

// 墙只在关卡加载时创建、布局静止，做静态缓存：仅当墙实体数量变化时重建。
static bool fog_ensure_walls(Context* ctx, std::vector<joy_fov_rect>& out) {
	static std::vector<joy_fov_rect> s_walls;
	static int s_wall_sign = -1;
	int wcount = 0;
	const auto& sv = ctx->ecs.view<ColliderComponent, LogicPositionComponent>();
	for (auto e : sv)
		if (sv.get<ColliderComponent>(e).type == ColliderComponent::BOX &&
			ctx->ecs.all_of<StaticMeshComponent>(e)) wcount++;
	if (wcount != s_wall_sign) {
		s_walls.clear();
		for (auto e : sv) {
			if (sv.get<ColliderComponent>(e).type != ColliderComponent::BOX) continue;
			if (!ctx->ecs.all_of<StaticMeshComponent>(e)) continue;
			auto& c  = sv.get<ColliderComponent>(e);
			auto& lp = sv.get<LogicPositionComponent>(e);
			joy_fov_rect a;
			a.x = fp_to_float(lp.x); a.z = fp_to_float(lp.z);
			a.hx = fp_to_float(c.half_size.x); a.hz = fp_to_float(c.half_size.z);
			s_walls.push_back(a);
		}
		s_wall_sign = wcount;
	}
	out = s_walls;
	return true;
}

// 定位本地玩家世界坐标（XZ）。返回 false 表示无本地玩家（服务端/观战）。
static bool fog_find_player(Context* ctx, float& px, float& pz) {
	const auto& gs = ctx->ecs.ctx().get<GlobalState>();
	int follow_conv = (ctx->local_conv > 0) ? (int)ctx->local_conv : gs.camera_follow_conv;
	if (follow_conv < 0) return false;
	auto pv = ctx->ecs.view<PlayerComponent, TransformComponent, ActionStateComponent>();
	for (auto e : pv) {
		auto& pc = pv.get<PlayerComponent>(e);
		if (pc.conv == follow_conv) {
			auto& tf = pv.get<TransformComponent>(e);
			px = tf.position.x; pz = tf.position.z;
			return true;
		}
	}
	return false;
}

void fog_pass(Context* ctx) {
	if (!ctx->r3d) return;

	float px, pz;
	if (!fog_find_player(ctx, px, pz)) return;

	std::vector<joy_fov_rect> walls;
	fog_ensure_walls(ctx, walls);
	if (walls.empty()) return;   // 无墙 → 全图可见，不画雾

	// 逐方向可见半径(引擎逐角精确射线，绝不穿墙)，再交由 joy 构建羽化软边雾面。
	const int SEG = (int)ceilf(360.0f / FOG_MESH_ANGLE);
	const float STEP = 360.0f / SEG;
	static std::vector<float> R;
	R.resize((size_t)SEG);
	joy_fov_compute_radii(walls.data(), (int)walls.size(),
		px, pz, FOG_VIS_RADIUS, 0.0f, STEP, SEG, R.data());

	joy_fov_draw(ctx->r3d, px, FOG_PLANE_Y, pz, R.data(), SEG,
		FOG_FEATHER_WIDTH, FOG_OUTER_RADIUS);
}

// 判定世界坐标 (ex,ez) 上的敌人在本地玩家的全向视野内是否可见
// （距离 + 墙遮挡；全向，无扇形朝向限制）。无本地玩家（服务端/观战）则视为可见。
bool fog_entity_visible(Context* ctx, float ex, float ez) {
	if (!ctx->r3d) return true;

	float px, pz;
	if (!fog_find_player(ctx, px, pz)) return true;

	float dx = ex - px, dz = ez - pz;
	float d = sqrtf(dx * dx + dz * dz);
	if (d <= FOG_NEAR_RADIUS) return true;
	if (d > FOG_VIS_RADIUS) return false;

	float inv = 1.0f / d;
	float nx = dx * inv, nz = dz * inv;
	std::vector<joy_fov_rect> walls;
	fog_ensure_walls(ctx, walls);
	float hit = joy_fov_ray_dist(walls.data(), (int)walls.size(), px, pz, nx, nz);
	if (hit < d - 1e-3f) return false;
	return true;
}