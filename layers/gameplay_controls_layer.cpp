#include "gameplay_controls_layer.h"
#include "../shared/components/player_component.h"
#include "../shared/components/action_state_component.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

gameplay_controls_layer_p g_gameplay_controls = NULL;

// 本地玩家的攻击/射击冷却剩余比例 (0..1 = 剩余/总量)。找不到本地玩家或未冷却返回 0。
// 冷却字段为逻辑档数(只读, 帧同步两端一致), 渲染端据此禁用按钮并画环形倒计时。
static float local_cool_ratio(Context* ctx, bool bullet)
{
	if (!ctx || ctx->local_conv == 0) return 0.0f;
	auto view = ctx->ecs.view<PlayerComponent, ActionStateComponent>();
	for (auto e : view) {
		auto& pc  = view.get<PlayerComponent>(e);
		if (pc.conv != (int)ctx->local_conv) continue;
		auto& act = view.get<ActionStateComponent>(e);
		if (bullet) {
			if (act.bullet_cooldown <= 0) return 0.0f;
			return (float)act.bullet_cooldown / (float)ActionStateComponent::BULLET_COOLDOWN;
		} else {
			if (act.melee_cooldown <= 0) return 0.0f;
			return (float)act.melee_cooldown / (float)ActionStateComponent::MELEE_COOLDOWN;
		}
	}
	return 0.0f;
}

// 本地玩家的攻击/射击冷却剩余 tick 数。未冷却返回 0。
static int local_cool_tick(Context* ctx, bool bullet)
{
	if (!ctx || ctx->local_conv == 0) return 0;
	auto view = ctx->ecs.view<PlayerComponent, ActionStateComponent>();
	for (auto e : view) {
		auto& pc  = view.get<PlayerComponent>(e);
		if (pc.conv != (int)ctx->local_conv) continue;
		auto& act = view.get<ActionStateComponent>(e);
		return bullet ? act.bullet_cooldown : act.melee_cooldown;
	}
	return 0;
}

// 本地玩家的射线冷却剩余 tick 数。未冷却返回 0。
static int local_cool_tick_ray(Context* ctx)
{
	if (!ctx || ctx->local_conv == 0) return 0;
	auto view = ctx->ecs.view<PlayerComponent, ActionStateComponent>();
	for (auto e : view) {
		auto& pc  = view.get<PlayerComponent>(e);
		if (pc.conv != (int)ctx->local_conv) continue;
		auto& act = view.get<ActionStateComponent>(e);
		return act.ray_cooldown;
	}
	return 0;
}

// 圆形冷却环 mesh：以原点为圆心、半径 R 的圆环扇区(R..R+thick), 从正上方(a=0)顺时针
// 填充前 frac 段, 与 r3d_button_draw_circle 的圆心/半径保持一致。坐标绝对像素。
static render_mesh_p make_cool_ring(Context* ctx, float R, float thick, float frac)
{
	constexpr int   SEGS_PER_TURN = 40;      // 整圆分段数(与 r3d_unit_disc 一致)
	float Ri = R, Ro = R + thick;
	int total  = SEGS_PER_TURN;
	int filled = (int)(frac * total);
	if (filled < 1) filled = 1;
	float verts[SEGS_PER_TURN * 6 * 5];
	int vi = 0;
	auto p2 = [](float a, float rr, float& x, float& y) { x = sinf(a) * rr; y = -cosf(a) * rr; };
	for (int k = 0; k < filled; k++) {
		float a0 = (float)k / total * 6.2831853f;
		float a1 = (float)(k + 1) / total * 6.2831853f;
		float o0x, o0y, o1x, o1y, i0x, i0y, i1x, i1y;
		p2(a0, Ro, o0x, o0y); p2(a1, Ro, o1x, o1y);
		p2(a0, Ri, i0x, i0y); p2(a1, Ri, i1x, i1y);
		// 四边形 o0-o1-i1-i0 拆两个三角
		verts[vi++]=o0x; verts[vi++]=o0y; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
		verts[vi++]=o1x; verts[vi++]=o1y; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
		verts[vi++]=i1x; verts[vi++]=i1y; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
		verts[vi++]=o0x; verts[vi++]=o0y; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
		verts[vi++]=i1x; verts[vi++]=i1y; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
		verts[vi++]=i0x; verts[vi++]=i0y; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
	}
	return r3d_mesh_custom(ctx->r3d, verts, vi / 5);
}

