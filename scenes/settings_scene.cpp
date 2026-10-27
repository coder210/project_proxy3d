#include "settings_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include <cstdlib>
#include "main_menu_scene.h"
#include "ui_particles.h"

SettingsScene::SettingsScene(Context* ctx) : Scene(ctx, "Settings") {}

// 把当前 UI 值同步到 settings 并自动保存
void SettingsScene::sync_settings() {
    Context* ctx = ctx_;
    bool changed = false;

    const char* ip = r3d_combobox_input_get_text(ip_input);
    if (strcmp(ip, ctx->settings.server_ip) != 0) {
        snprintf(ctx->settings.server_ip, sizeof(ctx->settings.server_ip), "%s", ip);
        changed = true;
    }

    int port = atoi(r3d_input_get_text(port_input));
    if (port != ctx->settings.server_port) {
        ctx->settings.server_port = port;
        changed = true;
    }

    float bgm = r3d_slider_get_value(bgm_slider);
    if (bgm != ctx->settings.bgm_volume) {
        ctx->settings.bgm_volume = bgm;
        if (ctx->bgm) audio_bgm_set_gain(ctx->bgm, bgm);
        changed = true;
    }

    float sfx = r3d_slider_get_value(sfx_slider);
    if (sfx != ctx->settings.sfx_volume) {
        ctx->settings.sfx_volume = sfx;
        if (ctx->step_sound) audio_shot_set_gain(ctx->step_sound, sfx);
        changed = true;
    }

    bool fs = (r3d_checkbox_get_state(fullscreen_cb) == JOY_R3D_CHECKBOX_CHECKED);
    if (fs != ctx->settings.fullscreen) {
        ctx->settings.fullscreen = fs;
        window_set_fullscreen(ctx->window, fs);
        changed = true;
    }

    if (changed) settings_save(&ctx->settings);
}

void SettingsScene::HandleEvent(const void* ev) {
    // 先让输入框处理事件（文本输入、聚焦等）
    const SDL_Event* sdle = (const SDL_Event*)ev;
    if (ip_input)   r3d_combobox_input_handle_event(ctx_->r3d, ip_input, (SDL_Event*)sdle);
    if (port_input) r3d_input_handle_event(ctx_->r3d, port_input, (SDL_Event*)sdle);

    jevent_callbacks_t cbs = {};
    cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
        (void)btn;
        SettingsScene* self = (SettingsScene*)arg;
        Context* ctx = self->ctx_;

        // 端口输入框点击聚焦（IP 下拉框由 combobox 自己处理）
        if (self->port_input && r3d_input_is_hovered(ctx->r3d, self->port_input, x, y))
            r3d_input_click(ctx->r3d, self->port_input, x, y);

        // 音量滑块（按下滑块或滑轨即更新值并进入拖动）
        if (self->bgm_slider) r3d_slider_is_pressed(ctx->r3d, self->bgm_slider, x, y);
        if (self->sfx_slider) r3d_slider_is_pressed(ctx->r3d, self->sfx_slider, x, y);

        // 全屏复选框（点击方框或标签即切换）
        if (self->fullscreen_cb) r3d_checkbox_is_pressed(ctx->r3d, self->fullscreen_cb, x, y);

        // 按钮
        if (self->btn_back && r3d_button_is_pressed(ctx->r3d, self->btn_back, true, x, y)) {
            push_user_event(SETTINGS_EVENT, SETTINGS_EVENT_BACK);
        }
    };
    cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
        (void)btn;
        SettingsScene* self = (SettingsScene*)arg;
        Context* ctx = self->ctx_;
        if (self->bgm_slider) r3d_slider_set_pressed(self->bgm_slider, false);
        if (self->sfx_slider) r3d_slider_set_pressed(self->sfx_slider, false);
        if (self->btn_back)       r3d_button_release(ctx->r3d, self->btn_back, x, y);
    };
    cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
        (void)dx; (void)dy;
        SettingsScene* self = (SettingsScene*)arg;
        Context* ctx = self->ctx_;
        if (self->bgm_slider) {
            r3d_slider_is_hovered(ctx->r3d, self->bgm_slider, x, y);
            r3d_slider_drag_move(ctx->r3d, self->bgm_slider, x, y);
        }
        if (self->sfx_slider) {
            r3d_slider_is_hovered(ctx->r3d, self->sfx_slider, x, y);
            r3d_slider_drag_move(ctx->r3d, self->sfx_slider, x, y);
        }
        if (self->fullscreen_cb) r3d_checkbox_is_hovered(ctx->r3d, self->fullscreen_cb, x, y);
        if (self->btn_back)       r3d_button_is_pressed(ctx->r3d, self->btn_back, false, x, y);
    };
    dispatch_event(cbs, ev, this);
}

