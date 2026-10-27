#include "entities_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "components/transform_component.h"
#include "components/mesh_component.h"
#include "components/health_component.h"
#include "components/player_component.h"
#include "components/global_state_component.h"
#include "components/hit_flash_component.h"
#include "components/enemy_component.h"
#include "fog_pass.h"

// 估算 UTF-8 文字世界宽度（ASCII 半宽、CJK 全宽），用于昵称头顶居中
static float utf8_display_width(const char* s, float size)
{
	float w = 0;
	const unsigned char* p = (const unsigned char*)s;
	while (*p) {
		if (*p < 0x80) { w += size * 0.55f; p += 1; }
		else if ((*p & 0xE0) == 0xC0) { w += size; p += 2; }
		else if ((*p & 0xF0) == 0xE0) { w += size; p += 3; }
		else { w += size; p += 4; }
	}
	return w;
}

void entities_pass(Context* ctx, float dt) {
	if (!ctx->r3d) return;

	// 实体（填充 + 描边双 pass）
	auto view = ctx->ecs.view<TransformComponent, MeshComponent>();
	for (auto e : view) {
		auto& t = view.get<TransformComponent>(e);
		auto& mc = view.get<MeshComponent>(e);
		if (!mc.mesh) continue;
		// 死亡敌人不再绘制（血环已跳过），逻辑上其实体保留到波次结束由 wave_system 统一销毁
		if (ctx->ecs.all_of<HealthComponent>(e) &&
			ctx->ecs.get<HealthComponent>(e).dead) continue;

		// 迷雾视野：敌人不在玩家扇形视野内（超距 / 朝向外 / 被墙遮挡）则隐藏，
		// 玩家与环境不受影响，始终绘制。
		if (ctx->ecs.all_of<EnemyComponent>(e) &&
			!fog_entity_visible(ctx, t.position.x, t.position.z)) continue;

		// 受击闪烁：闪烁期间填充色提亮为白色，时间递减到 0 后移除（纯客户端表现）
		jcolor_t draw_color = mc.color;
		if (ctx->ecs.all_of<HitFlashComponent>(e)) {
			auto& hf = ctx->ecs.get<HitFlashComponent>(e);
			if (hf.timer > 0.0f) {
				draw_color = { 255, 255, 255, 255 };
				hf.timer -= dt;
				if (hf.timer <= 0.0f) ctx->ecs.remove<HitFlashComponent>(e);
			}
		}

		mat44_t m = mat44_identity();
		m.m0 = t.scale.x; m.m5 = t.scale.y; m.m10 = t.scale.z;
		m.m12 = t.position.x; m.m13 = t.position.y; m.m14 = t.position.z;
		r3d_draw_mesh_solid_basis(ctx->r3d, mc.mesh, &m, draw_color);
		if (mc.wire_mesh)
			r3d_draw_wire_basis(ctx->r3d, mc.wire_mesh, &m, mc.stroke_color);
	}
}

// 玩家昵称头顶显示（屏幕投影方式：固定像素字号，不受相机距离/角度影响）。
// 独立函数、在雾层之后调用，保证昵称始终浮在迷雾之上可见，不被环境雾遮挡。
void entities_pass_draw_nicknames(Context* ctx) {
	if (!ctx->r3d) return;

	static bool   q_tried = false;
	static render_mesh_p q = NULL;
	if (!q_tried) { q_tried = true; q = r3d_mesh_quad(ctx->r3d); }
	if (!q) return;

	auto view = ctx->ecs.view<TransformComponent, MeshComponent>();
	for (auto e : view) {
		if (!ctx->ecs.all_of<PlayerComponent>(e)) continue;
		auto& t = view.get<TransformComponent>(e);
		const char* nick = ctx->ecs.get<PlayerComponent>(e).nickname;
		if (!nick || !nick[0]) continue;

		vec3_t centerPos = { t.position.x, t.position.y, t.position.z };
		float cx, cy;
		if (!r3d_world_to_screen(ctx->r3d, centerPos, &cx, &cy)) continue;
		int winW, winH;
		r3d_get_window_size(ctx->r3d, &winW, &winH);

		const float fontSize = 16.0f;
		float sy = cy - 14.0f;
		float textW = utf8_display_width(nick, fontSize);
		r3d_font_draw_2d_outline(ctx->r3d, q, nick,
			cx - textW * 0.5f, sy - fontSize * 0.5f, fontSize, (float)winW, (float)winH,
			0, 0, 0, 255, 255, 255, 1.5f);
	}
}
