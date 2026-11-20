#include "clr_host.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_loadso.h>
#include <joy/joy.h>
#include "../third_party/joy/modules/cs/joy_csharp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* ---- C# 瘦宿主状态（照 joy examples/csharp_host）---- */
static csx_p        s_csx = NULL;
static csx_method_p s_on_init = NULL;
static csx_method_p s_on_frame = NULL;
static csx_method_p s_on_shutdown = NULL;

static int    s_smoke_frames = 0;
static int    s_frame = 0;
static Uint64 s_last_tick_ns = 0;
static bool   s_ready = false;

static const int APP_W = 480;
static const int APP_H = 320;

static int find_arg(int argc, char* argv[], const char* name)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return i;
    return -1;
}

static bool setup_clr_dirs()
{
#if defined(_WIN32)
    _putenv_s("MONO_LIB_DIR", "lib");
#else
    setenv("MONO_LIB_DIR", "lib", 1);
#endif
    return true;
}

/* 让 SDL3 的 Vulkan 后端发现随程序部署的 SwiftShader ICD。
 * 三个文件由 CMake POST_BUILD 放到 exe 目录；这里从 argv[0] 取 exe 目录，
 * 拼出 vk_swiftshader_icd.json 的绝对路径设进 VK_ICD_FILENAMES。 */
static void setup_vulkan_dirs(const char* argv0)
{
    if (!argv0 || !*argv0) return;
    const char* sep = NULL;
    for (const char* p = argv0; *p; ++p)
        if (*p == '/' || *p == '\\') sep = p;
    if (!sep) return; // 经 PATH 启动，ICD 与 exe 同目录时 CWD 通常已是该目录
    size_t n = (size_t)(sep - argv0);
    if (n == 0 || n >= 1024) return;
    char dir[1024], icd[1080];
    std::memcpy(dir, argv0, n);
    dir[n] = '\0';
    std::snprintf(icd, sizeof(icd), "%s%cvk_swiftshader_icd.json", dir,
                  (sep[0] == '\\' || sep[0] == '/') ? sep[0] : '/');
#if defined(_WIN32)
    _putenv_s("VK_ICD_FILENAMES", icd);
#else
    setenv("VK_ICD_FILENAMES", icd, 1);
#endif
    SDL_Log("[cs] VK_ICD_FILENAMES=%s", icd);
}

bool clr_host_init(int argc, char* argv[])
{
    if (!setup_clr_dirs()) return false;
    setup_vulkan_dirs(argc > 0 ? argv[0] : NULL);

    const char* modeVal = NULL;
    int mi = find_arg(argc, argv, "--mode");
    if (mi >= 0 && mi + 1 < argc) modeVal = argv[mi + 1];
    else for (int a = 1; a < argc; ++a)
        if (std::strncmp(argv[a], "--mode=", 7) == 0) { modeVal = argv[a] + 7; break; }
    if (modeVal) {
#if defined(_WIN32)
        _putenv_s("PROXY_MODE", modeVal);
#else
        setenv("PROXY_MODE", modeVal, 1);
#endif
    }
    int i = find_arg(argc, argv, "--smoke");
    if (i >= 0 && i + 1 < argc) s_smoke_frames = std::atoi(argv[i + 1]);
    if (s_smoke_frames > 0) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    } else {
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60");
    }

    SDL_Log("[cs] creating MonoVM, assembly=JoyGame.dll");
    s_csx = csx_create();
    if (!s_csx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[cs] csx_create failed");
        return false;
    }
    if (!csx_load_assembly(s_csx, "JoyGame.dll")) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[cs] load assembly failed");
        return false;
    }
    s_on_init = csx_find_method(s_csx, "JoyGame", "Host", "OnInit", 3);
    s_on_frame = csx_find_method(s_csx, "JoyGame", "Host", "OnFrame", 1);
    s_on_shutdown = csx_find_method(s_csx, "JoyGame", "Host", "OnShutdown", 0);
    if (!s_on_init || !s_on_frame || !s_on_shutdown) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[cs] Host methods not found");
        return false;
    }

    int w = APP_W, h = APP_H, smoke = s_smoke_frames;
    void* args[3] = { &w, &h, &smoke };
    csx_invoke_method(s_csx, s_on_init, args);

    s_last_tick_ns = SDL_GetTicksNS();
    s_ready = true;
    SDL_Log("[cs] C# 3D host ready");
    return true;
}

SDL_AppResult clr_host_event(SDL_Event* e)
{
    if (!s_ready) return SDL_APP_CONTINUE;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        csx_input_key((int)e->key.scancode, true);
        break;
    case SDL_EVENT_KEY_UP:
        csx_input_key((int)e->key.scancode, false);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        csx_input_mouse_move(e->motion.x, e->motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        csx_input_mouse_button((int)e->button.button, true);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        csx_input_mouse_button((int)e->button.button, false);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        csx_input_wheel(e->wheel.y);
        break;
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult clr_host_iterate()
{
    if (!s_ready) return SDL_APP_FAILURE;

    float dt;
    if (s_smoke_frames > 0) {
        dt = 1.0f / 60.0f;
    } else {
        Uint64 now = SDL_GetTicksNS();
        dt = (float)((double)(now - s_last_tick_ns) / (double)SDL_NS_PER_SECOND);
        s_last_tick_ns = now;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;
    }

    void* args[1] = { &dt };
    csx_invoke_method(s_csx, s_on_frame, args);
    csx_input_end_frame();

    s_frame++;
    if (s_smoke_frames > 0 && s_frame >= s_smoke_frames) {
        std::printf("[cs] smoke finished: frames=%d exit=%d\n", s_frame, csx_host_exit_code());
        std::fflush(stdout);
        return csx_host_exit_code() == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    if (csx_host_quit_requested())
        return csx_host_exit_code() == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    return SDL_APP_CONTINUE;
}

void clr_host_quit()
{
    if (s_csx && s_on_shutdown) csx_invoke_method(s_csx, s_on_shutdown, NULL);
    if (s_on_init) csx_method_free(s_on_init);
    if (s_on_frame) csx_method_free(s_on_frame);
    if (s_on_shutdown) csx_method_free(s_on_shutdown);
    if (s_csx) csx_release(s_csx);
    s_csx = NULL;
    s_ready = false;
}

bool clr_host_quit_requested()
{
    return s_ready && csx_host_quit_requested();
}

int clr_host_exit_code()
{
    return csx_host_exit_code();
}
