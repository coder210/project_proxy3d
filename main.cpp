#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL.h>
#include <joy/joy.h>
#include <cstring>
#include "context.h"
#include "scenes/scene_manager.h"
#include "scenes/splash_scene.h"
#include "scenes/main_menu_scene.h"
#include "scenes/online_scene.h"
#include "scenes/loading_scene.h"
#include "scenes/settings_scene.h"
#include "scenes/character_select_scene.h"
#include "scenes/game_scene.h"
#include "scenes/victory_scene.h"

static Context* ctx;

// 解析 --mode=single|host|client|server（默认 single）
static GameMode parse_mode(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--mode=", 7) == 0) {
            const char* m = argv[i] + 7;
            if (strcmp(m, "host") == 0)   return MODE_HOST;
            if (strcmp(m, "client") == 0) return MODE_CLIENT;
            if (strcmp(m, "server") == 0) return MODE_SERVER;
            if (strcmp(m, "single") == 0) return MODE_SINGLE;
        }
    }
    return MODE_SINGLE;
}

// 进入游戏场景（通过加载场景过渡，支持运行时切换模式与反复进出）
static void enter_game_scene(GameMode mode) {
    // 服务端无本地玩家，直接进加载；单机/主机/客户端先进选角色页
    if (mode == MODE_SERVER) {
        ctx->scene_mgr->change(new LoadingScene(ctx, mode));
    } else {
        ctx->scene_mgr->change(new CharacterSelectScene(ctx, mode));
    }
}

// 处理过渡事件（splash/loading 完成后切换场景）
static void handle_transition_event(SDL_Event* e) {
    if (!is_user_event(e, TRANSITION_EVENT)) return;
    switch (get_event_code(e)) {
    case TRANSITION_SPLASH_DONE:
    case TRANSITION_LOADING_DONE:
        ctx->scene_mgr->processPending();
        break;
    }
}

extern "C" SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    (void)appstate;
    sys_init_netenv();

    if (!init_env()) {
        log_info("SDL init failed: %s", get_error());
        return SDL_APP_FAILURE;
    }

    ctx = new Context();
    ctx->mode = parse_mode(argc, argv);
    settings_load(&ctx->settings);
    log_info("settings loaded: ip=%s port=%d",
        ctx->settings.server_ip, ctx->settings.server_port);

    game_timer_init(&ctx->game_timer);
    game_timer_set_target_fps(&ctx->game_timer, 60);

    ctx->window = create_window("project_proxy3d", 480, 320);

    // 设置窗口图标（logo.png 位于 textures 目录下）
    window_set_icon(ctx->window, "textures/logo.png");

    // 应用全屏设置
    if (ctx->settings.fullscreen) {
        window_set_fullscreen(ctx->window, true);
    }

    // r3d 为进程级资源：所有模式统一创建一次（服务端也渲染场景、可返回主菜单）
    ctx->r3d = r3d_create(ctx->window, "shaders");
    if (!ctx->r3d) {
        log_error("r3d_create failed (GPU unavailable or shaders missing in shaders)");
        const char* sdlerr = SDL_GetError();
        char msg[640];
        SDL_snprintf(msg, sizeof(msg),
            "r3d_create 失败。SDL 最后错误：\n%s\n\n"
            "• 若含 \"Could not create GPU device\" / \"vkCreateInstance\" → 该设备 Vulkan 不可用或驱动不兼容；\n"
            "• 若含 \"CreateGPUShader\" / \"SPIR-V\" → 着色器 SPIR-V 在该 Vulkan 驱动（Mali 等移动 GPU）编译失败。",
            (sdlerr && *sdlerr) ? sdlerr : "(无错误信息)");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "无法启动 3D 渲染", msg, NULL);
        return SDL_APP_FAILURE;
    }
    // 清屏色统一为白色（UI 场景与 game_scene 均为白底蓝图风）
    r3d_set_clear_color(ctx->r3d, 255, 255, 255);
    // 设置逻辑分辨率 480x320：所有 2D UI 以逻辑坐标布局，窗口缩放时等比自适应（与 project_proxy2d 一致）
    r3d_set_logical_presentation(ctx->r3d, 480, 320);
    // 初始化字体（用于 UI 文字渲染）
    bool font_ok = r3d_font_init(ctx->r3d, "fonts/simhei.ttf", 24);
    if (!font_ok) {
        log_error("failed to init font: fonts/simhei.ttf");
    }
    // 创建 UI 用的单位 quad 网格
    ctx->ui_quad = r3d_mesh_quad(ctx->r3d);

    ctx->running = true;
    ctx->scene_mgr = new SceneManager();

    if (ctx->mode == MODE_SERVER) {
        // 独立服务端：直接进入游戏场景（无菜单）
        reset_context_for_mode(ctx, MODE_SERVER);
        ctx->scene_mgr->change(new GameScene(ctx));
        log_info("server start");
    } else {
        // 单机/Host/客户端：先进入闪屏画面，然后自动切换到主场景
        ctx->scene_mgr->change(new SplashScene(ctx));
    }

    log_info("project_proxy3d start: mode=%d", (int)ctx->mode);

    return SDL_APP_CONTINUE;
}

