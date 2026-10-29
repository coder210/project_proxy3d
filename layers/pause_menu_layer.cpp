#include "pause_menu_layer.h"
#include <SDL3/SDL_events.h>
#include <cstdio>
#include <cstring>
#include "../shared/protocol.h"
#include "../scenes/main_menu_scene.h"

// 当前场景的活动暂停面板（hud_pass 读取，game_scene 创建/销毁）
pause_menu_layer_p g_pause_menu = NULL;

/* 上一次布局所用的逻辑宽度(0=未初始化)。自适应逻辑宽度后须在逻辑宽变化时重算控件位置。
   注意：必须在 pause_menu_layer_create 之前声明（create 会重置它）。 */
static int g_pm_layout_lw = 0;

typedef enum {
    PM_NONE = 0,
    PM_MENU,
    PM_SETTINGS,
    PM_CONFIRM
} pm_page_t;

typedef enum {
    PM_CONFIRM_NONE = 0,
    PM_CONFIRM_QUIT,
    PM_CONFIRM_MAINMENU
} pm_confirm_t;

struct pause_menu_layer {
    Context* ctx;
    pm_page_t page;
    pm_confirm_t confirm_target;

    jr3d_button_p   btn_pause;   // 右上角暂停按钮（圆形）
    jr3d_button_p   btn_resume;
    jr3d_button_p   btn_settings;
    jr3d_button_p   btn_mainmenu;
    jr3d_button_p   btn_quit;
    jr3d_button_p   btn_back;    // 设置页 -> 返回菜单
    jr3d_button_p   btn_yes;     // 确认对话框
    jr3d_button_p   btn_no;

    jr3d_slider_p   bgm_slider;
    jr3d_slider_p   sfx_slider;
    jr3d_checkbox_p cb_debug;    // 调试信息：开/关
};

static void clear_movement(Context* ctx) {
    // 菜单打开时清空方向输入，避免角色继续移动
    ctx->current_input_mask &= ~INPUT_DIR_MASK;
}

static void sync_audio(pause_menu_layer_p self) {
    Context* ctx = self->ctx;
    float bgm = r3d_slider_get_value(self->bgm_slider);
    float sfx = r3d_slider_get_value(self->sfx_slider);
    ctx->settings.bgm_volume = bgm;
    ctx->settings.sfx_volume = sfx;
    if (ctx->bgm)        audio_bgm_set_gain(ctx->bgm, bgm);
    if (ctx->step_sound) audio_shot_set_gain(ctx->step_sound, sfx);
    if (ctx->sfx_melee)  audio_shot_set_gain(ctx->sfx_melee, sfx);
    if (ctx->sfx_bullet) audio_shot_set_gain(ctx->sfx_bullet, sfx);
    if (ctx->sfx_hit)    audio_shot_set_gain(ctx->sfx_hit, sfx);
    if (ctx->sfx_kill)   audio_shot_set_gain(ctx->sfx_kill, sfx);
    if (ctx->sfx_wave)   audio_shot_set_gain(ctx->sfx_wave, sfx);
    settings_save(&ctx->settings);
}

