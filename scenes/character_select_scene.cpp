#include "character_select_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "level_select_scene.h"
#include "loading_scene.h"
#include "main_menu_scene.h"
#include "online_scene.h"
#include "ui_particles.h"
#include "../shared/systems/character_def.h"

CharacterSelectScene::CharacterSelectScene(Context* ctx, GameMode mode)
    : Scene(ctx, "CharSelect"), target_mode(mode) {}

void CharacterSelectScene::HandleEvent(const void* ev) {
    jr3d_p r3d = ctx_->r3d;

    // 昵称输入框优先处理事件（文本输入、聚焦、退格等）
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
        // 角色卡片：完整点击（按下+松开均在卡内）→ 选中
        for (int i = 0; i < CHAR_COUNT; i++) {
            if (!self->btn_cards[i]) continue;
            bool clicked = self->btn_cards[i]->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_cards[i], x, y);
            r3d_button_release(r3d, self->btn_cards[i], x, y);
            if (clicked) self->selected = i;
        }
        // 确认：记录选中角色并进入 loading → 游戏
        if (self->btn_confirm) {
            bool clicked = self->btn_confirm->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_confirm, x, y);
            r3d_button_release(r3d, self->btn_confirm, x, y);
            if (clicked) {
                self->ctx_->selected_char_id = self->selected;
                // 昵称：去首部空白，空则用默认
                const char* nick = self->nickname_input ? r3d_input_get_text(self->nickname_input) : "";
                const char* s = nick;
                while (*s == ' ' || *s == '\t') s++;
                if (!*s) s = "玩家";
                snprintf(self->ctx_->selected_nickname, sizeof(self->ctx_->selected_nickname), "%s", s);
                // 客户端：关卡由房主全局状态统一下发，跳过选关页直接进 loading
                if (self->target_mode == MODE_CLIENT) {
                    self->ctx_->scene_mgr->change(new LoadingScene(self->ctx_, self->target_mode));
                    return;
                }
                // 单机/房主：先选地图关卡，再进 loading → 游戏
                self->ctx_->scene_mgr->change(new LevelSelectScene(self->ctx_, self->target_mode));
                return;
            }
        }
        // 返回：单机回主菜单，联机回联机菜单
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
        for (int i = 0; i < CHAR_COUNT; i++)
            if (self->btn_cards[i]) r3d_button_is_pressed(r3d, self->btn_cards[i], false, x, y);
        if (self->btn_confirm) r3d_button_is_pressed(r3d, self->btn_confirm, false, x, y);
        if (self->btn_back)    r3d_button_is_pressed(r3d, self->btn_back, false, x, y);
    };
    dispatch_event(cbs, ev, this);
}

void CharacterSelectScene::Load() {
    log_info("[character select scene] start...");

    ui_particles_init(&particles, 480, 320);

    // 3 张角色卡片（横向排列，逻辑坐标 480x320）
    const float cw = 140.0f, ch = 150.0f, gap = 15.0f;
    const float cx0 = (480.0f - (cw * CHAR_COUNT + gap * (CHAR_COUNT - 1))) / 2.0f;
    const float cy = 74.0f;
    for (int i = 0; i < CHAR_COUNT; i++) {
        btn_cards[i] = r3d_button_create(cx0 + i * (cw + gap), cy, cw, ch, "");
    }
    selected = (ctx_->selected_char_id >= 0 && ctx_->selected_char_id < CHAR_COUNT)
        ? ctx_->selected_char_id : 0;

    // 昵称输入框（标题下方、卡片上方，默认聚焦可直接输入）
    nickname_input = r3d_input_create(55.0f, 38.0f, 200.0f, 26.0f, 16.0f, 50, 50, 60, ctx_->selected_nickname);
    r3d_input_focus(nickname_input);
    r3d_input_start_text_input(ctx_->r3d, nickname_input);

    btn_confirm = r3d_button_create(180.0f, 244.0f, 120.0f, 45.0f, "开始游戏");
    btn_back    = r3d_button_create(15.0f,  244.0f, 80.0f,  45.0f, "返回");
}

void CharacterSelectScene::Update(float dt) {
    elapsed += dt;
    ui_particles_update(&particles, dt, 480, 320);
}