void SettingsScene::Load() {
    Context* ctx = ctx_;
    log_info("[settings scene] start...");

    // 服务器 IP 可输入下拉列表框（自动获取本机 IP 填充列表）
    ip_input = r3d_combobox_input_create(120.0f, 50.0f, 200.0f, 24.0f, 16.0f, 50, 50, 60, ctx->settings.server_ip);
    char ips[16][JOY_MAX_IP];
    int ip_count = get_local_ips(ips, 16);
    for (int i = 0; i < ip_count; i++) {
        // 只提供 IPv4 地址：IPv6 链路本地地址无作用域 ID，服务端无法绑定监听
        if (strchr(ips[i], ':')) continue;
        r3d_combobox_input_add_item(ip_input, ips[i]);
    }

    // 端口输入框
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", ctx->settings.server_port);
    port_input = r3d_input_create(120.0f, 90.0f, 100.0f, 24.0f, 16.0f, 50, 50, 60, port_str);

    // 音量滑块
    bgm_slider = r3d_slider_create(120.0f, 130.0f, 200.0f, 14.0f, 0.0f, 1.0f, ctx->settings.bgm_volume);
    sfx_slider = r3d_slider_create(120.0f, 170.0f, 200.0f, 14.0f, 0.0f, 1.0f, ctx->settings.sfx_volume);
    r3d_slider_set_colors(bgm_slider, { 200, 200, 200, 255 }, { 50, 50, 60, 255 }, { 80, 80, 80, 255 }, { 100, 100, 100, 255 });
    r3d_slider_set_colors(sfx_slider, { 200, 200, 200, 255 }, { 50, 50, 60, 255 }, { 80, 80, 80, 255 }, { 100, 100, 100, 255 });

    // 全屏复选框
    fullscreen_cb = r3d_checkbox_create(120.0f, 212.0f, 20.0f, "全屏");
    r3d_checkbox_set_colors(fullscreen_cb, { 200, 200, 200, 255 }, { 50, 50, 60, 255 }, { 180, 180, 180, 255 });
    r3d_checkbox_set_state(fullscreen_cb,
        ctx->settings.fullscreen ? JOY_R3D_CHECKBOX_CHECKED : JOY_R3D_CHECKBOX_UNCHECKED);

    // 返回按钮
    btn_back = r3d_button_create(180.0f, 260.0f, 100.0f, 40.0f, "返回");

    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_init(&particles, 480, 320);
}

void SettingsScene::Update(float dt) {
    elapsed += dt;
    ui_particles_update(&particles, dt, 480, 320);
    sync_settings();
}

void SettingsScene::Render() {
    jr3d_p r3d = ctx_->r3d;

    int winW, winH;
    r3d_get_window_size(r3d, &winW, &winH);

    if (!r3d_begin(r3d)) return;

    // 背景
    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);

    vec3_t bg_pos = { 0, 0, 0 };
    vec3_t bg_scale = { (float)winW / (float)winH * 6.0f, 6.0f, 0.1f };
    r3d_draw_mesh(r3d, ctx_->ui_quad, bg_pos, bg_scale, jcolor_t{ 255, 255, 255, 255 });

    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_draw(r3d, ctx_->ui_quad, &particles, 480.0f, 320.0f);

    // 标题(白底用深色, 仿 project_proxy2d settings_scene)
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "设置", 216.0f, 12.0f, 28.0f, 480.0f, 320.0f, 30, 30, 40);

    // 标签
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "服务器 IP", 20.0f, 52.0f, 16.0f, 480.0f, 320.0f, 50, 50, 60);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "端口", 20.0f, 92.0f, 16.0f, 480.0f, 320.0f, 50, 50, 60);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "音乐", 20.0f, 132.0f, 16.0f, 480.0f, 320.0f, 50, 50, 60);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "音效", 20.0f, 172.0f, 16.0f, 480.0f, 320.0f, 50, 50, 60);

    // 音量数值
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", (int)(r3d_slider_get_value(bgm_slider) * 100.0f));
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, pct, 330.0f, 132.0f, 16.0f, 480.0f, 320.0f, 50, 50, 60);
    snprintf(pct, sizeof(pct), "%d%%", (int)(r3d_slider_get_value(sfx_slider) * 100.0f));
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, pct, 330.0f, 172.0f, 16.0f, 480.0f, 320.0f, 50, 50, 60);

    // 控件
    if (ip_input)  r3d_combobox_input_draw(r3d, ctx_->ui_quad, ip_input, 480.0f, 320.0f);
    if (port_input) r3d_input_draw(r3d, ctx_->ui_quad, port_input, 480.0f, 320.0f);
    if (bgm_slider) r3d_slider_draw(r3d, ctx_->ui_quad, bgm_slider, 480.0f, 320.0f);
    if (sfx_slider) r3d_slider_draw(r3d, ctx_->ui_quad, sfx_slider, 480.0f, 320.0f);
    if (fullscreen_cb) r3d_checkbox_draw(r3d, ctx_->ui_quad, fullscreen_cb, 480.0f, 320.0f);
    if (btn_back)       r3d_button_draw(r3d, ctx_->ui_quad, btn_back, 480.0f, 320.0f);

    // 最后绘制下拉列表（保证在最上层，不被其他控件遮挡）
    if (ip_input)  r3d_combobox_input_draw_dropdown(r3d, ctx_->ui_quad, ip_input, 480.0f, 320.0f);

    r3d_end(r3d);
}

void SettingsScene::Destroy() {
    if (ip_input)  r3d_combobox_input_destroy(ip_input);
    if (port_input) r3d_input_destroy(port_input);
    if (bgm_slider) r3d_slider_destroy(bgm_slider);
    if (sfx_slider) r3d_slider_destroy(sfx_slider);
    if (fullscreen_cb) r3d_checkbox_destroy(fullscreen_cb);
    if (btn_back)       r3d_button_destroy(btn_back);
}