pause_menu_layer_p pause_menu_layer_create(Context* ctx) {
    pause_menu_layer_p self = new pause_menu_layer();
    memset(self, 0, sizeof(*self));
    self->ctx = ctx;
    self->page = PM_NONE;
    self->confirm_target = PM_CONFIRM_NONE;
    g_pm_layout_lw = 0;   // 新实例：重置布局缓存，确保首帧按当前逻辑宽度重算

    // 右上角暂停按钮（圆形）
    self->btn_pause = r3d_button_create(444, 4, 32, 32, "||");

    // 菜单页按钮
    self->btn_resume   = r3d_button_create(160, 95, 160, 30, "继续");
    self->btn_settings = r3d_button_create(160, 135, 160, 30, "设置");
    self->btn_mainmenu = r3d_button_create(160, 175, 160, 30, "返回主菜单");
    self->btn_quit     = r3d_button_create(160, 215, 160, 30, "退出游戏");

    // 设置页
    self->bgm_slider = r3d_slider_create(180, 90, 160, 14, 0.0f, 1.0f, ctx->settings.bgm_volume);
    self->sfx_slider = r3d_slider_create(180, 130, 160, 14, 0.0f, 1.0f, ctx->settings.sfx_volume);
    r3d_slider_set_colors(self->bgm_slider, { 200,200,200,255 }, { 50,50,60,255 }, { 80,80,80,255 }, { 100,100,100,255 });
    r3d_slider_set_colors(self->sfx_slider, { 200,200,200,255 }, { 50,50,60,255 }, { 80,80,80,255 }, { 100,100,100,255 });
    self->cb_debug = r3d_checkbox_create(110, 170, 20, "调试信息（F1）");
    r3d_checkbox_set_colors(self->cb_debug, { 200,200,200,255 }, { 50,50,60,255 }, { 180,180,180,255 });
    r3d_checkbox_set_state(self->cb_debug,
        ctx->show_debug_hud ? JOY_R3D_CHECKBOX_CHECKED : JOY_R3D_CHECKBOX_UNCHECKED);
    self->btn_back = r3d_button_create(180, 250, 120, 30, "返回");

    // 确认对话框
    self->btn_yes = r3d_button_create(155, 165, 80, 30, "确定");
    self->btn_no  = r3d_button_create(245, 165, 80, 30, "取消");

    return self;
}

void pause_menu_layer_destroy(pause_menu_layer_p self) {
    if (!self) return;
    if (self->btn_pause)   r3d_button_destroy(self->btn_pause);
    if (self->btn_resume)  r3d_button_destroy(self->btn_resume);
    if (self->btn_settings)r3d_button_destroy(self->btn_settings);
    if (self->btn_mainmenu)r3d_button_destroy(self->btn_mainmenu);
    if (self->btn_quit)    r3d_button_destroy(self->btn_quit);
    if (self->btn_back)    r3d_button_destroy(self->btn_back);
    if (self->btn_yes)     r3d_button_destroy(self->btn_yes);
    if (self->btn_no)      r3d_button_destroy(self->btn_no);
    if (self->bgm_slider)  r3d_slider_destroy(self->bgm_slider);
    if (self->sfx_slider)  r3d_slider_destroy(self->sfx_slider);
    if (self->cb_debug)    r3d_checkbox_destroy(self->cb_debug);
    delete self;
}

