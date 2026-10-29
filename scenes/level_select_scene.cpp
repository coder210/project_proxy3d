#include "level_select_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include <cstring>
#include "character_select_scene.h"
#include "loading_scene.h"
#include "../shared/systems/level_maps.h"
#include "../shared/systems/character_def.h"

// ===== 版式（逻辑坐标 480x320，与模板选关页一致：墨带 + 左侧列表 + 右侧详情 + 底部按钮） =====
static const float BAND_H   = 36.0f;
static const float LIST_X   = 12.0f;
static const float LIST_Y   = 44.0f;
static const float LIST_W   = 200.0f;
static const float LIST_H   = 224.0f;
static const float PANEL_X  = 224.0f;
static const float PANEL_Y  = 44.0f;
static const float PANEL_W  = 244.0f;
static const float PANEL_H  = 224.0f;

static const Uint8 INK_R = 40,   INK_G = 40,   INK_B = 48;
static const Uint8 PAP_R = 245,  PAP_G = 243,  PAP_B = 237;
static const Uint8 BG_R  = 238,  BG_G  = 234,  BG_B  = 226;
static const Uint8 PNL_R = 250,  PNL_G = 248,  PNL_B = 242;
static const Uint8 BDR_R = 150,  BDR_G = 146,  BDR_B = 138;
static const Uint8 SEL_R = 38,   SEL_G = 38,   SEL_B = 46;
static const Uint8 ROW_R = 176,  ROW_G = 172,  ROW_B = 164;
static const Uint8 HV_R  = 120,  HV_G  = 120,  HV_B  = 128;
static const Uint8 RED_R = 200,  RED_G = 60,   RED_B = 50;
static const Uint8 WALL_R = 60,  WALL_G = 60,  WALL_B = 68;
static const Uint8 FLR_R = 232,  FLR_G = 230,  FLR_B = 224;

static void rect_border(jr3d_p r, render_mesh_p m, float x, float y, float w, float h,
                        Uint8 cr, Uint8 cg, Uint8 cb, float th) {
    r3d_draw_rect_2d(r, m, x,         y,         w, th, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x,         y + h - th, w, th, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x,         y,         th, h, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x + w - th, y,         th, h, cr, cg, cb, 480.0f, 320.0f);
}

// 取关卡 ASCII 网格某格字符（行宽 64 + 换行 65 字节）
static char grid_char(const char* src, int r, int c) {
    int rr = 0, cc = 0;
    for (const char* p = src; *p; ++p) {
        if (*p == '\n') { rr++; cc = 0; continue; }
        if (rr == r && cc == c) return *p;
        cc++;
    }
    return '#';
}

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
        for (int i = 0; i < LEVEL_COUNT; i++) {
            if (!self->btn_levels[i]) continue;
            bool clicked = self->btn_levels[i]->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_levels[i], x, y);
            r3d_button_release(r3d, self->btn_levels[i], x, y);
            if (clicked) self->selected = i;
        }
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
        self->hovered_ = -1;
        for (int i = 0; i < LEVEL_COUNT; i++) {
            if (!self->btn_levels[i]) continue;
            r3d_button_is_pressed(r3d, self->btn_levels[i], false, x, y);
            if (r3d_button_is_hovered(r3d, self->btn_levels[i], x, y)) self->hovered_ = i;
        }
        if (self->btn_confirm) r3d_button_is_pressed(r3d, self->btn_confirm, false, x, y);
        if (self->btn_back)    r3d_button_is_pressed(r3d, self->btn_back, false, x, y);
    };
    dispatch_event(cbs, ev, this);
}

