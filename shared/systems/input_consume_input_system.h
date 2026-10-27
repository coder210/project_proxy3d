#ifndef INPUT_CONSUME_INPUT_SYSTEM_H
#define INPUT_CONSUME_INPUT_SYSTEM_H

#include "../components/player_physics_component.h"

struct Context;

static void input_consume_input_system(entt::registry& registry, Context* /*ctx*/, float /*dt*/) {
	auto& net = registry.ctx().get<NetComponent>();
	if (net.frame_id < 0) return;

	auto view = registry.view<PlayerComponent>();
	for (int i = 0; i < net.num_inputs; i++) {
		auto& input = net.inputs[i];

		entt::entity player = entt::null;
		for (auto e : view) {
			if (registry.get<PlayerComponent>(e).conv == input.conv) {
				player = e;
				break;
			}
		}
		if (player == entt::null) continue;

		PlayerStateInput state_input;
		state_input.move_bits = input.dir;
		state_input.yaw_quantized = input.yaw_quantized;

		// 跳跃：上升沿触发一次（按住不连跳）
		bool jump_down = (input.dir & INPUT_JUMP) != 0;
		if (registry.all_of<PlayerPhysicsComponent>(player)) {
			auto& phy = registry.get<PlayerPhysicsComponent>(player);
			if (jump_down && !phy.jump_prev) phy.jump_requested = true;
			phy.jump_prev = jump_down;
		}

		// log_info("Raw dir: %d yaw: %d\n", input.dir, input.yaw_quantized);

		if (registry.all_of<PlayerStateInput>(player)) {
			auto& state = registry.get<PlayerStateInput>(player);
			state.move_bits = state_input.move_bits;
			state.yaw_quantized = state_input.yaw_quantized;
		}
		else {
			registry.emplace<PlayerStateInput>(player, state_input);
		}

		if (input.attack) {
			if (registry.all_of<PlayerEventInput>(player)) {
				auto& events = registry.get<PlayerEventInput>(player);
				events.pending_actions.push_back({ input.client_seq, (uint8_t)input.attack });
			}
			else {
				PlayerEventInput events;
				events.pending_actions.push_back({ input.client_seq, (uint8_t)input.attack });
				registry.emplace<PlayerEventInput>(player, events);
			}
		}

	}
	net.num_inputs = 0;
}

#endif