static void handle_modal(pause_menu_layer_p self, const void* ev) {
    jevent_callbacks_t cbs = {};
    cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
        (void)btn;
        pause_menu_layer_p self = (pause_menu_layer_p)arg;
        Context* ctx = self->ctx;
        jr3d_p r3d = ctx->r3d;
        if (self->page == PM_MENU) {
            if (r3d_button_is_pressed(r3d, self->btn_resume, true, x, y)) { self->page = PM_NONE; }
            else if (r3d_button_is_pressed(r3d, self->btn_settings, true, x, y)) { self->page = PM_SETTINGS; }
            else if (r3d_button_is_pressed(r3d, self->btn_mainmenu, true, x, y)) { self->confirm_target = PM_CONFIRM_MAINMENU; self->page = PM_CONFIRM; }
            else if (r3d_button_is_pressed(r3d, self->btn_quit, true, x, y)) { self->confirm_target = PM_CONFIRM_QUIT; self->page = PM_CONFIRM; }
        } else if (self->page == PM_SETTINGS) {
            if (self->bgm_slider) r3d_slider_is_pressed(r3d, self->bgm_slider, x, y);
            if (self->sfx_slider) r3d_slider_is_pressed(r3d, self->sfx_slider, x, y);
            if (r3d_checkbox_is_pressed(r3d, self->cb_debug, x, y)) {
                ctx->show_debug_hud = (r3d_checkbox_get_state(self->cb_debug) == JOY_R3D_CHECKBOX_CHECKED);
            }
            if (r3d_button_is_pressed(r3d, self->btn_back, true, x, y)) { self->page = PM_MENU; }
        } else if (self->page == PM_CONFIRM) {
            if (r3d_button_is_pressed(r3d, self->btn_yes, true, x, y)) {
                if (self->confirm_target == PM_CONFIRM_QUIT) push_quit_event();
                else ctx->scene_mgr->change(new MainMenuScene(ctx));
            } else if (r3d_button_is_pressed(r3d, self->btn_no, true, x, y)) {
                self->page = PM_MENU;
            }
        }
    };
    cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
        (void)btn;
        pause_menu_layer_p self = (pause_menu_layer_p)arg;
        Context* ctx = self->ctx;
        if (self->bgm_slider) r3d_slider_set_pressed(self->bgm_slider, false);
        if (self->sfx_slider) r3d_slider_set_pressed(self->sfx_slider, false);
        if (self->page == PM_MENU) {
            if (r3d_button_is_pressed(ctx->r3d, self->btn_resume, false, x, y)) r3d_button_release(ctx->r3d, self->btn_resume, x, y);
            if (r3d_button_is_pressed(ctx->r3d, self->btn_settings, false, x, y)) r3d_button_release(ctx->r3d, self->btn_settings, x, y);
            if (r3d_button_is_pressed(ctx->r3d, self->btn_mainmenu, false, x, y)) r3d_button_release(ctx->r3d, self->btn_mainmenu, x, y);
            if (r3d_button_is_pressed(ctx->r3d, self->btn_quit, false, x, y)) r3d_button_release(ctx->r3d, self->btn_quit, x, y);
        } else if (self->page == PM_SETTINGS) {
            if (r3d_button_is_pressed(ctx->r3d, self->btn_back, false, x, y)) r3d_button_release(ctx->r3d, self->btn_back, x, y);
            sync_audio(self); // 松手时落盘音量
        } else if (self->page == PM_CONFIRM) {
            if (r3d_button_is_pressed(ctx->r3d, self->btn_yes, false, x, y)) r3d_button_release(ctx->r3d, self->btn_yes, x, y);
            if (r3d_button_is_pressed(ctx->r3d, self->btn_no, false, x, y)) r3d_button_release(ctx->r3d, self->btn_no, x, y);
        }
    };
    cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
        (void)dx; (void)dy;
        pause_menu_layer_p self = (pause_menu_layer_p)arg;
        Context* ctx = self->ctx;
        if (self->page == PM_SETTINGS) {
            if (self->bgm_slider) { r3d_slider_is_hovered(ctx->r3d, self->bgm_slider, x, y); r3d_slider_drag_move(ctx->r3d, self->bgm_slider, x, y); }
            if (self->sfx_slider) { r3d_slider_is_hovered(ctx->r3d, self->sfx_slider, x, y); r3d_slider_drag_move(ctx->r3d, self->sfx_slider, x, y); }
            sync_audio(self); // 拖动时实时应用音量
        } else if (self->page == PM_MENU) {
            r3d_button_is_pressed(ctx->r3d, self->btn_resume, false, x, y);
            r3d_button_is_pressed(ctx->r3d, self->btn_settings, false, x, y);
            r3d_button_is_pressed(ctx->r3d, self->btn_mainmenu, false, x, y);
            r3d_button_is_pressed(ctx->r3d, self->btn_quit, false, x, y);
        } else if (self->page == PM_SETTINGS) {
            r3d_button_is_pressed(ctx->r3d, self->btn_back, false, x, y);
        } else if (self->page == PM_CONFIRM) {
            r3d_button_is_pressed(ctx->r3d, self->btn_yes, false, x, y);
            r3d_button_is_pressed(ctx->r3d, self->btn_no, false, x, y);
        }
    };
    dispatch_event(cbs, ev, self);
}

