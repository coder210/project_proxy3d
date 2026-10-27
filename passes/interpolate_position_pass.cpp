#include "interpolate_position_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "../shared/components/id_component.h"
#include "../shared/components/logic_position_component.h"
#include "../shared/components/transform_component.h"

void interpolate_position_pass(Context* ctx, float dt) {
	entt::registry& registry = ctx->ecs;
	auto view = registry.view<IdComponent, LogicPositionComponent, TransformComponent>();
	for (auto e : view) {
		auto& lp = view.get<LogicPositionComponent>(e);
		auto& t  = view.get<TransformComponent>(e);
		float target_x = fp_to_float(lp.x);
		float target_y = fp_to_float(lp.y);
		float target_z = fp_to_float(lp.z);
		float alpha = dt * 10.0f;
		if (alpha > 1.0f) alpha = 1.0f;
		t.position.x = ft_lerp(t.position.x, target_x, alpha);
		t.position.y = ft_lerp(t.position.y, target_y, alpha);
		t.position.z = ft_lerp(t.position.z, target_z, alpha);
	}
}