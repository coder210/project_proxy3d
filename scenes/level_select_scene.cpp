#include "level_select_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include "character_select_scene.h"
#include "loading_scene.h"
#include "ui_particles.h"
#include "../shared/systems/level_maps.h"

// 每关展示信息（关卡数应与 LEVEL_COUNT 一致）
struct LevelMeta { const char* name; const char* desc; };
static const LevelMeta LEVEL_META[LEVEL_COUNT] = {
    { "训练场",   "开阔立柱，练手入门" },
    { "回廊",     "立柱渐多，新手进阶" },
    { "军营",     "障碍密集，节奏加快" },
    { "绝地",     "高难绝地，背水一战" },
    { "迷宫",     "之字回廊，步步深入" },
};

LevelSelectScene::LevelSelectScene(Context* ctx, GameMode mode)
    : Scene(ctx, "LevelSelect"), target_mode(mode) {}

void LevelSelectScene::HandleEvent(const void* ev) {
    jevent_callbacks_t cbs = {};
    cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
        (void)btn;
        LevelSelectScene* self = (LevelSelectScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        for (int i = 0; i < LEVEL_COUNT; i++)
            if (self->btn_levels[i]) r3d_button_is_pressed(r3d, self->btn_levels[i], true, x, y);
        if (self->btn_confirm) r3d_button_is_pressed(r3d, self->btn_confirm, true, x, y);
        if (self->btn_back)    r3d_button_is_pressed(r3d, self->btn_back, true, x, y);
    };
    cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
        (void)btn;
        LevelSelectScene* self = (LevelSelectScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        // 关卡按钮：完整点击（按下+松开都在按钮内）→ 选中
        for (int i = 0; i < LEVEL_COUNT; i++) {
            if (!self->btn_levels[i]) continue;
            bool clicked = self->btn_levels[i]->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_levels[i], x, y);
            r3d_button_release(r3d, self->btn_levels[i], x, y);
            if (clicked) self->selected = i;
        }
        // 确认：记录选定关卡并进入 loading → 游戏
        if (self->btn_confirm) {
            bool clicked = self->btn_confirm->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_confirm, x, y);
            r3d_button_release(r3d, self->btn_confirm, x, y);
            if (clicked) {
                self->ctx_->selected_level = self->selected + 1;
                self->ctx_->scene_mgr->change(new LoadingScene(self->ctx_, self->target_mode));
                return;
            }
        }
        // 返回：回角色选择页
        if (self->btn_back) {
            bool clicked = self->btn_back->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_back, x, y);
            r3d_button_release(r3d, self->btn_back, x, y);
            if (clicked) {
                self->ctx_->scene_mgr->change(new CharacterSelectScene(self->ctx_, self->target_mode));
            }
        }
    };
    cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
        (void)dx; (void)dy;
        LevelSelectScene* self = (LevelSelectScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        for (int i = 0; i < LEVEL_COUNT; i++)
            if (self->btn_levels[i]) r3d_button_is_pressed(r3d, self->btn_levels[i], false, x, y);
        if (self->btn_confirm) r3d_button_is_pressed(r3d, self->btn_confirm, false, x, y);
        if (self->btn_back)    r3d_button_is_pressed(r3d, self->btn_back, false, x, y);
    };
    dispatch_event(cbs, ev, this);
}

void LevelSelectScene::Load() {
    log_info("[level select scene] start...");

    ui_particles_init(&particles, 480, 320);
    selected = (ctx_->selected_level >= 1 && ctx_->selected_level <= LEVEL_COUNT)
        ? ctx_->selected_level - 1 : 0;

    // 统计每关敌人出生点数量（复用确定解析，不依赖随机流）
    for (int i = 0; i < LEVEL_COUNT; i++) {
        int player_cell = -1, enemy_n = 0;
        int enemy_cells[WAVE_MAX_ENEMIES];
        PmLayout layout;
        level_build_layout(i + 1, &layout, player_cell, enemy_cells, enemy_n);
        enemy_cnt[i] = enemy_n;
    }

    // 关卡按钮（纵向排列，逻辑坐标 480x320）
    const float bw = 190.0f, bh = 42.0f, gap = 8.0f;
    const float total = bh * LEVEL_COUNT + gap * (LEVEL_COUNT - 1);
    const float bx = (480.0f - bw) * 0.5f;
    const float by0 = 40.0f + (120.0f - total) * 0.5f;
    for (int i = 0; i < LEVEL_COUNT; i++)
        btn_levels[i] = r3d_button_create(bx, by0 + i * (bh + gap), bw, bh, "");

    btn_confirm = r3d_button_create(300.0f, 250.0f, 150.0f, 50.0f, "开始");
    btn_back    = r3d_button_create(30.0f,  250.0f, 90.0f,  50.0f, "返回");
}