/* 布局与逻辑宽度解耦（仿 joy_topdown_template 的 syncLayout）：
   自适应逻辑宽度后逻辑区不再是固定 480 宽，菜单若不重算会偏离屏幕中央；
   这里把右上角暂停按钮锚定真实右边缘，其余控件/面板按设计稿 480 居中平移。
   仅在逻辑宽度变化时重算。 */
static void pause_menu_sync_layout(pause_menu_layer_p self) {
    if (!self || !self->ctx || !self->ctx->r3d) return;
    int lw = 0, lh = 0;
    r3d_get_logical_presentation(self->ctx->r3d, &lw, &lh);
    if (lw <= 0) return;
    if (lw == g_pm_layout_lw) return;
    g_pm_layout_lw = lw;

    const float ox = ((float)lw - 480.0f) * 0.5f;   // 超出设计稿宽度的居中平移量

    // 右上角暂停按钮：锚定真实右边缘（设计稿 x=444, w=32, 右留边 4）
    if (self->btn_pause) self->btn_pause->x = (float)lw - self->btn_pause->w - 4.0f;

    // 菜单页按钮（设计稿 x=160）
    if (self->btn_resume)   self->btn_resume->x   = 160.0f + ox;
    if (self->btn_settings) self->btn_settings->x = 160.0f + ox;
    if (self->btn_mainmenu) self->btn_mainmenu->x = 160.0f + ox;
    if (self->btn_quit)     self->btn_quit->x     = 160.0f + ox;

    // 设置页
    if (self->bgm_slider) self->bgm_slider->x = 180.0f + ox;
    if (self->sfx_slider) self->sfx_slider->x = 180.0f + ox;
    if (self->cb_debug)   self->cb_debug->x   = 110.0f + ox;
    if (self->btn_back)   self->btn_back->x   = 180.0f + ox;

    // 确认对话框
    if (self->btn_yes) self->btn_yes->x = 155.0f + ox;
    if (self->btn_no)  self->btn_no->x  = 245.0f + ox;
}

bool pause_menu_layer_handle_event(pause_menu_layer_p self, const void* ev) {
    if (!self) return false;
    // 命中判定前确保控件几何与当前逻辑宽度一致（否则旋转/全屏后点不中）
    pause_menu_sync_layout(self);
    const SDL_Event* e = (const SDL_Event*)ev;

    // ESC：开/关菜单
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat &&
        e->key.scancode == SDL_SCANCODE_ESCAPE) {
        if (self->page == PM_NONE) self->page = PM_MENU;
        else self->page = PM_NONE;
        clear_movement(self->ctx);
        return true;
    }

    if (self->page == PM_NONE) {
        // 仅处理暂停按钮；其它事件交给游戏
        jevent_callbacks_t cbs = {};
        cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
            (void)btn;
            pause_menu_layer_p self = (pause_menu_layer_p)arg;
            if (r3d_button_is_pressed(self->ctx->r3d, self->btn_pause, true, x, y)) {
                self->page = PM_MENU;
                clear_movement(self->ctx);
            }
        };
        cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
            (void)btn;
            pause_menu_layer_p self = (pause_menu_layer_p)arg;
            r3d_button_is_pressed(self->ctx->r3d, self->btn_pause, false, x, y);
            r3d_button_release(self->ctx->r3d, self->btn_pause, x, y);
        };
        cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
            (void)dx; (void)dy;
            pause_menu_layer_p self = (pause_menu_layer_p)arg;
            r3d_button_is_pressed(self->ctx->r3d, self->btn_pause, false, x, y);
        };
        dispatch_event(cbs, ev, self);
        return self->page != PM_NONE; // 点了暂停按钮则吞掉本次事件
    }

    // 菜单/设置/确认打开时，吞掉所有事件
    handle_modal(self, ev);
    return true;
}

