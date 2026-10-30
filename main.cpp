#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_vulkan.h>
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
#include "resource_manager.h"

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

/* ---- 2D UI 逻辑分辨率自适应（仿 joy_topdown_template 的 presentation_set_adaptive）----
   逻辑高固定 320（设计高度，UI 按 480x320 设计稿布局），
   逻辑宽 = 320 * 输出宽/输出高（随屏幕宽高比变化，并裁剪极端值）。
   配 LETTERBOX 呈现后逻辑空间宽高比 == 屏幕宽高比 → 无缝铺满且等比缩放：
     - 圆/文字在任意宽高比（安卓 16:9/20:9）下都不拉伸变形；
     - 左右无留边，控件可由各 layer 锚定到屏幕真实边缘。
   每帧调用以自动处理旋转/全屏切换（仅逻辑宽变化时才重设）。 */
static int g_ui_logical_w = 0;

// 取真实输出尺寸(横屏游戏的宽高比来源)。
// 注意：移动端启动阶段 SDL_GetWindowSize / GPU swapchain 常停留在窗口创建尺寸(480x320)，
// 用它算出的自适应逻辑宽会卡在 480，导致 480x320 逻辑区在宽屏上被 letterbox 居中、左右出现
// 大留边，摇杆被推离屏幕左缘(PC 上窗口本身即 480x320 故看不出)。故改以“当前显示模式”
// 作宽高比来源——它稳定反映真实屏幕分辨率，不受创建尺寸/ swapchain 初始尺寸影响。
static void get_real_output_size(int* out_w, int* out_h) {
    int w = 0, h = 0;
    if (ctx && ctx->window) {
        SDL_Window* raw = (SDL_Window*)get_raw_window(ctx->window);
        if (raw) {
            SDL_DisplayID d = SDL_GetDisplayForWindow(raw);
            SDL_DisplayMode m;
            if (d && SDL_GetCurrentDisplayMode(d, &m) && m.w > 0 && m.h > 0) {
                w = m.w; h = m.h;
            }
        }
    }
    if (w <= 0 || h <= 0) r3d_get_window_size(ctx->r3d, &w, &h);
    if (w <= 0 || h <= 0) { w = 480; h = 320; }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void apply_adaptive_logical_presentation(void) {
    if (!ctx || !ctx->r3d) return;
    int ow = 0, oh = 0;
    get_real_output_size(&ow, &oh);
    // 横屏游戏：以较大值为宽、较小值为高，消除竖/横屏显示模式报告方向差异
    int pw = (ow >= oh) ? ow : oh;
    int ph = (ow >= oh) ? oh : ow;

    const int design_h = 320;
    // 仅对战场景开启自适应宽度；其余场景固定 480（自动居中）
    int lw = 480;
    if (ctx->ui_adaptive_layout) {
        lw = (int)((float)design_h * (float)pw / (float)ph + 0.5f);
        if (lw < 480)  lw = 480;    // 竖长窗口兜底
        if (lw > 1200) lw = 1200;   // 超宽窗口兜底
    }

    if (lw != g_ui_logical_w) {
        g_ui_logical_w = lw;
        r3d_set_logical_presentation_mode(ctx->r3d, lw, design_h, JOY_LOGICAL_PRESENTATION_LETTERBOX);
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

#if defined(__ANDROID__)
    // 移动端固定横屏：必须在创建窗口前设置。SDL 创建窗口时会据此调 Android 的
    // setRequestedOrientation()；若不设且窗口带 RESIZABLE 标志，Java 侧按 FULL_USER 处理，
    // 会覆盖 AndroidManifest 里的 screenOrientation，导致仍可能竖屏。
    // 指定两个横屏方向 → Java 侧取 USER_LANDSCAPE（仅横屏、可左右翻转）。
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    ctx->window = create_window("project_proxy3d", 480, 320);

    // 设置窗口图标（logo.png 位于 textures 目录下）
    window_set_icon(ctx->window, "textures/logo.png");

    // 应用全屏设置
    if (ctx->settings.fullscreen) {
        window_set_fullscreen(ctx->window, true);
    }

    // 诊断探针：
    // 1) 先单独调用 SDL_Vulkan_LoadLibrary 抓 loader/WSI 层错误（缺 VK_KHR_android_surface / 设备无 Vulkan）；
    // 2) 再调用 SDL_GPUSupportsProperties 抓实例/设备层错误（缺必需实例扩展）。
    //    该函数不会覆盖 SDL_GetError，故能保留 PrepareDriver 内部的具体原因（无需 logcat 即可定位）。
    char vk_diag[256] = { 0 };
    if (!SDL_Vulkan_LoadLibrary(NULL)) {
        SDL_strlcpy(vk_diag, SDL_GetError(), sizeof(vk_diag));
    } else {
        SDL_Vulkan_UnloadLibrary();
        SDL_PropertiesID probe_props = SDL_CreateProperties();
        if (probe_props) {
            SDL_SetBooleanProperty(probe_props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
            SDL_SetBooleanProperty(probe_props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
            SDL_SetBooleanProperty(probe_props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
            SDL_SetBooleanProperty(probe_props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
            SDL_SetBooleanProperty(probe_props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
            if (!SDL_GPUSupportsProperties(probe_props)) {
                const char* e = SDL_GetError();
                SDL_strlcpy(vk_diag, (e && *e) ? e : "(backend 不支持，但无具体错误串)", sizeof(vk_diag));
            } else {
                SDL_strlcpy(vk_diag, "(GPU backend 应通过；问题在 r3d_create 内部着色器/管线)", sizeof(vk_diag));
            }
            SDL_DestroyProperties(probe_props);
        } else {
            SDL_strlcpy(vk_diag, "(无法创建属性对象)", sizeof(vk_diag));
        }
    }

    // r3d 为进程级资源：所有模式统一创建一次（服务端也渲染场景、可返回主菜单）
    ctx->r3d = r3d_create(ctx->window, "shaders");
    if (!ctx->r3d) {
        log_error("r3d_create failed (GPU unavailable or shaders missing in shaders)");
        const char* sdlerr = SDL_GetError();
        char msg[768];
        SDL_snprintf(msg, sizeof(msg),
            "r3d_create 失败。\n\nSDL 最后错误：\n%s\n\nVulkan 探针：\n%s\n\n"
            "• 探针含 \"VK_KHR_android_surface\" → 驱动未实现安卓 surface 扩展（SDL_GPU 仅 Vulkan、无 OpenGL 回退，无法用）；\n"
            "• 探针含 \"Required Vulkan instance extension\" → 缺必需实例扩展（如 VK_KHR_get_physical_device_properties2）；\n"
            "• 探针含 \"Vulkan is not loaded\"/\"Could not load\" → 设备无可用 Vulkan；\n"
            "• 探针显示 (Vulkan loaded OK)/(GPU backend 应通过) → 加载层正常，问题在着色器/管线创建，需进一步日志。",
            (sdlerr && *sdlerr) ? sdlerr : "(无错误信息)", vk_diag);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "无法启动 3D 渲染", msg, NULL);
        return SDL_APP_FAILURE;
    }
    // 清屏色统一为白色（UI 场景与 game_scene 均为白底蓝图风）
    r3d_set_clear_color(ctx->r3d, 255, 255, 255);
    // 设置 2D UI 逻辑分辨率（自适应宽度 + LETTERBOX 等比铺满，随后每帧自动刷新）
    apply_adaptive_logical_presentation();
    // 初始化字体/资源：统一交给 ResourceManager（仿 joy_topdown_template 的 AssetManager 思路），
    // 按可执行文件目录稳健解析资源路径，不再依赖运行时 CWD。详细日志见 [ResourceManager]。
    ResourceManager::Init(ctx->r3d);
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
    // 每帧刷新自适应逻辑分辨率（处理旋转/全屏切换导致的屏幕宽高比变化）
    apply_adaptive_logical_presentation();
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