// 在圆形按钮外画圆形冷却进度环(深灰)：紧贴按钮黑描边外沿, 从正上方顺时针填充,
// 占比=已冷却进度。tick=剩余冷却 tick 数, max_tick=满冷却 tick 数; 冷却将尽(剩 1 tick)
// 时 fill=1 合围成整圆, 冷却结束(tick=0)不画。
static void draw_cool_ring(Context* ctx, jr3d_button_p btn, int tick, int max_tick)
{
	if (!btn || tick <= 0 || max_tick <= 1) return;
	constexpr int LEVELS = 40;               // 整圆远近量化档(存 mesh 复用)
	// 进度从 0 涨到 1: 剩 1 tick 时 fill=1 → 合围; 向上取整保证最后一帧画满档
	float fill = (float)(max_tick - tick) / (float)(max_tick - 1);
	if (fill <= 0.01f) return;
	int lvl = (int)(fill * LEVELS + 0.9999f);
	if (lvl > LEVELS) lvl = LEVELS;
	if (lvl < 1) lvl = 1;
	float R = (btn->w < btn->h ? btn->w : btn->h) * 0.5f + 3.0f;  // 覆盖按钮黑描边外沿
	float thick = 4.0f;
	static std::unordered_map<uint64_t, render_mesh_p> g_ring;
	uint64_t key = ((uint64_t)((uint16_t)(R * 10.0f)) << 16) | (uint16_t)lvl;
	auto it = g_ring.find(key);
	render_mesh_p m;
	if (it == g_ring.end()) {
		m = make_cool_ring(ctx, R, thick, (float)lvl / LEVELS);
		g_ring.emplace(key, m);
	} else {
		m = it->second;
	}
	if (!m) return;
	float cx = btn->x + btn->w * 0.5f;
	float cy = btn->y + btn->h * 0.5f;
	r3d_draw_mesh_2d(ctx->r3d, m, cx, cy, 1.0f, 1.0f,
					 30, 30, 35, 480.0f, 320.0f);
}

// 圆形按钮右下角叠加键盘键角标（方案 A）：在圆按钮直径的右下 1/4 处画一个小字母，
// 深色主字 + 白描边保证在按钮白底/冷却环上均清晰可读。纯表现层，不影响帧同步。
static void draw_key_corner(Context* ctx, jr3d_button_p btn, const char* key)
{
	if (!ctx || !ctx->r3d || !btn || !key || !key[0] || !ctx->ui_quad) return;
	render_mesh_p m = ctx->ui_quad;
	float R = (btn->w < btn->h ? btn->w : btn->h) * 0.5f;
	// 角标圆心：沿右下 45° 距按钮圆心约 R*0.68，往左上收拢一些
	float cx = btn->x + btn->w * 0.5f;
	float cy = btn->y + btn->h * 0.5f;
	float off = R * 0.68f;
	float bx = cx + off * 0.7071f;
	float by = cy + off * 0.7071f;
	float fontSize = R * 0.5f;
	// 用两字符宽度预判居中：单字母宽度≈fontSize/2，微调向左上让角标视觉居中
	float tw = fontSize * 0.5f;
	r3d_font_draw_2d_outline(ctx->r3d, m, key, bx - tw * 0.5f, by - fontSize * 0.5f,
							 fontSize, 480.0f, 320.0f,
							 40, 40, 45,      // 主色：深灰
							 255, 255, 255,   // 描边：白
							 1.5f);
}

// ---- 摇杆几何(逻辑 480x320, 参照 joy_topdown_template 的 syncLayout s=1) ----
// 底座半径 72, 圆心 (81.6, 224) 锚定左下角; 圆钮半径 30。
static const float JS_CX    = 81.6f;
static const float JS_CY    = 224.0f;
static const float JS_R     = 72.0f;
static const float JS_NUB_R = 30.0f;

