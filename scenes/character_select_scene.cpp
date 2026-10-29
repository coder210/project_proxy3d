#include "character_select_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "level_select_scene.h"
#include "loading_scene.h"
#include "main_menu_scene.h"
#include "online_scene.h"
#include "../shared/systems/character_def.h"

// ===== 版式（逻辑坐标 480x320，与模板选角页一致：墨带 + 左侧列表 + 右侧详情 + 底部按钮） =====
static const float BAND_H   = 36.0f;
static const float LIST_X   = 12.0f;
static const float LIST_Y   = 44.0f;
static const float LIST_W   = 150.0f;
static const float LIST_H   = 224.0f;
static const float PANEL_X  = 172.0f;
static const float PANEL_Y  = 44.0f;
static const float PANEL_W  = 296.0f;
static const float PANEL_H  = 224.0f;

// 配色（复刻模板的墨/纸风格）
static const Uint8 INK_R = 40,   INK_G = 40,   INK_B = 48;
static const Uint8 PAP_R = 245,  PAP_G = 243,  PAP_B = 237;
static const Uint8 BG_R  = 238,  BG_G  = 234,  BG_B  = 226;
static const Uint8 PNL_R = 250,  PNL_G = 248,  PNL_B = 242;
static const Uint8 BDR_R = 150,  BDR_G = 146,  BDR_B = 138;
static const Uint8 SEL_R = 38,   SEL_G = 38,   SEL_B = 46;
static const Uint8 ROW_R = 176,  ROW_G = 172,  ROW_B = 164;
static const Uint8 HV_R  = 120,  HV_G  = 120,  HV_B  = 128;
static const Uint8 RED_R = 200,  RED_G = 60,   RED_B = 50;

// 画矩形边框（r3d_draw_rect_2d 无 alpha，故用实色描边）
static void rect_border(jr3d_p r, render_mesh_p m, float x, float y, float w, float h,
                        Uint8 cr, Uint8 cg, Uint8 cb, float th) {
    r3d_draw_rect_2d(r, m, x,         y,         w, th, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x,         y + h - th, w, th, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x,         y,         th, h, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x + w - th, y,         th, h, cr, cg, cb, 480.0f, 320.0f);
}

CharacterSelectScene::CharacterSelectScene(Context* ctx, GameMode mode)
    : Scene(ctx, "CharSelect"), target_mode(mode) {}

void CharacterSelectScene::HandleEvent(const void* ev) {
    jr3d_p r3d = ctx_->r3d;

    const SDL_Event* sdle = (const SDL_Event*)ev;
    if (nickname_input) r3d_input_handle_event(ctx_->r3d, nickname_input, (SDL_Event*)sdle);

    jevent_callbacks_t cbs = {};
    cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
        (void)btn;
        CharacterSelectScene* self = (CharacterSelectScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        for (int i = 0; i < CHAR_COUNT; i++)
            if (self->btn_cards[i]) r3d_button_is_pressed(r3d, self->btn_cards[i], true, x, y);
        if (self->btn_confirm) r3d_button_is_pressed(r3d, self->btn_confirm, true, x, y);
        if (self->btn_back)    r3d_button_is_pressed(r3d, self->btn_back, true, x, y);
    };
    cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
        (void)btn;
        CharacterSelectScene* self = (CharacterSelectScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        for (int i = 0; i < CHAR_COUNT; i++) {
            if (!self->btn_cards[i]) continue;
            bool clicked = self->btn_cards[i]->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_cards[i], x, y);
            r3d_button_release(r3d, self->btn_cards[i], x, y);
            if (clicked) self->selected = i;
        }
        if (self->btn_confirm) {
            bool clicked = self->btn_confirm->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_confirm, x, y);
            r3d_button_release(r3d, self->btn_confirm, x, y);
            if (clicked) {
                self->ctx_->selected_char_id = self->selected;
                const char* nick = self->nickname_input ? r3d_input_get_text(self->nickname_input) : "";
                const char* s = nick;
                while (*s == ' ' || *s == '\t') s++;
                if (!*s) s = "玩家";
                snprintf(self->ctx_->selected_nickname, sizeof(self->ctx_->selected_nickname), "%s", s);
                // 客户端：关卡由房主统一下发，跳过选关页直接进 loading
                if (self->target_mode == MODE_CLIENT) {
                    self->ctx_->scene_mgr->change(new LoadingScene(self->ctx_, self->target_mode));
                    return;
                }
                self->ctx_->scene_mgr->change(new LevelSelectScene(self->ctx_, self->target_mode));
                return;
            }
        }
        if (self->btn_back) {
            bool clicked = self->btn_back->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_back, x, y);
            r3d_button_release(r3d, self->btn_back, x, y);
            if (clicked) {
                if (self->target_mode == MODE_SINGLE)
                    self->ctx_->scene_mgr->change(new MainMenuScene(self->ctx_));
                else
                    self->ctx_->scene_mgr->change(new OnlineScene(self->ctx_));
            }
        }
    };
    cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
        (void)dx; (void)dy;
        CharacterSelectScene* self = (CharacterSelectScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        self->hovered_ = -1;
        for (int i = 0; i < CHAR_COUNT; i++) {
            if (!self->btn_cards[i]) continue;
            r3d_button_is_pressed(r3d, self->btn_cards[i], false, x, y);
            if (r3d_button_is_hovered(r3d, self->btn_cards[i], x, y)) self->hovered_ = i;
        }
        if (self->btn_confirm) r3d_button_is_pressed(r3d, self->btn_confirm, false, x, y);
        if (self->btn_back)    r3d_button_is_pressed(r3d, self->btn_back, false, x, y);
    };
    dispatch_event(cbs, ev, this);
}

