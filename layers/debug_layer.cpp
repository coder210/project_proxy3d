#include "debug_layer.h"
#include "../context.h"
#include "../shared/components/global_state_component.h"
#include "../shared/components/enemy_component.h"
#include "../shared/components/player_component.h"
#include "../shared/components/health_component.h"
#include "../shared/components/logic_position_component.h"
#include "../shared/components/collider_component.h"
#include "../shared/components/static_mesh_component.h"
#include "../shared/components/wave_advance_component.h"
#include "../shared/systems/level_maps.h"
#include <cstdio>
#include <cstring>

// 小地图：世界坐标 [-32,32] 映射到右上角矩形（避开右下角攻击/射击按钮）。
// 相机位于玩家正后方(-Z)看向 +Z，屏幕右 = 世界 -X，故 X 轴也反转；Z 轴反转使北在上。
static void draw_minimap(Context* ctx, render_mesh_p q)
{
	const float mm_x = 356.0f, mm_y = 10.0f, mm_w = 116.0f, mm_h = 96.0f;
	const float world = (float)PF_GRID_N;      // 世界范围 64 米（-32..32）
	const float half_world = world * 0.5f;     // 世界半幅（一象限 20 米）

	// 水墨风：宣纸底 + 墨色边框
	r3d_draw_rect_2d(ctx->r3d, q, mm_x, mm_y, mm_w, mm_h, 248, 246, 240, 480.0f, 320.0f);
	r3d_draw_rect_2d(ctx->r3d, q, mm_x - 1.0f, mm_y - 1.0f, mm_w + 2.0f, 1.5f, 40, 40, 45, 480.0f, 320.0f);
	r3d_draw_rect_2d(ctx->r3d, q, mm_x - 1.0f, mm_y + mm_h - 0.5f, mm_w + 2.0f, 1.5f, 40, 40, 45, 480.0f, 320.0f);
	r3d_draw_rect_2d(ctx->r3d, q, mm_x - 1.0f, mm_y - 1.0f, 1.5f, mm_h + 2.0f, 40, 40, 45, 480.0f, 320.0f);
	r3d_draw_rect_2d(ctx->r3d, q, mm_x + mm_w - 0.5f, mm_y - 1.0f, 1.5f, mm_h + 2.0f, 40, 40, 45, 480.0f, 320.0f);

	auto to_map = [&](float wx, float wz, float& mx, float& my) {
		mx = mm_x + (half_world - wx) / world * mm_w;   // X 反转：屏幕右 = 世界 -X
		my = mm_y + (half_world - wz) / world * mm_h;   // Z 反转：北(世界 -Z)在上
	};

	// 障碍物（淡墨方块，尺寸按碰撞体缩放）
	auto ov = ctx->ecs.view<StaticMeshComponent, ColliderComponent, LogicPositionComponent>();
	for (auto e : ov) {
		auto& opos = ov.get<LogicPositionComponent>(e);
		auto& col  = ov.get<ColliderComponent>(e);
		float mx, my;
		to_map(fp_to_float(opos.x), fp_to_float(opos.z), mx, my);
		float bw = fp_to_float(col.half_size.x) * 2.0f / world * mm_w;
		float bh = fp_to_float(col.half_size.z) * 2.0f / world * mm_h;
		if (bw < 2.0f) bw = 2.0f;
		if (bh < 2.0f) bh = 2.0f;
		r3d_draw_rect_2d(ctx->r3d, q, mx - bw * 0.5f, my - bh * 0.5f, bw, bh, 90, 90, 98, 480.0f, 320.0f);
	}

	// 存活敌人（朱砂红点）
	auto ev = ctx->ecs.view<EnemyComponent, LogicPositionComponent, HealthComponent>();
	for (auto e : ev) {
		if (ev.get<HealthComponent>(e).dead) continue;
		auto& epos = ev.get<LogicPositionComponent>(e);
		float mx, my;
		to_map(fp_to_float(epos.x), fp_to_float(epos.z), mx, my);
		r3d_draw_rect_2d(ctx->r3d, q, mx - 2.0f, my - 2.0f, 4.0f, 4.0f, 200, 60, 50, 480.0f, 320.0f);
	}

	// 玩家（本地墨色大点，其他淡墨小点）
	auto pv = ctx->ecs.view<PlayerComponent, LogicPositionComponent>();
	for (auto e : pv) {
		auto& ppos = pv.get<LogicPositionComponent>(e);
		float mx, my;
		to_map(fp_to_float(ppos.x), fp_to_float(ppos.z), mx, my);
		bool local = pv.get<PlayerComponent>(e).conv == (int)ctx->local_conv;
		if (local)
			r3d_draw_rect_2d(ctx->r3d, q, mx - 3.0f, my - 3.0f, 6.0f, 6.0f, 40, 40, 45, 480.0f, 320.0f);
		else
			r3d_draw_rect_2d(ctx->r3d, q, mx - 2.5f, my - 2.5f, 5.0f, 5.0f, 120, 120, 128, 480.0f, 320.0f);
	}

	// 出口：未激活淡灰方角，清场后转朱砂/金高亮（存活敌人数本地推导，确定性）
	auto& gs = ctx->ecs.ctx().get<GlobalState>();
	bool cleared = (gs.wave > 0);
	auto ev2 = ctx->ecs.view<EnemyComponent, HealthComponent>();
	for (auto e : ev2)
		if (!ev2.get<HealthComponent>(e).dead) { cleared = false; break; }
	{
		fp_t ewx, ewz;
		level_exit_position(gs.level, &ewx, &ewz);
		float mx, my;
		to_map(fp_to_float(ewx), fp_to_float(ewz), mx, my);
		const float s = 8.0f;
		uint8_t r = cleared ? 230 : 150, g = cleared ? 170 : 110, b = cleared ? 40 : 110;
		r3d_draw_rect_2d(ctx->r3d, q, mx - s * 0.5f, my - s * 0.5f, s, s, r, g, b, 480.0f, 320.0f);
	}
}