// 单位圆盘(半径 0.5)网格, 贴图缺失时兜底绘制。进程内缓存一份。
static render_mesh_p g_js_disc = NULL;
static render_mesh_p get_js_disc(Context* ctx)
{
	if (!g_js_disc && ctx && ctx->r3d) {
		constexpr int SEG = 48;
		float verts[SEG * 3 * 5];
		int vi = 0;
		for (int i = 0; i < SEG; i++) {
			float a0 = (float)i / SEG * 6.2831853f;
			float a1 = (float)(i + 1) / SEG * 6.2831853f;
			float p0x = cosf(a0) * 0.5f, p0y = sinf(a0) * 0.5f;
			float p1x = cosf(a1) * 0.5f, p1y = sinf(a1) * 0.5f;
			verts[vi++]=0.0f; verts[vi++]=0.0f; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
			verts[vi++]=p0x; verts[vi++]=p0y; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
			verts[vi++]=p1x; verts[vi++]=p1y; verts[vi++]=0; verts[vi++]=0; verts[vi++]=0;
		}
		g_js_disc = r3d_mesh_custom(ctx->r3d, verts, vi / 5);
	}
	return g_js_disc;
}

struct gameplay_controls_layer {
	Context* ctx;
	// 左下摇杆
	float nub_dx, nub_dy;      // 圆钮相对底座中心的偏移(逻辑像素), 未推为 0
	bool  js_active;
	void* pad_tex;
	void* nub_tex;
	// 右下操作键
	jr3d_button_p btn_attack, btn_bullet, btn_ray;
};

// 由指针逻辑坐标更新摇杆: 夹紧偏移 -> 计算方向 -> 置/清四个方向位(八向, 含死区)。
// 每次调用先无条件清四个方向位再按当前向量置位, 保证方向实时跟随(与模板 updateStick 一致)。
static void js_update(gameplay_controls_layer_p self, float lx, float ly)
{
	if (!self || !self->ctx) return;
	float dx = lx - JS_CX;
	float dy = ly - JS_CY;
	float len = sqrtf(dx * dx + dy * dy);
	if (len > JS_R) { dx = dx / len * JS_R; dy = dy / len * JS_R; }  // 夹紧到底座半径
	self->nub_dx = dx;
	self->nub_dy = dy;

	int* mask = &self->ctx->current_input_mask;
	*mask &= ~(INPUT_UP | INPUT_DOWN | INPUT_LEFT | INPUT_RIGHT);

	float nx = (JS_R > 0) ? dx / JS_R : 0.0f;   // -1..1
	float ny = (JS_R > 0) ? dy / JS_R : 0.0f;
	float mag = sqrtf(nx * nx + ny * ny);
	if (mag > 0.25f) {
		// 屏幕 Y 向下: ny<0 = 上, ny>0 = 下
		if (ny < -0.35f) *mask |= INPUT_UP;
		if (ny >  0.35f) *mask |= INPUT_DOWN;
		if (nx < -0.35f) *mask |= INPUT_LEFT;
		if (nx >  0.35f) *mask |= INPUT_RIGHT;
	}
}

gameplay_controls_layer_p gameplay_controls_layer_create(Context* ctx)
{
	gameplay_controls_layer_p self = (gameplay_controls_layer_p)malloc(sizeof(*self));
	memset(self, 0, sizeof(*self));
	self->ctx = ctx;

	// 摇杆贴图(相对 exe 目录: CMake 把 assets/ui/* 拷到 <exe>/ui/*)
	int tw = 0, th = 0;
	self->pad_tex = r3d_load_texture_png(ctx->r3d, "ui/joystick_circle_pad_c.png", &tw, &th);
	self->nub_tex = r3d_load_texture_png(ctx->r3d, "ui/joystick_circle_nub_c.png", &tw, &th);
	log_info("[controls] joystick textures: pad=%p nub=%p", self->pad_tex, self->nub_tex);

	// 右下操作键: 攻击/射击/射线
	self->btn_attack = r3d_button_create(412, 254, 64, 64, "攻");   // 主攻击: 最靠右下角
	self->btn_bullet = r3d_button_create(340, 254, 58, 58, "射");   // 射击: 攻左下方
	self->btn_ray    = r3d_button_create(408, 176, 58, 58, "线");   // 射线(大招): 弧形顶端
	return self;
}

