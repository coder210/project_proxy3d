#include "online_scene.h"
#include <joy/joy.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ===== 版式（逻辑坐标 480x320，与 joy_topdown_template 的 online_scene 一致）=====
static const float BAND_H   = 36.0f;
static const float LIST_X   = 12.0f;
static const float LIST_Y   = 44.0f;
static const float LIST_W   = 180.0f;
static const float LIST_H   = 230.0f;
static const float PANEL_X  = 200.0f;
static const float PANEL_Y  = 44.0f;
static const float PANEL_H  = 230.0f;

// 底部操作条：创建房间 / 加入房间 / 服务端 / 返回
static const float BTN_Y    = 280.0f;
static const float BTN_H    = 30.0f;
static const float BTN_W    = 108.0f;
static const float BTN_GAP  = 6.0f;
static const float BTN_START= 20.0f;

// 右上「直连」小按钮
static const float DOPEN_W = 68.0f, DOPEN_H = 24.0f;

// 直连弹窗（居中卡片）
static const float DLG_X = 90.0f, DLG_Y = 85.0f, DLG_W = 300.0f, DLG_H = 150.0f;
static const float DLG_IP_X = 150.0f, DLG_IP_Y = 128.0f, DLG_IP_W = 220.0f, DLG_ROW_H = 28.0f;
static const float DLG_PORT_X = 150.0f, DLG_PORT_Y = 166.0f, DLG_PORT_W = 110.0f;
static const float DLG_BTN_Y = 212.0f, DLG_BTN_W = 90.0f, DLG_BTN_H = 26.0f;
static const float DLG_CANCEL_X = 196.0f, DLG_CONNECT_X = 316.0f;

// 配色（与其它选单页一致）
static const Uint8 INK_R = 40,   INK_G = 40,   INK_B = 48;
static const Uint8 PAP_R = 245,  PAP_G = 243,  PAP_B = 237;
static const Uint8 BG_R  = 238,  BG_G  = 234,  BG_B  = 226;
static const Uint8 PNL_R = 250,  PNL_G = 248,  PNL_B = 242;
static const Uint8 BDR_R = 150,  BDR_G = 146,  BDR_B = 138;

static void rect_border(jr3d_p r, render_mesh_p m, float x, float y, float w, float h,
                        Uint8 cr, Uint8 cg, Uint8 cb, float th) {
    r3d_draw_rect_2d(r, m, x,         y,         w, th, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x,         y + h - th, w, th, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x,         y,         th, h, cr, cg, cb, 480.0f, 320.0f);
    r3d_draw_rect_2d(r, m, x + w - th, y,         th, h, cr, cg, cb, 480.0f, 320.0f);
}

static void send_online(int code) { push_user_event(ONLINE_EVENT, code); }

OnlineScene::OnlineScene(Context* ctx) : Scene(ctx, "Online") {}

void OnlineScene::Load() {
    log_info("[online scene] start...");

    // 底部操作条：创建房间(HOST) / 加入房间(CLIENT) / 服务端(SERVER) / 返回(BACK)
    btn_create = r3d_button_create(BTN_START + (BTN_W + BTN_GAP) * 0.0f, BTN_Y, BTN_W, BTN_H, "创建房间");
    btn_join   = r3d_button_create(BTN_START + (BTN_W + BTN_GAP) * 1.0f, BTN_Y, BTN_W, BTN_H, "加入房间");
    btn_server = r3d_button_create(BTN_START + (BTN_W + BTN_GAP) * 2.0f, BTN_Y, BTN_W, BTN_H, "服务端");
    btn_back   = r3d_button_create(BTN_START + (BTN_W + BTN_GAP) * 3.0f, BTN_Y, BTN_W, BTN_H, "返回");

    // 右上「直连」
    btn_direct = r3d_button_create(480.0f - 12.0f - DOPEN_W, 6.0f, DOPEN_W, DOPEN_H, "直连");

    // 直连弹窗的输入与按钮（默认隐藏，打开弹窗时才绘制/接收）
    ip_input = r3d_input_create(DLG_IP_X, DLG_IP_Y, DLG_IP_W, DLG_ROW_H, 16.0f, 50, 50, 60,
                                ctx_->settings.server_ip);
    char pbuf[16];
    snprintf(pbuf, sizeof(pbuf), "%d", ctx_->settings.server_port);
    port_input = r3d_input_create(DLG_PORT_X, DLG_PORT_Y, DLG_PORT_W, DLG_ROW_H, 16.0f, 50, 50, 60, pbuf);

    btn_cancel  = r3d_button_create(DLG_CANCEL_X,  DLG_BTN_Y, DLG_BTN_W, DLG_BTN_H, "取消");
    btn_connect = r3d_button_create(DLG_CONNECT_X, DLG_BTN_Y, DLG_BTN_W, DLG_BTN_H, "连接");
}