// 波次过渡提示：换波后敌人延迟生成期间，屏幕中央偏上显示"Wave N 即将来袭" + 倒计时。
// 水墨风：墨色文字 + 宣纸色描边，无背景色块。
static void draw_wave_hint(Context* ctx, render_mesh_p q)
{
	auto wav = ctx->ecs.view<WaveAdvanceComponent>();
	for (auto e : wav) {
		auto& wa = wav.get<WaveAdvanceComponent>(e);
		char wtxt[64];
		snprintf(wtxt, sizeof(wtxt), "Wave %d 即将来袭", wa.wave);
		float remain = (float)wa.delay_ticks / 20.0f;   // 20Hz 逻辑帧
		if (remain < 0.0f) remain = 0.0f;
		char ct[32];
		snprintf(ct, sizeof(ct), "%.1f", remain);

		r3d_font_draw_2d_outline(ctx->r3d, q, wtxt, 240.0f - 58.0f, 100.0f, 22.0f, 480.0f, 320.0f,
			40, 40, 45, 248, 246, 240, 1.5f);
		r3d_font_draw_2d_outline(ctx->r3d, q, ct, 240.0f - 6.0f, 122.0f, 14.0f, 480.0f, 320.0f,
			90, 90, 98, 248, 246, 240, 1.5f);
		break;
	}
}

// 击杀反馈：2 秒内显示"击杀/双杀/三杀..."连杀称号，随时间上浮
static void draw_kill_feedback(Context* ctx, render_mesh_p q)
{
	uint64_t now = get_ticks();
	for (auto& ke : ctx->kill_events) {
		if (now - ke.time_ms > 2000) continue;
		const char* label = "击杀 +1";
		if (ke.combo >= 5)      label = "五杀!";
		else if (ke.combo >= 4) label = "四杀!";
		else if (ke.combo >= 3) label = "三杀!";
		else if (ke.combo >= 2) label = "双杀!";
		float t = (float)(now - ke.time_ms) / 1000.0f;
		float y = 150.0f - t * 30.0f;
		uint8_t cr = 40, cg = 40, cb = 45;
		if (ke.combo >= 3) { cr = 200; cg = 60; cb = 50; }
		else if (ke.combo == 2) { cr = 60; cg = 130; cb = 220; }
		r3d_font_draw_2d_color(ctx->r3d, q, label, 240.0f - 36.0f, y, 26.0f, 480.0f, 320.0f, cr, cg, cb);
		break;   // 只显示最近一次
	}
}