void gameplay_controls_layer_handle_event(gameplay_controls_layer_p self, const void* ev)
{
	if (!self) return;

	jevent_callbacks_t cbs = {};
	cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
		gameplay_controls_layer_p self = (gameplay_controls_layer_p)arg;
		Context* ctx = self->ctx;
		jr3d_p r3d = ctx->r3d;
		(void)btn;
		// 攻击/射击/射线: 冷却未到则点击无效(不注册 pressedInside)
		if (self->btn_attack) {
			if (local_cool_ratio(ctx, false) <= 0.0f)
				r3d_button_is_pressed(r3d, self->btn_attack, true, x, y);
		}
		if (self->btn_bullet) {
			if (local_cool_ratio(ctx, true) <= 0.0f)
				r3d_button_is_pressed(r3d, self->btn_bullet, true, x, y);
		}
		if (self->btn_ray) {
			if (local_ray_cooldown_ratio(ctx) <= 0.0f)
				r3d_button_is_pressed(r3d, self->btn_ray, true, x, y);
		}
		// 左摇杆: 命中底座圆内则激活并立即更新方向
		float lx = x, ly = y;
		r3d_window_to_logical(r3d, x, y, &lx, &ly);
		float dx = lx - JS_CX, dy = ly - JS_CY;
		float rr = JS_R * 1.15f;
		if (dx * dx + dy * dy <= rr * rr) {
			self->js_active = true;
			js_update(self, lx, ly);
		}
	};

	cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
		gameplay_controls_layer_p self = (gameplay_controls_layer_p)arg;
		Context* ctx = self->ctx;
		jr3d_p r3d = ctx->r3d;
		(void)btn;
		// 攻击：完整点击(按下+松开均在按钮内)且非冷却才触发一次
		if (self->btn_attack) {
			bool cool = local_cool_ratio(ctx, false) > 0.0f;
			bool full_click = !cool && self->btn_attack->pressedInside &&
				r3d_button_is_hovered(r3d, self->btn_attack, x, y);
			r3d_button_release(r3d, self->btn_attack, x, y);
			if (full_click) ctx->attack_triggered = true;
		}
		if (self->btn_bullet) {
			bool cool = local_cool_ratio(ctx, true) > 0.0f;
			bool full_click = !cool && self->btn_bullet->pressedInside &&
				r3d_button_is_hovered(r3d, self->btn_bullet, x, y);
			r3d_button_release(r3d, self->btn_bullet, x, y);
			if (full_click) ctx->bullet_triggered = true;
		}
		if (self->btn_ray) {
			bool cool = local_ray_cooldown_ratio(ctx) > 0.0f;
			bool full_click = !cool && self->btn_ray->pressedInside &&
				r3d_button_is_hovered(r3d, self->btn_ray, x, y);
			r3d_button_release(r3d, self->btn_ray, x, y);
			if (full_click) ctx->ray_triggered = true;
		}
		// 摇杆: 松开清四个方向位并回中(瞄准角在逻辑层锁存, 仍可朝最后方向开火)
		if (self->js_active) {
			self->js_active = false;
			ctx->current_input_mask &= ~(INPUT_UP | INPUT_DOWN | INPUT_LEFT | INPUT_RIGHT);
			self->nub_dx = 0.0f;
			self->nub_dy = 0.0f;
		}
	};

	cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
		gameplay_controls_layer_p self = (gameplay_controls_layer_p)arg;
		Context* ctx = self->ctx;
		jr3d_p r3d = ctx->r3d;
		(void)dx; (void)dy;
		if (self->btn_attack) r3d_button_is_pressed(r3d, self->btn_attack, false, x, y);
		if (self->btn_bullet) r3d_button_is_pressed(r3d, self->btn_bullet, false, x, y);
		if (self->btn_ray)    r3d_button_is_pressed(r3d, self->btn_ray,    false, x, y);
		// 拖动摇杆时实时更新方向
		if (self->js_active) {
			float lx = x, ly = y;
			r3d_window_to_logical(r3d, x, y, &lx, &ly);
			js_update(self, lx, ly);
		}
	};
	dispatch_event(cbs, ev, self);
}