void LevelSelectScene::Load() {
    log_info("[level select scene] start...");

    selected = (ctx_->selected_level >= 1 && ctx_->selected_level <= LEVEL_COUNT)
        ? ctx_->selected_level - 1 : 0;

    // 每关敌人出生点数量
    for (int i = 0; i < LEVEL_COUNT; i++) {
        int player_cell = -1, enemy_n = 0;
        int enemy_cells[WAVE_MAX_ENEMIES];
        PmLayout layout;
        level_build_layout(i + 1, &layout, player_cell, enemy_cells, enemy_n);
        enemy_cnt[i] = enemy_n;
    }

    // 粗采样每关墙位图（16x16）供右侧迷你地图
    for (int lv = 0; lv < LEVEL_COUNT; lv++) {
        const char* src = LEVEL_SRCS[lv];
        if (!src) continue;
        for (int gy = 0; gy < 16; gy++)
            for (int gx = 0; gx < 16; gx++)
                mini_wall_[lv][gy * 16 + gx] = (grid_char(src, gy * 4, gx * 4) == '#');
    }

    // 左侧关卡列表
    float pitch = LIST_H / (float)LEVEL_COUNT;
    float rowH = pitch - 6.0f;
    for (int i = 0; i < LEVEL_COUNT; i++)
        btn_levels[i] = r3d_button_create(LIST_X, LIST_Y + (float)i * pitch + 3.0f, LIST_W, rowH, "");

    btn_confirm = r3d_button_create(356.0f, 276.0f, 112.0f, 34.0f, "开始");
    btn_back    = r3d_button_create(12.0f,  276.0f, 96.0f,  34.0f, "返回");
}

void LevelSelectScene::Update(float dt) {
    (void)dt;
}

// 左侧列表中的一行：序号 + 名称 + 敌人数量
void LevelSelectScene::draw_level(int i) {
    jr3d_p r3d = ctx_->r3d;
    render_mesh_p m = ctx_->ui_quad;
    jr3d_button_p btn = btn_levels[i];
    if (!btn) return;
    float x = btn->x, y = btn->y, w = btn->w, h = btn->h;
    bool sel = (i == selected);
    bool hov = (i == hovered_);

    Uint8 br = sel ? SEL_R : (hov ? HV_R : ROW_R);
    Uint8 bg = sel ? SEL_G : (hov ? HV_G : ROW_G);
    Uint8 bb = sel ? SEL_B : (hov ? HV_B : ROW_B);
    r3d_draw_rect_2d(r3d, m, x, y, w, h, br, bg, bb, 480.0f, 320.0f);
    rect_border(r3d, m, x, y, w, h, BDR_R, BDR_G, BDR_B, 2.0f);
    if (sel)
        r3d_draw_rect_2d(r3d, m, x + 2.0f, y + 2.0f, 4.0f, h - 4.0f, RED_R, RED_G, RED_B, 480.0f, 320.0f);

    // 序号
    char num[8]; snprintf(num, sizeof(num), "%02d", i + 1);
    Uint8 nr = sel ? PAP_R : 130, ng = sel ? PAP_G : 130, nb = sel ? PAP_B : 138;
    r3d_font_draw_2d_color(r3d, m, num, x + 12.0f, y + (h - 16.0f) * 0.5f, 14.0f, 480.0f, 320.0f, nr, ng, nb);

    // 名称
    Uint8 tr = sel ? PAP_R : 28, tg = sel ? PAP_G : 28, tb = sel ? PAP_B : 32;
    r3d_font_draw_2d_color(r3d, m, LEVEL_META[i].name, x + 42.0f, y + (h - 18.0f) * 0.5f - 8.0f, 18.0f, 480.0f, 320.0f, tr, tg, tb);

    // 敌人数量
    char buf[32]; snprintf(buf, sizeof(buf), "敌人 %d", enemy_cnt[i]);
    r3d_font_draw_2d_color(r3d, m, buf, x + 42.0f, y + (h - 14.0f) * 0.5f + 8.0f, 12.0f, 480.0f, 320.0f, sel ? 200 : 110, sel ? 200 : 110, sel ? 200 : 118);
}