void CharacterSelectScene::Load() {
    log_info("[character select scene] start...");

    selected = (ctx_->selected_char_id >= 0 && ctx_->selected_char_id < CHAR_COUNT)
        ? ctx_->selected_char_id : 0;

    // 左侧角色列表（每行一个角色，CHAR_COUNT 少无需滚动）
    float pitch = LIST_H / (float)CHAR_COUNT;
    float rowH = pitch - 8.0f;
    for (int i = 0; i < CHAR_COUNT; i++)
        btn_cards[i] = r3d_button_create(LIST_X, LIST_Y + (float)i * pitch + 4.0f, LIST_W, rowH, "");

    // 昵称输入框（顶部右侧，墨带内）
    nickname_input = r3d_input_create(300.0f, 6.0f, 166.0f, 24.0f, 16.0f, 50, 50, 60, ctx_->selected_nickname);
    r3d_input_focus(nickname_input);
    r3d_input_start_text_input(ctx_->r3d, nickname_input);

    btn_confirm = r3d_button_create(352.0f, 276.0f, 116.0f, 34.0f, "开始游戏");
    btn_back    = r3d_button_create(12.0f,  276.0f, 96.0f,  34.0f, "返回");
}

void CharacterSelectScene::Update(float dt) {
    (void)dt;
}

// 左侧列表中的一行：名称 + 角色主色块
void CharacterSelectScene::draw_card(int i) {
    jr3d_p r3d = ctx_->r3d;
    render_mesh_p m = ctx_->ui_quad;
    const CharacterDef& cd = character_def(i);
    jr3d_button_p btn = btn_cards[i];
    if (!btn) return;
    float x = btn->x, y = btn->y, w = btn->w, h = btn->h;
    bool sel = (i == selected);
    bool hov = (i == hovered_);

    Uint8 br = sel ? SEL_R : (hov ? HV_R : ROW_R);
    Uint8 bg = sel ? SEL_G : (hov ? HV_G : ROW_G);
    Uint8 bb = sel ? SEL_B : (hov ? HV_B : ROW_B);
    r3d_draw_rect_2d(r3d, m, x, y, w, h, br, bg, bb, 480.0f, 320.0f);
    rect_border(r3d, m, x, y, w, h, BDR_R, BDR_G, BDR_B, 2.0f);
    if (sel) // 选中红条
        r3d_draw_rect_2d(r3d, m, x + 2.0f, y + 2.0f, 4.0f, h - 4.0f, RED_R, RED_G, RED_B, 480.0f, 320.0f);

    // 名称（左对齐，避免中文按字节居中算错）
    Uint8 nr = sel ? PAP_R : 28, ng = sel ? PAP_G : 28, nb = sel ? PAP_B : 32;
    r3d_font_draw_2d_color(r3d, m, cd.name, x + 12.0f, y + (h - 18.0f) * 0.5f, 18.0f, 480.0f, 320.0f, nr, ng, nb);

    // 角色主色块
    float sw = 12.0f, sx = x + w - sw - 10.0f, sy = y + (h - sw) * 0.5f;
    r3d_draw_rect_2d(r3d, m, sx, sy, sw, sw, cd.fill_r, cd.fill_g, cd.fill_b, 480.0f, 320.0f);
    rect_border(r3d, m, sx, sy, sw, sw, cd.stroke_r, cd.stroke_g, cd.stroke_b, 1.5f);
}