void gameplay_controls_layer_render(gameplay_controls_layer_p self)
{
	if (!self || !self->ctx || !self->ctx->r3d) return;
	Context* ctx = self->ctx;
	jr3d_p r3d = ctx->r3d;
	render_mesh_p m = ctx->ui_quad;
	if (!m) return;

	// 键盘同步到摇杆圆钮(未拖拽时, 纯显示): 按 WASD 时圆钮偏向对应方向
	if (!self->js_active) {
		int kb = ctx->current_input_mask;
		float kx = 0.0f, ky = 0.0f;
		if (kb & INPUT_RIGHT) kx += 1.0f;
		if (kb & INPUT_LEFT)  kx -= 1.0f;
		if (kb & INPUT_DOWN)  ky += 1.0f;
		if (kb & INPUT_UP)    ky -= 1.0f;
		self->nub_dx = kx * JS_R * 0.55f;
		self->nub_dy = ky * JS_R * 0.55f;
	}

	// 摇杆底座(贴图; 缺纹理退化为圆盘)
	if (self->pad_tex) {
		r3d_draw_tex_2d(r3d, m, JS_CX - JS_R, JS_CY - JS_R, JS_R * 2.0f, JS_R * 2.0f,
						self->pad_tex, 480.0f, 320.0f);
	} else {
		render_mesh_p d = get_js_disc(ctx);
		if (d) r3d_draw_mesh_2d(r3d, d, JS_CX, JS_CY, JS_R * 2.0f, JS_R * 2.0f,
								235, 233, 226, 480.0f, 320.0f);
	}
	// 摇杆圆钮(跟随偏移)
	{
		float nx = JS_CX + self->nub_dx;
		float ny = JS_CY + self->nub_dy;
		if (self->nub_tex) {
			r3d_draw_tex_2d(r3d, m, nx - JS_NUB_R, ny - JS_NUB_R,
							JS_NUB_R * 2.0f, JS_NUB_R * 2.0f, self->nub_tex, 480.0f, 320.0f);
		} else {
			render_mesh_p d = get_js_disc(ctx);
			if (d) r3d_draw_mesh_2d(r3d, d, nx, ny, JS_NUB_R * 2.0f, JS_NUB_R * 2.0f,
									40, 40, 45, 480.0f, 320.0f);
		}
	}

	// 右下操作键(圆形)
	r3d_button_draw_circle(r3d, m, self->btn_attack, 480.0f, 320.0f);
	r3d_button_draw_circle(r3d, m, self->btn_bullet, 480.0f, 320.0f);
	r3d_button_draw_circle(r3d, m, self->btn_ray,    480.0f, 320.0f);
	// 冷却环
	draw_cool_ring(ctx, self->btn_attack, local_cool_tick(ctx, false), ActionStateComponent::MELEE_COOLDOWN);
	draw_cool_ring(ctx, self->btn_bullet, local_cool_tick(ctx, true), ActionStateComponent::BULLET_COOLDOWN);
	draw_cool_ring(ctx, self->btn_ray,    local_cool_tick_ray(ctx),  ActionStateComponent::RAY_COOLDOWN);
	// 键角标
	draw_key_corner(ctx, self->btn_attack, "J");
	draw_key_corner(ctx, self->btn_bullet, "K");
	draw_key_corner(ctx, self->btn_ray,    "L");
}

void gameplay_controls_layer_destroy(gameplay_controls_layer_p self)
{
	if (!self) return;
	// 退出时清掉四个方向位, 避免残留
	if (self->ctx)
		self->ctx->current_input_mask &= ~(INPUT_UP | INPUT_DOWN | INPUT_LEFT | INPUT_RIGHT);
	if (self->btn_attack) r3d_button_destroy(self->btn_attack);
	if (self->btn_bullet) r3d_button_destroy(self->btn_bullet);
	if (self->btn_ray)    r3d_button_destroy(self->btn_ray);
	free(self);
}
