#include "camera_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "components/transform_component.h"
#include "components/global_state_component.h"
#include "components/player_component.h"

bool camera_pass(Context* ctx, float dt) {
	if (!ctx->r3d) return false;

	auto& gs = ctx->ecs.ctx().get<GlobalState>();

	// 白底蓝图风（与 project_proxy2d 背景一致）
	r3d_set_clear_color(ctx->r3d, 255, 255, 255);

	// 复刻 Sebastian Lague Create-a-Game 的俯视视角：透视 FOV 60°、绕 X 轴俯角 75°、
	// 相机位于玩家后上方约 (0, +27, -7.2)，64×64 大地图整体尽收眼底
	const float CAM_DIST  = 28.0f;
	const float CAM_PITCH = 75.0f * 3.14159265f / 180.0f;
	float yaw = 0.0f;
	float cp = cosf(CAM_PITCH), sp = sinf(CAM_PITCH);

	// 相机跟随：优先本地玩家，其次 camera_follow_conv，都没有则看原点（服务端观战）
	int follow_conv = (ctx->local_conv > 0) ? (int)ctx->local_conv : gs.camera_follow_conv;
	if (follow_conv >= 0) {
		auto pv = ctx->ecs.view<PlayerComponent, TransformComponent>();
		for (auto e : pv) {
			auto& pc = pv.get<PlayerComponent>(e);
			if (pc.conv == follow_conv) {
				auto& tf = pv.get<TransformComponent>(e);
				float alpha = dt * 10.0f;
				if (alpha > 1.0f) alpha = 1.0f;
				gs.camera_target.x = ft_lerp(gs.camera_target.x, tf.position.x, alpha);
				gs.camera_target.z = ft_lerp(gs.camera_target.z, tf.position.z, alpha);
				break;
			}
		}
	}

	vec3_t target = { gs.camera_target.x, gs.camera_target.y, gs.camera_target.z };
	vec3_t aim = { target.x, target.y + 1.0f, target.z };
	vec3_t eye = {
		target.x - sinf(yaw) * cp * CAM_DIST,
		target.y + sp * CAM_DIST,
		target.z - cosf(yaw) * cp * CAM_DIST
	};
	gs.camera_position = eye;
	vec3_t up = { 0.0f, 1.0f, 0.0f };

	if (!r3d_begin(ctx->r3d)) return false;
	r3d_camera_lookat(ctx->r3d, eye, aim, up, 60.0f);
	return true;
}