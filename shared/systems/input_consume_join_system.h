#ifndef INPUT_CONSUME_JOIN_SYSTEM_H
#define INPUT_CONSUME_JOIN_SYSTEM_H

#include "../components/global_state_component.h"
#include "../components/collider_component.h"
#include "../components/player_physics_component.h"
#include "../components/player_state_input.h"
#include "../components/health_component.h"
#include "../components/spawn_point_component.h"
#include "../components/mesh_component.h"
#include "character_def.h"

struct Context;

static void input_consume_join_system(entt::registry& registry, Context* ctx, float /*dt*/) {
	auto& net = registry.ctx().get<NetComponent>();
	if (net.frame_id < 0) return;

	auto& gs = registry.ctx().get<GlobalState>();

	for (int i = 0; i < net.num_joins; i++) {
		log_info("creating player %d", net.num_joins);
		auto& join = net.joins[i];

		// 去重：同一 conv 的玩家已存在（如 handle_loading 已创建）则跳过
		bool exists = false;
		auto pv = registry.view<PlayerComponent>();
		for (auto e : pv) {
			if (pv.get<PlayerComponent>(e).conv == join.conv) { exists = true; break; }
		}
		if (exists) continue;

		entt::entity player = registry.create();
		registry.emplace<IdComponent>(player, get_entity_id(ctx));
		PlayerComponent pc;
		pc.conv = join.conv;
		pc.char_id = join.char_id;
		snprintf(pc.nickname, sizeof(pc.nickname), "%s", join.nickname);
		registry.emplace<PlayerComponent>(player, pc);
		registry.emplace<LogicPositionComponent>(player, join.position_x, join.position_y, join.position_z);
		registry.emplace<LogicRectComponent>(player, fp_from_float(1), fp_from_float(1));
		registry.emplace<TransformComponent>(player,
			vec3_t{ fp_to_float(join.position_x), fp_to_float(join.position_y), fp_to_float(join.position_z) },
			0.0f,
			vec3_t{ 1.0f, 1.0f, 1.0f });

		// 玩家输入/碰撞/物理组件（无动画时也可移动/跳跃）
		registry.emplace<PlayerStateInput>(player);
		registry.emplace<ColliderComponent>(player);
		registry.emplace<PlayerPhysicsComponent>(player);

		// 血量与出生点（死亡重生用；出生点=加入位置，帧同步确定性）
		// 血量上限按角色定义表取（战士高血、射手/刺客低血）
		const CharacterDef& cd = character_def(join.char_id);
		registry.emplace<HealthComponent>(player, cd.max_hp, cd.max_hp);
		registry.emplace<SpawnPointComponent>(player, join.position_x, join.position_y, join.position_z);

		// 挂载状态组件（承载 yaw/朝向数据，3D 相机跟随需要）
		registry.emplace<ActionStateComponent>(player);

		// 渲染网格：从共享资源池取玩家 mesh，每个玩家实体独立持有（填充/描边按角色色）
		MeshComponent mc;
		mc.mesh = ctx->mesh_player;
		mc.wire_mesh = ctx->mesh_player_wire;
		mc.color = { cd.fill_r, cd.fill_g, cd.fill_b, 255 };
		mc.stroke_color = { cd.stroke_r, cd.stroke_g, cd.stroke_b, 255 };
		registry.emplace<MeshComponent>(player, mc);

		// 首次加入的玩家作为摄像机跟随目标
		if (gs.camera_follow_conv < 0) {
			gs.camera_follow_conv = join.conv;
		}
	}
	net.num_joins = 0;
}

#endif
