#ifndef SNAPSHOT_SYSTEM_H
#define SNAPSHOT_SYSTEM_H
#include <joy/joy.h>
#include "../context.h"
#include "components/static_mesh_component.h"
#include "components/player_component.h"
#include "components/enemy_component.h"
#include "components/health_component.h"
#include "components/ai_component.h"
#include "components/pathfinding_component.h"
#include "components/transform_component.h"
#include "components/id_component.h"
#include "components/logic_position_component.h"
#include "components/logic_rect_component.h"
#include "components/global_state_component.h"

static void snapshot_system(entt::registry& registry, Context* ctx, float)
{
	int frame_id = registry.ctx().get<GlobalState>().next_frame_id;
	S2CEntity entities[512];
	int ec = 0;

	auto view = registry.view<IdComponent, LogicRectComponent, LogicPositionComponent, TransformComponent>();
	for (auto e : view) {
		auto& id = view.get<IdComponent>(e);
		auto& rect = view.get<LogicRectComponent>(e);
		auto& pos = view.get<LogicPositionComponent>(e);
		auto& tf = view.get<TransformComponent>(e);

		int type = S2C_TYPE_PROP;
		if (registry.any_of<PlayerComponent>(e)) type = S2C_TYPE_PLAYER;
		else if (registry.any_of<EnemyComponent>(e)) type = S2C_TYPE_ENEMY;

		// 对象池：死亡未复用的敌人保留实体但已冻结（不渲染/不模拟）。快照跳过它们，
		// 减小跨波期间的同步数据量。玩家不跳过（会重生，重生后需重新下发）。
		if (type == S2C_TYPE_ENEMY &&
			registry.all_of<HealthComponent>(e) &&
			registry.get<HealthComponent>(e).dead) continue;

		int16_t yaw = (int16_t)(tf.yaw / 3.14159265f * 32767.0f);

		entities[ec].type = type;
		entities[ec].id = id.id;
		entities[ec].width = rect.width;
		entities[ec].height = rect.height;
		entities[ec].position_x = pos.x;
		entities[ec].position_y = pos.y;
		entities[ec].position_z = pos.z;
		entities[ec].yaw_quantized = yaw;
		entities[ec].player_conv = 0;
		entities[ec].ai_type = 0;
		entities[ec].hp = 0;
		if (registry.all_of<HealthComponent>(e))
			entities[ec].hp = registry.get<HealthComponent>(e).hp;
		if (type == S2C_TYPE_PLAYER) {
			entities[ec].player_conv = registry.get<PlayerComponent>(e).conv;
			entities[ec].char_id = registry.get<PlayerComponent>(e).char_id;
			snprintf(entities[ec].nickname, sizeof(entities[ec].nickname), "%s",
				registry.get<PlayerComponent>(e).nickname);
		}
		if (type == S2C_TYPE_ENEMY) {
			// 必须下发真实出生类型（SpawnIdxComponent.idx），客户端据此用同一张
			// ENEMY_SPAWNS 表重建 AI 半径（巡逻/侦测/攻击/领地）。若这里是固定 0，
			// 客户端会把所有首波敌人建成全局刺客型，导致 AI 行为、位置与权威端分歧，
			// 双端近战判定（GJK）不一致 → 服务端击杀的敌人客户端未死亡（帧同步不同步）。
			entities[ec].ai_type = registry.all_of<SpawnIdxComponent>(e)
				? (int16_t)registry.get<SpawnIdxComponent>(e).idx : 0;
			// 同步 AI 运行时状态（timer/state/target），客户端接入时恢复，
			// 否则持续累积的 AI 状态起点不同步 → 敌人行为/位置分叉（同 project_proxy2d）
			if (registry.all_of<AIComponent>(e)) {
				auto& ai = registry.get<AIComponent>(e);
				entities[ec].ai_timer    = ai.timer;
				entities[ec].ai_state    = ai.state;
				entities[ec].ai_target_x = ai.target_x;
				entities[ec].ai_target_z = ai.target_z;
			}
			// 同步 PathComponent 状态：客户端恢复时若缺 PathComponent，
			// set_target 会新建（target_gx=0），导致 dirty=true → 不必要的路径重算 → 双端分叉
			entities[ec].pf_target_gx = 0;
			entities[ec].pf_target_gz = 0;
			entities[ec].pf_path_len  = 0;
			entities[ec].pf_next_idx  = 0;
			entities[ec].pf_recompute_cd = 0;
			if (registry.all_of<PathComponent>(e)) {
				auto& pc = registry.get<PathComponent>(e);
				entities[ec].pf_target_gx = pc.target_gx;
				entities[ec].pf_target_gz = pc.target_gz;
				entities[ec].pf_path_len  = pc.path_len;
				entities[ec].pf_next_idx  = pc.next_idx;
				entities[ec].pf_recompute_cd = pc.recompute_cooldown;
			}
		}
		ec++;
	}

	auto data = pack_world(ctx->ecs.ctx().get<GlobalState>().next_entity_id, entities, ec);
	ctx->world_snapshots[frame_id] = data;

	// 清理旧 snapshot（保留最近100帧）
	while (ctx->world_snapshots.size() > 100) {
		auto it = ctx->world_snapshots.begin();
		ctx->world_snapshots.erase(it);
	}
}

#endif