void OnlineScene::Update(float dt) { (void)dt; }

void OnlineScene::set_direct_open(bool open) {
    direct_open_ = open;
    if (open) {
        if (ip_input)   { r3d_input_focus(ip_input); }
        if (ip_input)   r3d_input_start_text_input(ctx_->r3d, ip_input);
    } else {
        if (ip_input)   { r3d_input_stop_text_input(ctx_->r3d, ip_input); r3d_input_defocus(ip_input); }
        if (port_input) { r3d_input_stop_text_input(ctx_->r3d, port_input); r3d_input_defocus(port_input); }
    }
}

void OnlineScene::direct_connect() {
    const char* ip = ip_input ? r3d_input_get_text(ip_input) : "";
    int port = port_input ? atoi(r3d_input_get_text(port_input)) : 0;
    if (!ip || !ip[0] || port <= 0 || port > 65535) {
        log_info("[lobby] direct connect invalid ip/port");
        return;   // 无效则保持弹窗打开
    }
    snprintf(ctx_->settings.server_ip, sizeof(ctx_->settings.server_ip), "%s", ip);
    ctx_->settings.server_port = port;
    settings_save(&ctx_->settings);
    set_direct_open(false);
    log_info("[lobby] direct connect %s:%d", ctx_->settings.server_ip, ctx_->settings.server_port);
    send_online(ONLINE_EVENT_CLIENT);
}

void OnlineScene::HandleEvent(const void* ev) {
    jr3d_p r3d = ctx_->r3d;
    SDL_Event* sdle = (SDL_Event*)ev;

    // 弹窗打开时，事件交给输入框处理（聚焦/文本输入/退格/方向键）
    if (direct_open_) {
        if (ip_input)   r3d_input_handle_event(r3d, ip_input, sdle);
        if (port_input) r3d_input_handle_event(r3d, port_input, sdle);
    }

    jevent_callbacks_t cbs = {};
    cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
        (void)btn;
        OnlineScene* self = (OnlineScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        if (self->direct_open_) {
            if (self->btn_cancel)  r3d_button_is_pressed(r3d, self->btn_cancel, true, x, y);
            if (self->btn_connect) r3d_button_is_pressed(r3d, self->btn_connect, true, x, y);
            return;   // 弹窗打开时只处理弹窗
        }
        if (self->btn_create) r3d_button_is_pressed(r3d, self->btn_create, true, x, y);
        if (self->btn_join)   r3d_button_is_pressed(r3d, self->btn_join, true, x, y);
        if (self->btn_server) r3d_button_is_pressed(r3d, self->btn_server, true, x, y);
        if (self->btn_back)   r3d_button_is_pressed(r3d, self->btn_back, true, x, y);
        if (self->btn_direct) r3d_button_is_pressed(r3d, self->btn_direct, true, x, y);
    };

    cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
        (void)btn;
        OnlineScene* self = (OnlineScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;

        if (self->direct_open_) {
            bool cancel = self->btn_cancel && self->btn_cancel->pressedInside &&
                          r3d_button_is_hovered(r3d, self->btn_cancel, x, y);
            bool connect = self->btn_connect && self->btn_connect->pressedInside &&
                           r3d_button_is_hovered(r3d, self->btn_connect, x, y);
            if (self->btn_cancel)  r3d_button_release(r3d, self->btn_cancel, x, y);
            if (self->btn_connect) r3d_button_release(r3d, self->btn_connect, x, y);
            if (cancel)  { self->set_direct_open(false); return; }
            if (connect) { self->direct_connect(); return; }
            return;
        }

        auto clicked = [&](jr3d_button_p b) {
            if (!b) return false;
            bool c = b->pressedInside && r3d_button_is_hovered(r3d, b, x, y);
            r3d_button_release(r3d, b, x, y);
            return c;
        };
        if (clicked(self->btn_create))      { send_online(ONLINE_EVENT_HOST);   return; }
        if (clicked(self->btn_join))        { send_online(ONLINE_EVENT_CLIENT); return; }
        if (clicked(self->btn_server))      { send_online(ONLINE_EVENT_SERVER); return; }
        if (clicked(self->btn_direct))      { self->set_direct_open(true);      return; }
        if (clicked(self->btn_back))        { send_online(ONLINE_EVENT_BACK);   return; }
    };

    cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
        (void)dx; (void)dy;
        OnlineScene* self = (OnlineScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        if (self->direct_open_) {
            if (self->btn_cancel)  r3d_button_is_pressed(r3d, self->btn_cancel, false, x, y);
            if (self->btn_connect) r3d_button_is_pressed(r3d, self->btn_connect, false, x, y);
            if (self->ip_input)   r3d_input_is_hovered(r3d, self->ip_input, x, y);
            if (self->port_input) r3d_input_is_hovered(r3d, self->port_input, x, y);
            return;
        }
        if (self->btn_create) r3d_button_is_pressed(r3d, self->btn_create, false, x, y);
        if (self->btn_join)   r3d_button_is_pressed(r3d, self->btn_join, false, x, y);
        if (self->btn_server) r3d_button_is_pressed(r3d, self->btn_server, false, x, y);
        if (self->btn_back)   r3d_button_is_pressed(r3d, self->btn_back, false, x, y);
        if (self->btn_direct) r3d_button_is_pressed(r3d, self->btn_direct, false, x, y);
    };

    dispatch_event(cbs, ev, this);
}