void LevelSelectScene::Update(float dt) {
    elapsed += dt;
    ui_particles_update(&particles, dt, 480, 320);
}

void LevelSelectScene::draw_level(int i) {
    jr3d_p r3d = ctx_->r3d;
    render_mesh_p m = ctx_->ui_quad;
    jr3d_button_p btn = btn_levels[i];
    if (!btn) return;
    float x = btn->x, y = btn->y, w = btn->w, h = btn->h;
    bool sel = (i == selected);

    // 背景：未选浅灰，选中加深
    if (sel) r3d_draw_rect_2d(r3d, m, x, y, w, h, 96, 96, 104, 480.0f, 320.0f);
    else     r3d_draw_rect_2d(r3d, m, x, y, w, h, 220, 220, 226, 480.0f, 320.0f);

    // 边框：选中白粗边，未选中灰细边
    float bw = sel ? 4.0f : 2.0f;
    Uint8 br = sel ? 255 : 170, bg = br, bb = br;
    r3d_draw_rect_2d(r3d, m, x,         y,         w, bw, br, bg, bb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r3d, m, x,         y + h - bw, w, bw, br, bg, bb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r3d, m, x,         y,         bw, h, br, bg, bb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r3d, m, x + w - bw, y,         bw, h, br, bg, bb, 480.0f, 320.0f);

    // 文本：第 N 关 · 名称（白字，选中加深对比）
    char buf[64];
    snprintf(buf, sizeof(buf), "第 %d 关 · %s", i + 1, LEVEL_META[i].name);
    r3d_font_draw_2d_color(r3d, m, buf, x + 10.0f, y + 6.0f, 16.0f, 480.0f, 320.0f, sel ? 255 : 40, sel ? 255 : 40, sel ? 255 : 40);
    // 描述 + 敌人出生点数量（小字灰）
    snprintf(buf, sizeof(buf), "%s  敌人出生点 %d", LEVEL_META[i].desc, enemy_cnt[i]);
    r3d_font_draw_2d_color(r3d, m, buf, x + 10.0f, y + 24.0f, 11.0f, 480.0f, 320.0f, sel ? 220 : 110, sel ? 220 : 110, sel ? 220 : 110);
}

void LevelSelectScene::Render() {
    jr3d_p r3d = ctx_->r3d;

    int winW, winH;
    r3d_get_window_size(r3d, &winW, &winH);

    if (!r3d_begin(r3d)) return;

    // 背景相机 + 白底蓝图风（与其它场景一致）
    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);
    vec3_t bg_pos = { 0, 0, 0 };
    vec3_t bg_scale = { (float)winW / (float)winH * 6.0f, 6.0f, 0.1f };
    r3d_draw_mesh(r3d, ctx_->ui_quad, bg_pos, bg_scale, jcolor_t{ 255, 255, 255, 255 });
    ui_particles_draw(r3d, ctx_->ui_quad, &particles, 480.0f, 320.0f);

    // 标题
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "选择关卡", 180.0f, 10.0f, 24.0f, 480.0f, 320.0f, 60, 60, 70);

    for (int i = 0; i < LEVEL_COUNT; i++) draw_level(i);

    r3d_button_draw(r3d, ctx_->ui_quad, btn_confirm, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_back,    480.0f, 320.0f);

    r3d_end(r3d);
}

void LevelSelectScene::Destroy() {
    for (int i = 0; i < LEVEL_COUNT; i++)
        if (btn_levels[i]) r3d_button_destroy(btn_levels[i]);
    if (btn_confirm) r3d_button_destroy(btn_confirm);
    if (btn_back)    r3d_button_destroy(btn_back);
}
