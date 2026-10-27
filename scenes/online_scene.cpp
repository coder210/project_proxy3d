#include "online_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include "main_menu_scene.h"
#include "loading_scene.h"
#include "ui_particles.h"

static void send_online_event(int event_code) {
    push_user_event(ONLINE_EVENT, event_code);
}

OnlineScene::OnlineScene(Context* ctx) : Scene(ctx, "Online") {}

void OnlineScene::HandleEvent(const void* ev) {
    jevent_callbacks_t cbs = {};
    cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
        (void)btn;
        OnlineScene* self = (OnlineScene*)arg;
        if (self->btn_host)   r3d_button_is_pressed(self->ctx_->r3d, self->btn_host, true, x, y);
        if (self->btn_client) r3d_button_is_pressed(self->ctx_->r3d, self->btn_client, true, x, y);
        if (self->btn_server) r3d_button_is_pressed(self->ctx_->r3d, self->btn_server, true, x, y);
        if (self->btn_back)   r3d_button_is_pressed(self->ctx_->r3d, self->btn_back, true, x, y);
    };
    cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
        (void)btn;
        OnlineScene* self = (OnlineScene*)arg;
        if (self->btn_host)   r3d_button_release(self->ctx_->r3d, self->btn_host, x, y);
        if (self->btn_client) r3d_button_release(self->ctx_->r3d, self->btn_client, x, y);
        if (self->btn_server) r3d_button_release(self->ctx_->r3d, self->btn_server, x, y);
        if (self->btn_back)   r3d_button_release(self->ctx_->r3d, self->btn_back, x, y);
    };
    cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
        (void)dx; (void)dy;
        OnlineScene* self = (OnlineScene*)arg;
        if (self->btn_host)   r3d_button_is_pressed(self->ctx_->r3d, self->btn_host, false, x, y);
        if (self->btn_client) r3d_button_is_pressed(self->ctx_->r3d, self->btn_client, false, x, y);
        if (self->btn_server) r3d_button_is_pressed(self->ctx_->r3d, self->btn_server, false, x, y);
        if (self->btn_back)   r3d_button_is_pressed(self->ctx_->r3d, self->btn_back, false, x, y);
    };
    dispatch_event(cbs, ev, this);
}

void OnlineScene::Load() {
    log_info("[online scene] start...");

    // 按钮（逻辑坐标 480x320，窗口缩放自适应）
    int btn_width = 140, btn_height = 50;
    int btn_x = (480 - btn_width) / 2;
    int start_y = (320 - btn_height * 4 - 60) / 2;
    int gap = 20;

    label_title = r3d_label_create((float)(480 / 2 - 60), (float)(start_y - 60), 28.0f, 60, 60, 70, "联机模式");

    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_init(&particles, 480, 320);

    btn_host = r3d_button_create((float)btn_x, (float)start_y, (float)btn_width, (float)btn_height, "创建主机");
    r3d_button_set_onclick(btn_host, [](jr3d_button_p btn) {
        (void)btn;
        send_online_event(ONLINE_EVENT_HOST);
    });
    btn_client = r3d_button_create((float)btn_x, (float)(start_y + (btn_height + gap)), (float)btn_width, (float)btn_height, "加入游戏");
    r3d_button_set_onclick(btn_client, [](jr3d_button_p btn) {
        (void)btn;
        send_online_event(ONLINE_EVENT_CLIENT);
    });
    btn_server = r3d_button_create((float)btn_x, (float)(start_y + (btn_height + gap) * 2), (float)btn_width, (float)btn_height, "启动服务端");
    r3d_button_set_onclick(btn_server, [](jr3d_button_p btn) {
        (void)btn;
        send_online_event(ONLINE_EVENT_SERVER);
    });
    btn_back = r3d_button_create((float)btn_x, (float)(start_y + (btn_height + gap) * 3), (float)btn_width, (float)btn_height, "返回");
    r3d_button_set_onclick(btn_back, [](jr3d_button_p btn) {
        (void)btn;
        send_online_event(ONLINE_EVENT_BACK);
    });
}

void OnlineScene::Update(float dt) {
    elapsed += dt;
    ui_particles_update(&particles, dt, 480, 320);
}

void OnlineScene::Render() {
    jr3d_p r3d = ctx_->r3d;

    int winW, winH;
    r3d_get_window_size(r3d, &winW, &winH);

    if (!r3d_begin(r3d)) return;

    // 背景
    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);

    // 背景(白底蓝图风, 与 game_scene 一致)
    vec3_t bg_pos = { 0, 0, 0 };
    vec3_t bg_scale = { (float)winW / (float)winH * 6.0f, 6.0f, 0.1f };
    r3d_draw_mesh(r3d, ctx_->ui_quad, bg_pos, bg_scale, jcolor_t{ 255, 255, 255, 255 });

    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_draw(r3d, ctx_->ui_quad, &particles, 480.0f, 320.0f);

    // 标题
    r3d_label_draw(r3d, ctx_->ui_quad, label_title, 480.0f, 320.0f);

    // 按钮
    r3d_button_draw(r3d, ctx_->ui_quad, btn_host, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_client, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_server, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_back, 480.0f, 320.0f);

    r3d_end(r3d);
}

void OnlineScene::Destroy() {
    if (label_title) r3d_label_destroy(label_title);
    if (btn_host) r3d_button_destroy(btn_host);
    if (btn_client) r3d_button_destroy(btn_client);
    if (btn_server) r3d_button_destroy(btn_server);
    if (btn_back) r3d_button_destroy(btn_back);
}