void OnlineScene::Render() {
    jr3d_p r3d = ctx_->r3d;
    render_mesh_p m = ctx_->ui_quad;
    if (!r3d_begin(r3d)) return;

    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);

    // 米色纸底
    r3d_draw_rect_2d(r3d, m, 0, 0, 480.0f, 320.0f, BG_R, BG_G, BG_B, 480.0f, 320.0f);

    // 顶部墨色标题带 + 标题
    r3d_draw_rect_2d(r3d, m, 0, 0, 480.0f, BAND_H, INK_R, INK_G, INK_B, 480.0f, 320.0f);
    r3d_font_draw_2d_color(r3d, m, "联机大厅", 16.0f, 6.0f, 24.0f, 480.0f, 320.0f, PAP_R, PAP_G, PAP_B);

    // 右上「直连」按钮
    r3d_button_draw(r3d, m, btn_direct, 480.0f, 320.0f);

    // ---- 左侧房间列表 ----
    {
        float px = LIST_X, py = LIST_Y, pw = LIST_W, ph = LIST_H;
        r3d_draw_rect_2d(r3d, m, px, py, pw, ph, PNL_R, PNL_G, PNL_B, 480.0f, 320.0f);
        rect_border(r3d, m, px, py, pw, ph, BDR_R, BDR_G, BDR_B, 2.0f);
        r3d_font_draw_2d_color(r3d, m, "房间列表", px + 10.0f, py + 8.0f, 14.0f, 480.0f, 320.0f, 120, 116, 108);
        // 本工程无局域网发现，列表恒为空（占位）
        r3d_font_draw_2d_color(r3d, m, "暂无房间", px + 12.0f, py + ph * 0.5f - 10.0f, 14.0f, 480.0f, 320.0f, 150, 146, 138);
        r3d_font_draw_2d_color(r3d, m, "用右上角『直连』加入", px + 12.0f, py + ph * 0.5f + 12.0f, 12.0f, 480.0f, 320.0f, 165, 161, 152);
    }

    // ---- 右侧详情面板 ----
    {
        float px = PANEL_X, py = PANEL_Y;
        float pw = 480.0f - px - 12.0f;
        float ph = PANEL_H;
        r3d_draw_rect_2d(r3d, m, px, py, pw, ph, PNL_R, PNL_G, PNL_B, 480.0f, 320.0f);
        rect_border(r3d, m, px, py, pw, ph, BDR_R, BDR_G, BDR_B, 2.0f);

        r3d_font_draw_2d_color(r3d, m, "连接信息", px + 10.0f, py + 10.0f, 20.0f, 480.0f, 320.0f, 28, 28, 32);

        char buf[128];
        snprintf(buf, sizeof(buf), "目标  %s:%d", ctx_->settings.server_ip, ctx_->settings.server_port);
        r3d_font_draw_2d_color(r3d, m, buf, px + 10.0f, py + 48.0f, 14.0f, 480.0f, 320.0f, 90, 90, 98);
        r3d_font_draw_2d_color(r3d, m, "「创建房间」本机开房，其他玩家直连加入", px + 10.0f, py + 72.0f, 12.0f, 480.0f, 320.0f, 90, 90, 98);
        r3d_font_draw_2d_color(r3d, m, "「加入房间」连接上面目标地址", px + 10.0f, py + 92.0f, 12.0f, 480.0f, 320.0f, 90, 90, 98);
        r3d_font_draw_2d_color(r3d, m, "「服务端」仅跑逻辑与转发（无本地玩家）", px + 10.0f, py + 112.0f, 12.0f, 480.0f, 320.0f, 90, 90, 98);

        r3d_font_draw_2d_color(r3d, m, "同网段房主开房后可用『直连』加入", px + 10.0f, py + ph - 26.0f, 12.0f, 480.0f, 320.0f, 165, 161, 152);
    }

    // ---- 底部操作条 ----
    r3d_button_draw(r3d, m, btn_create, 480.0f, 320.0f);
    r3d_button_draw(r3d, m, btn_join,   480.0f, 320.0f);
    r3d_button_draw(r3d, m, btn_server, 480.0f, 320.0f);
    r3d_button_draw(r3d, m, btn_back,   480.0f, 320.0f);

    // ---- 直连弹窗（最上层）----
    if (direct_open_) {
        // r3d_draw_rect_2d 无 alpha，不做半透明遮罩；直接把卡片画在大厅之上
        float dx = DLG_X, dy = DLG_Y, dw = DLG_W, dh = DLG_H;
        r3d_draw_rect_2d(r3d, m, dx, dy, dw, dh, PNL_R, PNL_G, PNL_B, 480.0f, 320.0f);
        rect_border(r3d, m, dx, dy, dw, dh, BDR_R, BDR_G, BDR_B, 2.0f);

        r3d_font_draw_2d_color(r3d, m, "直连服务器", dx + 16.0f, dy + 12.0f, 20.0f, 480.0f, 320.0f, 28, 28, 32);
        r3d_font_draw_2d_color(r3d, m, "IP 地址", dx + 16.0f, dy + 49.0f, 12.0f, 480.0f, 320.0f, 90, 90, 98);
        r3d_font_draw_2d_color(r3d, m, "端口",     dx + 16.0f, dy + 87.0f, 12.0f, 480.0f, 320.0f, 90, 90, 98);

        if (ip_input)   r3d_input_draw(r3d, m, ip_input, 480.0f, 320.0f);
        if (port_input) r3d_input_draw(r3d, m, port_input, 480.0f, 320.0f);
        if (btn_cancel)  r3d_button_draw(r3d, m, btn_cancel, 480.0f, 320.0f);
        if (btn_connect) r3d_button_draw(r3d, m, btn_connect, 480.0f, 320.0f);
    }

    r3d_end(r3d);
}

void OnlineScene::Destroy() {
    if (btn_create)  r3d_button_destroy(btn_create);
    if (btn_join)    r3d_button_destroy(btn_join);
    if (btn_server)  r3d_button_destroy(btn_server);
    if (btn_back)    r3d_button_destroy(btn_back);
    if (btn_direct)  r3d_button_destroy(btn_direct);
    if (btn_cancel)  r3d_button_destroy(btn_cancel);
    if (btn_connect) r3d_button_destroy(btn_connect);
    if (ip_input) {
        r3d_input_stop_text_input(ctx_->r3d, ip_input);
        r3d_input_destroy(ip_input);
    }
    if (port_input) {
        r3d_input_stop_text_input(ctx_->r3d, port_input);
        r3d_input_destroy(port_input);
    }
}
