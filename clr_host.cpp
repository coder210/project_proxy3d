#include "clr_host.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_loadso.h>
#include <joy/joy.h>
#include "third_party/joy/modules/cs/joy_csharp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#  include <direct.h>
#else
#  include <unistd.h>
#endif

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

/* 把 CWD 对齐到 exe 目录（照抄 joy examples 的 align_working_dir）。
 * JoyGame.dll / lib/ / assets 都是相对路径；从仓库根或 IDE 启动时若不对齐，
 * MONO_LIB_DIR="lib" 会解析到错误目录，mono_jit_init() 因找不到 CoreLib 而 abort。 */
static void align_working_dir(const char* argv0)
{
#if defined(__ANDROID__)
    (void)argv0;
#else
    if (!argv0 || !*argv0) return;
    const char* sep = NULL;
    for (const char* p = argv0; *p; ++p)
        if (*p == '/' || *p == '\\') sep = p;
    if (!sep) return; // 经 PATH 启动，CWD 通常已是资源根
    size_t n = (size_t)(sep - argv0);
    if (n == 0 || n >= 1024) return;
    char dir[1024];
    std::memcpy(dir, argv0, n);
    dir[n] = '\0';
#if defined(_WIN32)
    (void)_chdir(dir);
#else
    (void)chdir(dir);
#endif
#endif
}

#if defined(__ANDROID__)
/* APK 的 assets/ 是压缩数据，不是文件系统目录；mono_set_dirs() 与
 * mono_set_assemblies_path() 要真实路径（jcsx.c 用 stat() 校验）。
 * 首次启动把 lib/ 下的 DLL 从 assets 解到内部存储，再把两条路径都指过去。
 * 清单与 CMakeLists.txt 部署的 BCL + Joy.Bindings.dll 保持一致。 */
static const char* const kAndroidClrLibs[] = {
    "System.Private.CoreLib.dll", "netstandard.dll",
    "System.Runtime.dll", "System.Collections.dll",
    "Joy.Bindings.dll", "JoyGame.dll", NULL   /* TPA 也需要 JoyGame.dll 本体 */
};

static bool copy_android_clr_lib(const char* dir, const char* name)
{
    char src[512], dst[512];
    /* BCL/Joy.Bindings 在 assets/lib/；JoyGame.dll 在 assets 根（CMake 部署不同）。 */
    std::snprintf(src, sizeof(src), "lib/%s", name);
    size_t size = 0;
    void* data = SDL_LoadFile(src, &size);
    if (!data) {
        std::snprintf(src, sizeof(src), "%s", name);
        data = SDL_LoadFile(src, &size);
    }
    std::snprintf(dst, sizeof(dst), "%s%s", dir, name);
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[cs] read asset %s failed: %s", src, SDL_GetError());
        return false;
    }
    bool ok = SDL_SaveFile(dst, data, size);
    if (!ok)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[cs] write %s failed: %s", dst, SDL_GetError());
    SDL_free(data);
    return ok;
}
#endif

static bool setup_clr_dirs()
{
#if defined(__ANDROID__)
    char* pref = SDL_GetPrefPath("proxy3d", "project_proxy3d");
    if (!pref) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[cs] SDL_GetPrefPath failed: %s", SDL_GetError());
        return false;
    }
    for (int i = 0; kAndroidClrLibs[i]; ++i) {
        /* 少一个 BCL，mono_jit_init() 就因找不到 CoreLib 直接 abort，必须立刻终止。 */
        if (!copy_android_clr_lib(pref, kAndroidClrLibs[i])) {
            SDL_free(pref);
            return false;
        }
    }
    /* JOY_CLR_LIB_DIR 由 jcsx.c 的 Android mono embedding 读取，传给
     * mono_set_assemblies_path 定位 BCL/JoyGame.dll/Joy.Bindings.dll；
     * MONO_PATH / MONO_LIB_DIR 保留给上游 joy 代码和测试脚本用，都指向同一解包目录。 */
    setenv("JOY_CLR_LIB_DIR", pref, 1);
    setenv("MONO_PATH", pref, 1);
    setenv("MONO_LIB_DIR", pref, 1);
    SDL_Log("[cs] CLR dirs: %s", pref);
    SDL_free(pref);
