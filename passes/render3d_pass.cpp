#include "render3d_pass.h"
#include <SDL3/SDL_timer.h>
#include <joy/joy.h>
#include "../context.h"
#include "../shared/components/enemy_component.h"
#include "../shared/components/bullet_component.h"

#include "camera_pass.h"
#include "floor_pass.h"
#include "entities_pass.h"
#include "static_collider_pass.h"
#include "health_ring_pass.h"
#include "slash_pass.h"
#include "bullet_pass.h"
#include "ray_pass.h"
#include "damage_pass.h"
#include "fog_pass.h"
#include "decor_pass.h"
#include "hud_pass.h"

void render3d_pass(Context* ctx, float dt) {
	if (!ctx->r3d) return;

	uint64_t freq = SDL_GetPerformanceFrequency();
	uint64_t t_frame0 = SDL_GetPerformanceCounter();

	// 相机 pass 内部完成 r3d_begin + 清屏 + lookat；失败则本帧不绘制
	if (!camera_pass(ctx, dt)) return;

	// [PROFILE] 逐 pass 耗时计时（性能计数器微秒级；120 帧累计取平均定位大 pass）
	const char* g_names[] = { "floor","rings","entities","colliders","slash","bullet","ray","damage","fog","hud" };
	static double      g_acc[10] = { 0 };
	static Uint32      g_frames = 0;

	double pass_ms[10] = { 0 };

#define TIMED_EVAL(idx, expr) do { \
		uint64_t _a = SDL_GetPerformanceCounter(); \
		expr; \
		uint64_t _b = SDL_GetPerformanceCounter(); \
		pass_ms[idx] = (double)(_b - _a) / freq * 1000.0; \
		g_acc[idx] += pass_ms[idx]; \
	} while (0)

	TIMED_EVAL(0, floor_pass(ctx));
	TIMED_EVAL(1, health_ring_pass(ctx));
	TIMED_EVAL(2, entities_pass(ctx, dt));
	TIMED_EVAL(3, static_collider_pass(ctx));
	TIMED_EVAL(4, slash_pass(ctx, dt));
	TIMED_EVAL(5, bullet_pass(ctx));
	TIMED_EVAL(6, ray_pass(ctx, dt));
	TIMED_EVAL(7, damage_pass(ctx, dt));
	TIMED_EVAL(8, fog_pass(ctx));
	decor_pass(ctx);   // 地图装饰（门/焦点柱）画在雾之上，始终清晰可见；不纳入 profile
	entities_pass_draw_nicknames(ctx);   // 昵称画在雾之上，始终可见
	TIMED_EVAL(9, hud_pass(ctx));

	uint64_t t_frame1 = SDL_GetPerformanceCounter();
	double frame_ms = (double)(t_frame1 - t_frame0) / freq * 1000.0;

	// 掉帧检测：单帧超过 33ms（掉到 30 帧以下）时立即输出该帧详细分布，定位卡顿瞬间
	if (frame_ms > 33.0) {
		int enemies = 0, bullets = 0;
		for (auto e : ctx->ecs.view<EnemyComponent>()) { (void)e; enemies++; }
		for (auto e : ctx->ecs.view<BulletComponent>()) { (void)e; bullets++; }
		std::string s;
		for (int i = 0; i < 10; i++) {
			s += g_names[i]; s += "="; s += std::to_string(pass_ms[i]); s += " ";
		}
		log_info("[spike] frame=%.1fms drawcalls=%d enemies=%d bullets=%d %s",
			frame_ms, r3d_get_draw_calls(ctx->r3d), enemies, bullets, s.c_str());
	}

	g_frames++;
	if (g_frames >= 120) {
		std::string s;
		for (int i = 0; i < 10; i++) {
			s += g_names[i]; s += "="; s += std::to_string(g_acc[i] / 120.0); s += " ";
		}
		log_info("[render-pass] %s drawcalls=%d", s.c_str(), r3d_get_draw_calls(ctx->r3d));
		for (int i = 0; i < 10; i++) g_acc[i] = 0;
		g_frames = 0;
	}
#undef TIMED_EVAL

	r3d_end(ctx->r3d);
}