extern "C" SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* e) {
    (void)appstate;
    if (should_close(e)) return SDL_APP_SUCCESS;

    // 独立服务端：只响应退出（不处理菜单/场景切换按键），但允许 ESC 返回主场景，
    // 且仍处理主场景的模式按钮事件（ESC 返回后 mode 仍为 SERVER，需能再切换）
    // 同时需处理 loading_scene 推送的 TRANSITION_EVENT，否则从联机菜单选择"服务端"后卡在加载画面
    if (ctx->mode == MODE_SERVER) {
        ctx->scene_mgr->HandleEvent(e);
        if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE) {
            log_info("[server] ESC pressed, back to main");
            ctx->scene_mgr->change(new MainMenuScene(ctx));
        }
        else if (is_user_event(e, TRANSITION_EVENT)) {
            handle_transition_event(e);
        }
        else if (is_user_event(e, MENU_EVENT)) {
            switch (get_event_code(e)) {
            case MENU_EVENT_START:
                enter_game_scene(MODE_SINGLE);
                break;
            case MENU_EVENT_ONLINE:
                ctx->scene_mgr->change(new OnlineScene(ctx));
                break;
            case MENU_EVENT_SETTINGS:
                ctx->scene_mgr->change(new SettingsScene(ctx));
                break;
            case MENU_EVENT_EXIT:
                push_quit_event();
                break;
            }
        }
        else if (is_user_event(e, SETTINGS_EVENT)) {
            switch (get_event_code(e)) {
            case SETTINGS_EVENT_BACK:
                ctx->scene_mgr->change(new MainMenuScene(ctx));
                break;
            }
        }
        else if (is_user_event(e, ONLINE_EVENT)) {
            switch (get_event_code(e)) {
            case ONLINE_EVENT_HOST:   enter_game_scene(MODE_HOST); break;
            case ONLINE_EVENT_CLIENT: enter_game_scene(MODE_CLIENT); break;
            case ONLINE_EVENT_SERVER: enter_game_scene(MODE_SERVER); break;
            case ONLINE_EVENT_BACK:
                ctx->scene_mgr->change(new MainMenuScene(ctx));
                break;
            }
        }
        handle_transition_event(e);
        return SDL_APP_CONTINUE;
    }

    ctx->scene_mgr->HandleEvent(e);

    if (e->type == SDL_EVENT_KEY_DOWN) {
        switch (e->key.scancode) {
        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_RETURN:
        {
            if (strcmp(ctx->scene_mgr->currentName(), "Main") == 0) {
                enter_game_scene(ctx->mode);
            }
            break;
        }
        case SDL_SCANCODE_ESCAPE:
            ctx->scene_mgr->change(new MainMenuScene(ctx));
            break;
        case SDL_SCANCODE_R:
            // 重置场景为游戏初始状态（清场 + 重新加载），保留设置
            ctx->scene_mgr->reset();
            ctx->scene_mgr->change(new MainMenuScene(ctx));
            break;
        }
    }
    else if (is_user_event(e, MENU_EVENT)) {
        switch (get_event_code(e)) {
        case MENU_EVENT_START:
            enter_game_scene(MODE_SINGLE);
            break;
        case MENU_EVENT_ONLINE:
            ctx->scene_mgr->change(new OnlineScene(ctx));
            break;
        case MENU_EVENT_SETTINGS:
            ctx->scene_mgr->change(new SettingsScene(ctx));
            break;
        case MENU_EVENT_EXIT:
            push_quit_event();
            break;
        }
    }
    else if (is_user_event(e, SETTINGS_EVENT)) {
        switch (get_event_code(e)) {
        case SETTINGS_EVENT_BACK:
            ctx->scene_mgr->change(new MainMenuScene(ctx));
            break;
        }
    }
    else if (is_user_event(e, ONLINE_EVENT)) {
        switch (get_event_code(e)) {
        case ONLINE_EVENT_HOST:   enter_game_scene(MODE_HOST); break;
        case ONLINE_EVENT_CLIENT: enter_game_scene(MODE_CLIENT); break;
        case ONLINE_EVENT_SERVER: enter_game_scene(MODE_SERVER); break;
        case ONLINE_EVENT_BACK:
            ctx->scene_mgr->change(new MainMenuScene(ctx));
            break;
        }
    }
    else if (is_user_event(e, TRANSITION_EVENT)) {
        handle_transition_event(e);
    }

    return SDL_APP_CONTINUE;
}

extern "C" SDL_AppResult SDL_AppIterate(void*) {
    game_timer_update(&ctx->game_timer);
    float dt = game_timer_get_delta_time(&ctx->game_timer);
    ctx->scene_mgr->Update(dt);
    ctx->scene_mgr->Render(ctx->r3d);
    return SDL_APP_CONTINUE;
}

extern "C" void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
    delete ctx->scene_mgr;
    settings_save(&ctx->settings);
    if (ctx->r3d) {
        r3d_destroy(ctx->r3d);
    }
    destroy_window(ctx->window);
    delete ctx;
    log_info("project_proxy3d quit");
}