#elif defined(_WIN32)
    _putenv_s("MONO_LIB_DIR", "lib");
#else
    setenv("MONO_LIB_DIR", "lib", 1);

    /* Linux/macOS 的 CoreLib P/Invoke 目标拆成独立 shim，libcoreclr 无 RUNPATH，
     * dlopen 不搜 exe 目录；按带 "./" 的相对路径预加载（CWD 已对齐 exe 目录），
     * 缺失的忽略。清单与 runtimes/<platform>/ 入库的 libSystem.* 一致。 */
    static const char* const kClrShims[] = {
        "libSystem.Native.so", "libSystem.Native.dylib",
        "libSystem.Globalization.Native.so", "libSystem.Globalization.Native.dylib",
        "libSystem.IO.Compression.Native.so", "libSystem.IO.Compression.Native.dylib",
        "libSystem.Net.Security.Native.so", "libSystem.Net.Security.Native.dylib",
        "libSystem.Security.Cryptography.Native.OpenSsl.so",
        "libSystem.Security.Cryptography.Native.OpenSsl.dylib",
        "libSystem.Security.Cryptography.Native.Apple.dylib",
        NULL
    };
    for (int i = 0; kClrShims[i]; ++i) {
        char path[512];
        std::snprintf(path, sizeof(path), "./%s", kClrShims[i]);
        if (!SDL_LoadObject(path)) SDL_ClearError();
    }
#endif
    return true;
}

/* 让 SDL3 的 Vulkan 后端发现随程序部署的 SwiftShader ICD。
 * 三个文件由 CMake POST_BUILD 放到 exe 目录；align_working_dir() 之后 CWD
 * 已是 exe 目录，直接拼该目录下的 vk_swiftshader_icd.json 绝对路径。 */
static void setup_vulkan_dirs()
{
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) return;
    char sep =
#if defined(_WIN32)
        '\\';
#else
        '/';
#endif
    char icd[1080];
    std::snprintf(icd, sizeof(icd), "%s%cvk_swiftshader_icd.json", cwd, sep);
#if defined(_WIN32)
    _putenv_s("VK_ICD_FILENAMES", icd);
#else
    setenv("VK_ICD_FILENAMES", icd, 1);
#endif
    SDL_Log("[cs] VK_ICD_FILENAMES=%s", icd);
}

bool clr_host_init(int argc, char* argv[])
{
    /* 先对齐 CWD 到 exe 目录：JoyGame.dll / lib/ / assets / SwiftShader ICD
     * 全是相对资源根的路径，宿主不切 CWD 会静默解析到别处。 */
    align_working_dir(argc > 0 ? argv[0] : NULL);
    setup_vulkan_dirs();
    if (!setup_clr_dirs()) return false;

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

    /* C# 调试：运行时为 hostfxr 启动的真 CoreCLR，ICorDebug 诊断通道由
     * DOTNET_EnableDiagnostics=1 激活（见 CMake 生成的 .vcxproj 调试环境），
     * VS 用 NativeWithManagedCore 引擎 F5 即可附加，见 docs/csharp-debugging.md。
     * 旧的 Mono 软调试代理（PROXY_DEBUG_CSHARP / 22222）已随迁移废弃。 */

    SDL_Log("[cs] creating CoreCLR, assembly=JoyGame.dll");
    s_csx = csx_create();
    if (!s_csx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[cs] csx_create failed");
        return false;
    }
    if (!csx_load_assembly(s_csx, "JoyGame.dll")) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[cs] load assembly failed");
        return false;
    }
    s_on_init = csx_find_method(s_csx, "JoyGame", "GameApplication", "Initialize", 3);
    s_on_frame = csx_find_method(s_csx, "JoyGame", "GameApplication", "Update", 1);
    s_on_shutdown = csx_find_method(s_csx, "JoyGame", "GameApplication", "Shutdown", 0);
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