void CharacterSelectScene::draw_card(int i) {
    jr3d_p r3d = ctx_->r3d;
    render_mesh_p m = ctx_->ui_quad;
    const CharacterDef& cd = character_def(i);
    jr3d_button_p btn = btn_cards[i];
    if (!btn) return;
    float x = btn->x, y = btn->y, w = btn->w, h = btn->h;
    bool sel = (i == selected);

    // 卡片背景：角色色，选中加深
    Uint8 fr = cd.fill_r, fg = cd.fill_g, fb = cd.fill_b;
    if (sel) { fr = (Uint8)(fr * 0.72f); fg = (Uint8)(fg * 0.72f); fb = (Uint8)(fb * 0.72f); }
    r3d_draw_rect_2d(r3d, m, x, y, w, h, fr, fg, fb, 480.0f, 320.0f);

    // 边框：选中白粗边，未选中黑细边
    float bw = sel ? 4.0f : 2.0f;
    Uint8 br = sel ? 255 : 0, bg = sel ? 255 : 0, bb = sel ? 255 : 0;
    r3d_draw_rect_2d(r3d, m, x,         y,         w, bw, br, bg, bb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r3d, m, x,         y + h - bw, w, bw, br, bg, bb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r3d, m, x,         y,         bw, h, br, bg, bb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r3d, m, x + w - bw, y,         bw, h, br, bg, bb, 480.0f, 320.0f);

    // 角色名（顶部居中，白字）
    float nameW = 20.0f * (float)strlen(cd.name) * 0.5f;
    r3d_font_draw_2d_color(r3d, m, cd.name, x + (w - nameW) * 0.5f, y + 10.0f, 20.0f, 480.0f, 320.0f, 255, 255, 255);

    // 属性（白字小号）
    char buf[64];
    snprintf(buf, sizeof(buf), "血量 %d", cd.max_hp);
    r3d_font_draw_2d_color(r3d, m, buf, x + 12.0f, y + 46.0f, 12.0f, 480.0f, 320.0f, 255, 255, 255);
    snprintf(buf, sizeof(buf), "近战 %d", cd.melee_damage);
    r3d_font_draw_2d_color(r3d, m, buf, x + 12.0f, y + 66.0f, 12.0f, 480.0f, 320.0f, 255, 255, 255);
    snprintf(buf, sizeof(buf), "远程 %d", cd.bullet_damage);
    r3d_font_draw_2d_color(r3d, m, buf, x + 12.0f, y + 86.0f, 12.0f, 480.0f, 320.0f, 255, 255, 255);
    snprintf(buf, sizeof(buf), "移速 %.1f", fp_to_float(cd.move_speed));
    r3d_font_draw_2d_color(r3d, m, buf, x + 12.0f, y + 106.0f, 12.0f, 480.0f, 320.0f, 255, 255, 255);
}

void CharacterSelectScene::Render() {
    jr3d_p r3d = ctx_->r3d;

    int winW, winH;
    r3d_get_window_size(r3d, &winW, &winH);

    if (!r3d_begin(r3d)) return;

    // 3D 相机用于背景
    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);

    // 背景（白底蓝图风，与 game_scene 一致）
    vec3_t bg_pos = { 0, 0, 0 };
    vec3_t bg_scale = { (float)winW / (float)winH * 6.0f, 6.0f, 0.1f };
    r3d_draw_mesh(r3d, ctx_->ui_quad, bg_pos, bg_scale, jcolor_t{ 255, 255, 255, 255 });

    ui_particles_draw(r3d, ctx_->ui_quad, &particles, 480.0f, 320.0f);

    // 标题
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "选择角色", 180.0f, 10.0f, 24.0f, 480.0f, 320.0f, 60, 60, 70);

    // 昵称输入框（标题下方、卡片上方）
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "昵称", 15.0f, 48.0f, 16.0f, 480.0f, 320.0f, 50, 50, 60);
    if (nickname_input) r3d_input_draw(r3d, ctx_->ui_quad, nickname_input, 480.0f, 320.0f);

    // 角色卡片
    for (int i = 0; i < CHAR_COUNT; i++) draw_card(i);

    // 确认 / 返回按钮
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