// 右侧详情面板：迷你地图（色块） + 名称 + 简介 + 敌人数
void LevelSelectScene::draw_preview(int index) {
    jr3d_p r3d = ctx_->r3d;
    render_mesh_p m = ctx_->ui_quad;
    float px = PANEL_X, py = PANEL_Y, pw = PANEL_W, ph = PANEL_H;

    r3d_draw_rect_2d(r3d, m, px, py, pw, ph, PNL_R, PNL_G, PNL_B, 480.0f, 320.0f);
    rect_border(r3d, m, px, py, pw, ph, BDR_R, BDR_G, BDR_B, 2.0f);

    // 迷你地图（粗采样墙位图 → 色块；缺纹理用色块，符合需求）
    float mx = px + 12.0f, my = py + 12.0f, mw = pw - 24.0f, mh = 104.0f;
    float cell = mw / 16.0f;
    for (int gy = 0; gy < 16; gy++)
        for (int gx = 0; gx < 16; gx++) {
            bool wall = mini_wall_[index][gy * 16 + gx];
            Uint8 cr = wall ? WALL_R : FLR_R, cg = wall ? WALL_G : FLR_G, cb = wall ? WALL_B : FLR_B;
            r3d_draw_rect_2d(r3d, m, mx + (float)gx * cell, my + (float)gy * cell,
                             cell + 0.5f, cell + 0.5f, cr, cg, cb, 480.0f, 320.0f);
        }
    rect_border(r3d, m, mx, my, mw, mh, BDR_R, BDR_G, BDR_B, 1.5f);

    // 名称 + 简介 + 敌人
    r3d_font_draw_2d_color(r3d, m, LEVEL_META[index].name, px + 12.0f, py + 126.0f, 20.0f, 480.0f, 320.0f, 28, 28, 32);
    r3d_font_draw_2d_color(r3d, m, LEVEL_META[index].desc, px + 12.0f, py + 150.0f, 12.0f, 480.0f, 320.0f, 90, 90, 98);
    char buf[48]; snprintf(buf, sizeof(buf), "敌人出生点  %d", enemy_cnt[index]);
    r3d_font_draw_2d_color(r3d, m, buf, px + 12.0f, py + 172.0f, 13.0f, 480.0f, 320.0f, 90, 90, 98);
}

void LevelSelectScene::Render() {
    jr3d_p r3d = ctx_->r3d;
    if (!r3d_begin(r3d)) return;

    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);

    r3d_draw_rect_2d(r3d, ctx_->ui_quad, 0, 0, 480.0f, 320.0f, BG_R, BG_G, BG_B, 480.0f, 320.0f);

    // 顶部墨色标题带
    r3d_draw_rect_2d(r3d, ctx_->ui_quad, 0, 0, 480.0f, BAND_H, INK_R, INK_G, INK_B, 480.0f, 320.0f);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "选择关卡", 16.0f, 6.0f, 24.0f, 480.0f, 320.0f, PAP_R, PAP_G, PAP_B);

    // 当前角色（标题带右侧）；越界时回退到 0 号，避免越界访问
    int cid = ctx_->selected_char_id;
    if (cid < 0 || cid >= CHAR_COUNT) cid = 0;
    const char* chName = character_def(cid).name;
    char chBuf[64]; snprintf(chBuf, sizeof(chBuf), "当前角色：%s", chName);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, chBuf, 230.0f, 9.0f, 16.0f, 480.0f, 320.0f, 200, 200, 208);

    for (int i = 0; i < LEVEL_COUNT; i++) draw_level(i);

    draw_preview(selected);

    r3d_button_draw(r3d, ctx_->ui_quad, btn_confirm, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_back, 480.0f, 320.0f);

    r3d_end(r3d);
}

void LevelSelectScene::Destroy() {
    for (int i = 0; i < LEVEL_COUNT; i++)
        if (btn_levels[i]) r3d_button_destroy(btn_levels[i]);
    if (btn_confirm) r3d_button_destroy(btn_confirm);
    if (btn_back)    r3d_button_destroy(btn_back);
}
