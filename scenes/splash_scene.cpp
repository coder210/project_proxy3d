#include "splash_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include <SDL3/SDL.h>
#include "main_menu_scene.h"
#include "ui_particles.h"

SplashScene::SplashScene(Context* ctx) : Scene(ctx, "Splash") {}

void SplashScene::Load() {
    // 加载 logo 纹理（使用 flip=0，与 SDL_GPU 的 V=0 在顶部一致）
    logo_w = 0; logo_h = 0;
    SDL_Surface* surf = SDL_LoadPNG("project_proxy3d/textures/logo.png");
    if (surf) {
        SDL_Surface* rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surf);
        if (rgba) {
            ctx_->logo_tex = r3d_create_texture(ctx_->r3d, rgba->w, rgba->h, rgba->pixels, 0);
            logo_w = rgba->w;
            logo_h = rgba->h;
            SDL_DestroySurface(rgba);
        }
    }
    log_info("[splash scene] start, logo_tex=%p w=%d h=%d", ctx_->logo_tex, logo_w, logo_h);

    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_init(&particles, 480, 320);
}

void SplashScene::Update(float dt) {
    elapsed += dt;
    ui_particles_update(&particles, dt, 480, 320);

    // 淡入：0~0.5s，透明度从 0→1
    if (elapsed < 0.5f) {
        alpha = elapsed / 0.5f;
    }
    // 保持：0.5s~2.5s，透明度 = 1
    else if (elapsed < 2.5f) {
        alpha = 1.0f;
        faded_in = true;
    }
    // 淡出：2.5s~3.0s，透明度从 1→0
    else if (elapsed < 3.0f) {
        alpha = 1.0f - (elapsed - 2.5f) / 0.5f;
        fading_out = true;
    }
    // 3.0s 后切换到主场景
    else {
        ctx_->scene_mgr->setPending(new MainMenuScene(ctx_));
        push_user_event(TRANSITION_EVENT, TRANSITION_SPLASH_DONE);
    }
}

void SplashScene::Render() {
    jr3d_p r3d = ctx_->r3d;

    int winW, winH;
    r3d_get_window_size(r3d, &winW, &winH);

    if (!r3d_begin(r3d)) return;

    // 背景 — 设置相机渲染一个全屏深色背景
    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);

    // 绘制全屏背景板（白底蓝图风，与 game_scene 一致）
    vec3_t bg_pos = { 0, 0, 0 };
    vec3_t bg_scale = { (float)winW / (float)winH * 6.0f, 6.0f, 0.1f };
    jcolor_t bg_color = { 255, 255, 255, 255 };
    r3d_draw_mesh(r3d, ctx_->ui_quad, bg_pos, bg_scale, bg_color);

    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_draw(r3d, ctx_->ui_quad, &particles, 480.0f, 320.0f);

    // 绘制 Logo（居中，使用纹理，alpha 混合通过纹理透明度控制）
    if (ctx_->logo_tex && logo_w > 0 && logo_h > 0) {
        // 根据窗口比例计算 logo 显示大小（保持宽高比，限制最大高度 2.0 单位）
        float logo_aspect = (float)logo_w / (float)logo_h;
        float lw = 2.0f;
        float lh = lw / logo_aspect;
        if (lh > 2.0f) { lh = 2.0f; lw = lh * logo_aspect; }
        vec3_t logo_pos = { 0, 1.3f, 0 };
        vec3_t logo_scale = { lw, lh, 0.1f };
        r3d_draw_mesh_tex(r3d, ctx_->ui_quad, logo_pos, logo_scale, ctx_->logo_tex);
    }

    // 标题文字（使用 2D HUD 文字，逻辑坐标 480x320，窗口缩放自适应；白底用深灰）
    const char* title = "Project Proxy 3D";
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, title,
        (float)(480 - 200) / 2.0f, 170.0f, 24.0f, 480.0f, 320.0f, 60, 60, 70);

    // 版权信息（底部）
    const char* dev = "Developed by 大懒虫工作室, 228407354@qq.com";
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, dev,
        (float)(480 - 320) / 2.0f, 320.0f - 24.0f, 14.0f, 480.0f, 320.0f, 60, 60, 70);

    r3d_end(r3d);
}