static void draw_panel(jr3d_p r3d, render_mesh_p q, float x, float y, float w, float h, float W, float H) {
    r3d_draw_rect_2d(r3d, q, x, y, w, h, 28, 28, 38, W, H);
    r3d_draw_rect_2d(r3d, q, x, y, w, 2, 90, 90, 120, W, H);
}

void pause_menu_layer_render(pause_menu_layer_p self) {
    if (!self) return;
    Context* ctx = self->ctx;
    jr3d_p r3d = ctx->r3d;
    render_mesh_p q = ctx->ui_quad;
    pause_menu_sync_layout(self);

    int lw = 0, lh = 0;
    r3d_get_logical_presentation(r3d, &lw, &lh);
    if (lw <= 0) lw = 480;
    if (lh <= 0) lh = 320;
    const float W = 480.0f, H = 320.0f;          // 设计稿尺寸(LETTERBOX 下传给 r3d 仅作正数校验)
    const float ox = ((float)lw - W) * 0.5f;     // 超出设计稿宽度的居中平移量

    if (self->page == PM_NONE) {
        if (self->btn_pause) r3d_button_draw_circle(r3d, q, self->btn_pause, W, H);
        return;
    }

    // 遮罩（r3d_draw_rect_2d 不支持 alpha，故为不透明黑色背景；菜单/面板画在其上）
    r3d_draw_rect_2d(r3d, q, 0, 0, (float)lw, (float)lh, 0, 0, 0, W, H);

    if (self->page == PM_MENU) {
        draw_panel(r3d, q, 120 + ox, 55, 240, 200, W, H);
        r3d_font_draw_2d_color(r3d, q, "暂停", 212 + ox, 65, 24, W, H, 230, 230, 230);
        r3d_button_draw(r3d, q, self->btn_resume, W, H);
        r3d_button_draw(r3d, q, self->btn_settings, W, H);
        r3d_button_draw(r3d, q, self->btn_mainmenu, W, H);
        r3d_button_draw(r3d, q, self->btn_quit, W, H);
    } else if (self->page == PM_SETTINGS) {
        draw_panel(r3d, q, 100 + ox, 40, 280, 250, W, H);
        r3d_font_draw_2d_color(r3d, q, "设置", 214 + ox, 50, 24, W, H, 230, 230, 230);
        r3d_font_draw_2d_color(r3d, q, "音乐", 110 + ox, 92, 16, W, H, 200, 200, 200);
        r3d_font_draw_2d_color(r3d, q, "音效", 110 + ox, 132, 16, W, H, 200, 200, 200);
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", (int)(r3d_slider_get_value(self->bgm_slider) * 100.0f));
        r3d_font_draw_2d_color(r3d, q, pct, 350 + ox, 92, 16, W, H, 200, 200, 200);
        snprintf(pct, sizeof(pct), "%d%%", (int)(r3d_slider_get_value(self->sfx_slider) * 100.0f));
        r3d_font_draw_2d_color(r3d, q, pct, 350 + ox, 132, 16, W, H, 200, 200, 200);
        r3d_slider_draw(r3d, q, self->bgm_slider, W, H);
        r3d_slider_draw(r3d, q, self->sfx_slider, W, H);
        r3d_checkbox_draw(r3d, q, self->cb_debug, W, H);
        r3d_button_draw(r3d, q, self->btn_back, W, H);
    } else if (self->page == PM_CONFIRM) {
        draw_panel(r3d, q, 140 + ox, 110, 200, 100, W, H);
        r3d_font_draw_2d_color(r3d, q,
            self->confirm_target == PM_CONFIRM_QUIT ? "退出游戏？" : "返回主菜单？",
            158 + ox, 122, 18, W, H, 230, 230, 230);
        r3d_button_draw(r3d, q, self->btn_yes, W, H);
        r3d_button_draw(r3d, q, self->btn_no, W, H);
    }
}