void debug_layer_render_hud(Context* ctx, render_mesh_p quad)
{
	if (!ctx || !ctx->r3d) return;

	static bool  g_font_ok = false;
	static bool  g_font_tried = false;
	static render_mesh_p g_quad = NULL;
	static uint64_t g_fps_last = 0;
	static int   g_fps_count = 0;
	static int   g_fps = 0;
	// 战况文本：每帧都绘制（保证持久显示不闪烁），仅间隔刷新以限制字体重烘焙+纹理上传频率
	static char   g_battle_text[96] = "";
	static uint64_t g_battle_last = 0;
	static const uint64_t g_battle_interval = 250; // ms
	// 波次面板换波高亮动画
	static int      g_last_wave = 0;
	static uint64_t g_wave_anim_start = 0;

	if (!g_font_tried) {
		g_font_tried = true;
		if (!r3d_font_init(ctx->r3d, "fonts/simhei.ttf", 48) &&
			!r3d_font_init(ctx->r3d, "fonts/arial.ttf", 48)) {
			log_info("debug_layer: font init failed (FPS HUD disabled)");
		} else {
			g_font_ok = true;
		}
	}

	{
		uint64_t now = get_ticks();
		g_fps_count++;
		if (g_fps_last == 0) g_fps_last = now;
		if (now - g_fps_last >= 500) {
			g_fps = (int)(g_fps_count * 1000 / (now - g_fps_last));
			g_fps_count = 0;
			g_fps_last = now;
		}
	}

	render_mesh_p q = quad ? quad : g_quad;

	// 调试信息（F1 开关，仿 joy_topdown_template 的 frame_display_pass）：
	// 一行显示 帧号 / 逻辑帧率 / 敌人数 / 全局实体 id，1px 白色描边保证在任意背景可读。
	if (ctx->show_debug_hud && g_font_ok && q) {
		auto& gs = ctx->ecs.ctx().get<GlobalState>();
		int enemy_count = 0;
		for (auto e : ctx->ecs.view<EnemyComponent>()) { (void)e; enemy_count++; }
		char dbg[128];
		snprintf(dbg, sizeof(dbg), "FPS: %d  Frame: %d  LogicFPS: %d  Enemies: %d  EID: %d",
				 g_fps, gs.next_frame_id, ctx->logic_fps, enemy_count, gs.next_entity_id);
		r3d_font_draw_2d_outline(ctx->r3d, q, dbg, 8.0f, 6.0f, 16.0f, 480.0f, 320.0f,
								 60, 60, 70, 255, 255, 255, 1.0f);
	}

	// 本地玩家血量条（左上角 FPS 下方）
	if (q && ctx->local_conv > 0) {
		auto pv = ctx->ecs.view<PlayerComponent>();
		for (auto e : pv) {
			const PlayerComponent& pc = pv.get<PlayerComponent>(e);
			if (pc.conv != (int)ctx->local_conv) continue;
			if (!ctx->ecs.all_of<HealthComponent>(e)) break;
			const HealthComponent& h = ctx->ecs.get<HealthComponent>(e);
			float ratio = h.max_hp > 0 ? (float)h.hp / h.max_hp : 0.0f;
			if (ratio < 0.0f) ratio = 0.0f;
			if (ratio > 1.0f) ratio = 1.0f;

			const float bx = 10.0f, by = 40.0f, bw = 140.0f, bh = 12.0f;
			r3d_draw_rect_2d(ctx->r3d, q, bx, by, bw, bh, 200, 200, 205, 480.0f, 320.0f); // 底
			uint8_t hr = 40, hg = 200, hb = 60;
			if (ratio < 0.5f) { hr = 230; hg = 180; hb = 40; }
			if (ratio < 0.25f) { hr = 230; hg = 60; hb = 40; }
			if (ratio > 0.0f)
				r3d_draw_rect_2d(ctx->r3d, q, bx + 1.0f, by + 1.0f, (bw - 2.0f) * ratio, bh - 2.0f, hr, hg, hb, 480.0f, 320.0f);

			char hpTxt[64];
			snprintf(hpTxt, sizeof(hpTxt), "HP: %d/%d", h.hp, h.max_hp);
			r3d_font_draw_2d_color(ctx->r3d, q, hpTxt, bx, by + bh + 2.0f, 16.0f, 480.0f, 320.0f, 60, 60, 70);
			break;
		}
	}

	// 战况面板：波次 / 击杀 / 存活敌人（全局确定性数据，直接从 ECS 读取）
	if (q) {
		auto& gs = ctx->ecs.ctx().get<GlobalState>();
		int alive = 0;
		auto ev = ctx->ecs.view<EnemyComponent, HealthComponent>();
		for (auto e : ev)
			if (!ev.get<HealthComponent>(e).dead) alive++;

		// 换波高亮动画：wave 变化时短暂放大/变色
		uint64_t now_a = get_ticks();
		if (gs.wave != g_last_wave) {
			g_last_wave = gs.wave;
			g_wave_anim_start = now_a;
		}
		float anim = (float)(now_a - g_wave_anim_start) / 1000.0f;   // 换波后经过秒数
		bool wave_highlight = anim < 1.0f;
		float wave_scale = wave_highlight ? (1.0f + 0.3f * (1.0f - anim)) : 1.0f;

		// 战况文字（水墨风：墨色 + 宣纸色描边，无背景色块）
		const float px = 10.0f, py = 70.0f;

		// 关卡大字（墨色）
		char wTxt[32];
		snprintf(wTxt, sizeof(wTxt), "第 %d 关", gs.level);
		r3d_font_draw_2d_outline(ctx->r3d, q, wTxt, px, py, 20.0f, 480.0f, 320.0f,
			40, 40, 45, 248, 246, 240, 1.5f);

		// Kill / Enemies 小字
		char kTxt[64], eTxt[64];
		snprintf(kTxt, sizeof(kTxt), "Kill %d", gs.kills);
		snprintf(eTxt, sizeof(eTxt), "Enemies %d", alive);
		r3d_font_draw_2d_outline(ctx->r3d, q, kTxt, px, py + 24.0f, 14.0f, 480.0f, 320.0f,
			60, 60, 70, 248, 246, 240, 1.5f);
		r3d_font_draw_2d_outline(ctx->r3d, q, eTxt, px + 80.0f, py + 24.0f, 14.0f, 480.0f, 320.0f,
			60, 60, 70, 248, 246, 240, 1.5f);

		// 本地玩家重生倒计时（死亡期间显示）
		if (ctx->local_conv > 0) {
			auto pv2 = ctx->ecs.view<PlayerComponent>();
			for (auto e : pv2) {
				const PlayerComponent& pc = pv2.get<PlayerComponent>(e);
				if (pc.conv != (int)ctx->local_conv) continue;
				if (!ctx->ecs.all_of<HealthComponent>(e)) break;
				const HealthComponent& h = ctx->ecs.get<HealthComponent>(e);
				if (h.dead) {
					char rTxt[64];
					snprintf(rTxt, sizeof(rTxt), "ReSpawning in %.1fs",
						(h.respawn_timer > 0 ? (float)h.respawn_timer : 0.0f) / 20.0f);
					r3d_font_draw_2d_color(ctx->r3d, q, rTxt, 10.0f, 118.0f, 16.0f, 480.0f, 320.0f, 230, 60, 40);
				}
				break;
			}
		}
	}

	// 波次过渡提示（换波延迟生成期间）
	if (q) draw_wave_hint(ctx, q);

	// 击杀反馈（连杀称号）
	if (q) draw_kill_feedback(ctx, q);

	// 小地图（右下角）
	if (q) draw_minimap(ctx, q);
}