// 右侧详情面板：大立绘（色块占位） + 名称 + 属性
void CharacterSelectScene::draw_preview(int index) {
    jr3d_p r3d = ctx_->r3d;
    render_mesh_p m = ctx_->ui_quad;
    const CharacterDef& cd = character_def(index);
    float px = PANEL_X, py = PANEL_Y, pw = PANEL_W, ph = PANEL_H;

    r3d_draw_rect_2d(r3d, m, px, py, pw, ph, PNL_R, PNL_G, PNL_B, 480.0f, 320.0f);
    rect_border(r3d, m, px, py, pw, ph, BDR_R, BDR_G, BDR_B, 2.0f);

    // 大立绘占位（角色主色块，缺纹理→色块，符合需求）
    float vx = px + 12.0f, vy = py + 10.0f, vw = pw - 24.0f, vh = 104.0f;
    r3d_draw_rect_2d(r3d, m, vx, vy, vw, vh, cd.fill_r, cd.fill_g, cd.fill_b, 480.0f, 320.0f);
    rect_border(r3d, m, vx, vy, vw, vh, cd.stroke_r, cd.stroke_g, cd.stroke_b, 2.0f);

    // 名称
    r3d_font_draw_2d_color(r3d, m, cd.name, px + 12.0f, py + 124.0f, 22.0f, 480.0f, 320.0f, 28, 28, 32);

    // 属性
    char buf[64];
    float ay = py + 154.0f; float step = 14.0f;
    snprintf(buf, sizeof(buf), "血量  %d", cd.max_hp);
    r3d_font_draw_2d_color(r3d, m, buf, px + 12.0f, ay, 13.0f, 480.0f, 320.0f, 90, 90, 98);
    snprintf(buf, sizeof(buf), "近战  %d", cd.melee_damage);
    r3d_font_draw_2d_color(r3d, m, buf, px + 12.0f, ay + step, 13.0f, 480.0f, 320.0f, 90, 90, 98);
    snprintf(buf, sizeof(buf), "远程  %d", cd.bullet_damage);
    r3d_font_draw_2d_color(r3d, m, buf, px + 12.0f, ay + step * 2.0f, 13.0f, 480.0f, 320.0f, 90, 90, 98);
    snprintf(buf, sizeof(buf), "射线  %d", cd.ray_damage);
    r3d_font_draw_2d_color(r3d, m, buf, px + 12.0f, ay + step * 3.0f, 13.0f, 480.0f, 320.0f, 90, 90, 98);
    snprintf(buf, sizeof(buf), "移速  %.1f", fp_to_float(cd.move_speed));
    r3d_font_draw_2d_color(r3d, m, buf, px + 12.0f, ay + step * 4.0f, 13.0f, 480.0f, 320.0f, 90, 90, 98);
}

void CharacterSelectScene::Render() {
    jr3d_p r3d = ctx_->r3d;
    if (!r3d_begin(r3d)) return;

    // 相机（保持原 2D 叠加渲染所需的投影设置）
    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);

    // 纸底
    r3d_draw_rect_2d(r3d, ctx_->ui_quad, 0, 0, 480.0f, 320.0f, BG_R, BG_G, BG_B, 480.0f, 320.0f);

    // 顶部墨色标题带
    r3d_draw_rect_2d(r3d, ctx_->ui_quad, 0, 0, 480.0f, BAND_H, INK_R, INK_G, INK_B, 480.0f, 320.0f);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "选择角色", 16.0f, 6.0f, 24.0f, 480.0f, 320.0f, PAP_R, PAP_G, PAP_B);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "昵称", 250.0f, 9.0f, 16.0f, 480.0f, 320.0f, PAP_R, PAP_G, PAP_B);
    if (nickname_input) r3d_input_draw(r3d, ctx_->ui_quad, nickname_input, 480.0f, 320.0f);

    // 左侧列表
    for (int i = 0; i < CHAR_COUNT; i++) draw_card(i);

    // 右侧详情
    draw_preview(selected);

    // 底部按钮
    r3d_button_draw(r3d, ctx_->ui_quad, btn_confirm, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_back, 480.0f, 320.0f);

    r3d_end(r3d);
}

void CharacterSelectScene::Destroy() {
    for (int i = 0; i < CHAR_COUNT; i++)
        if (btn_cards[i]) r3d_button_destroy(btn_cards[i]);
    if (btn_confirm) r3d_button_destroy(btn_confirm);
    if (btn_back)    r3d_button_destroy(btn_back);
    if (nickname_input) {
        r3d_input_stop_text_input(ctx_->r3d, nickname_input);
        r3d_input_destroy(nickname_input);
    }
}
