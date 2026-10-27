#ifndef MOVEMENT_SYSTEM_H
#define MOVEMENT_SYSTEM_H
#include "../components/player_state_input.h"
#include "../components/logic_position_component.h"
#include "../components/player_physics_component.h"
#include "../components/action_state_component.h"
#include "../components/health_component.h"
#include "../components/player_component.h"
#include "../protocol.h"
#include "character_def.h"
#include "../components/pathfinding_component.h"
#include <joy/jmath.h>

struct Context;

static const fp_t GRAVITY    = fp_from_float(9.8f);
static const fp_t JUMP_SPEED = fp_from_float(4.0f);
static const fp_t GROUND_Y   = fp_from_float(0.6f);  // 玩家中心高度（半高 0.6，脚底贴地）

// 空气墙边界：地图世界范围为 [-OFFSET, OFFSET]，边界墙格心 = ±(OFFSET-0.5)。
// 随 PF_GRID_OFFSET(32) 推导，与 64×64 地图一致；全定点计算，帧同步确定性。
static const fp_t WALL_HALF   = fp_div(fp_from_int(1), fp_from_int(2));
static const fp_t WALL_LIMIT  = fp_sub(fp_from_int(PF_GRID_OFFSET), WALL_HALF);          // +31.5
static const fp_t WALL_NEG    = fp_sub(fp_from_int(-PF_GRID_OFFSET), -WALL_HALF); // -31.5

static void movement_system(entt::registry& registry, Context* ctx, float /*dt*/) {
	auto view = registry.view<PlayerStateInput, LogicPositionComponent>();
	for (auto e : view) {
		auto& psi = view.get<PlayerStateInput>(e);
		auto& p   = view.get<LogicPositionComponent>(e);

		// 死亡玩家不移动（等待重生传送）
		if (registry.all_of<HealthComponent>(e) && registry.get<HealthComponent>(e).dead) continue;

		// 移速按角色定义表取（帧同步确定性：两端同一 char_id 查同一张表）
		const CharacterDef& cd = character_def(
			registry.all_of<PlayerComponent>(e) ? registry.get<PlayerComponent>(e).char_id : CHAR_WARRIOR);
		fp_t step = fp_mul(cd.move_speed, fp_from_float(ctx->FIXED_TIMESTEP));

		// 朝向：来自鼠标（量化角度 → 弧度），纯定点计算保证帧同步确定性
		fp_t yaw_fp = fp_mul(fp_div(fp_from_int((int32_t)psi.yaw_quantized),
			fp_from_int(32767)), fp_pi());
		if (registry.all_of<ActionStateComponent>(e))
			registry.get<ActionStateComponent>(e).yaw = fp_to_float(yaw_fp);

		// ---- MOBA 标准：WASD 相对相机方向移动 ----
		// 相机 yaw 固定 0（正对 +Z 看）时：W=+Z、S=-Z、A=+X、D=-X
		// 前向 F=(sin,0,cos)，右侧 R=(-cos,0,sin)，W=F、S=-F、D=R、A=-R
		// cam_yaw 恒为 0 → sy=sin(0)=0、cy=cos(0)=1（纯定点常量）
		const fp_t sy = fp_zero();
		const fp_t cy = fp_one();
		fp_t fdx = fp_zero(), fdz = fp_zero();
		if (psi.move_bits & INPUT_UP)    { fdx = fp_add(fdx, sy); fdz = fp_add(fdz, cy); }
		if (psi.move_bits & INPUT_DOWN)  { fdx = fp_sub(fdx, sy); fdz = fp_sub(fdz, cy); }
		if (psi.move_bits & INPUT_LEFT)  { fdx = fp_add(fdx, cy); fdz = fp_sub(fdz, sy); }
		if (psi.move_bits & INPUT_RIGHT) { fdx = fp_sub(fdx, cy); fdz = fp_add(fdz, sy); }
		if (fdx != fp_zero() || fdz != fp_zero()) {
			// 定点归一化 + 位移（替代 float sqrtf/除法，帧同步确定性）
			fp_t len = fp_sqrt(fp_add(fp_mul(fdx, fdx), fp_mul(fdz, fdz)));
			p.x = fp_add(p.x, fp_mul(fp_div(fdx, len), step));
			p.z = fp_add(p.z, fp_mul(fp_div(fdz, len), step));
		}

		// ---- 垂直物理（重力/跳跃/落地） ----
		if (registry.all_of<PlayerPhysicsComponent>(e)) {
			auto& phy = registry.get<PlayerPhysicsComponent>(e);
			if (phy.jump_requested && phy.on_ground) {
				phy.vertical_velocity = JUMP_SPEED;
				phy.on_ground = false;
			}
			phy.jump_requested = false;
			phy.vertical_velocity = fp_sub(phy.vertical_velocity,
				fp_mul(GRAVITY, fp_from_float(ctx->FIXED_TIMESTEP)));
			p.y = fp_add(p.y, fp_mul(phy.vertical_velocity, fp_from_float(ctx->FIXED_TIMESTEP)));
			if (fp_to_float(p.y) <= fp_to_float(GROUND_Y)) {
				p.y = GROUND_Y;
				phy.vertical_velocity = fp_zero();
				phy.on_ground = true;
			}
		}

		// ---- 空气墙 ----
		p.x = fp_clamp(p.x, WALL_NEG, WALL_LIMIT);
		p.z = fp_clamp(p.z, WALL_NEG, WALL_LIMIT);
	}
}

#endif
