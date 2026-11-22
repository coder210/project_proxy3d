#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <cstdint>

#include "hostfxr.h"
#include "coreclr_delegates.h"

#ifndef HOSTFXR_PATH
#error HOSTFXR_PATH must be defined
#endif

// ---- native 导出给 managed 的回调（Godot 式函数指针表）----
// 顺序必须与 ManagedSmoke.cs 的 Initialize 读取顺序一致。
extern "C" __declspec(dllexport) int32_t smoke_add(int32_t a, int32_t b)
{
    std::printf("[native] smoke_add(%d, %d)\n", a, b);
    return a + b;
}

extern "C" __declspec(dllexport) int32_t smoke_log(const char* msg)
{
    std::printf("[native] smoke_log: %s\n", msg ? msg : "(null)");
    return 0;
}

static const void* unmanaged_callbacks[] = {
    reinterpret_cast<const void*>(&smoke_add),
    reinterpret_cast<const void*>(&smoke_log),
};

static std::filesystem::path exe_dir()
{
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(buf).parent_path();
}

static void* load_symbol(HMODULE mod, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(mod, name));
}

int main()
{
    HMODULE fxr = LoadLibraryA(HOSTFXR_PATH);
    if (!fxr) {
        std::fprintf(stderr, "load hostfxr failed: %lu\n", GetLastError());
        return 1;
    }

    auto init = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        load_symbol(fxr, "hostfxr_initialize_for_runtime_config"));
    auto get_delegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        load_symbol(fxr, "hostfxr_get_runtime_delegate"));
    auto close = reinterpret_cast<hostfxr_close_fn>(
        load_symbol(fxr, "hostfxr_close"));

    if (!init || !get_delegate || !close) {
        std::fprintf(stderr, "hostfxr symbols missing\n");
        return 1;
    }

    std::filesystem::path dir = exe_dir();
    std::wstring config = (dir / "ManagedSmoke.runtimeconfig.json").wstring();
    std::wstring assembly = (dir / "ManagedSmoke.dll").wstring();

    hostfxr_handle ctx = nullptr;
    int rc = init(config.c_str(), nullptr, &ctx);
    if (rc < 0 || !ctx) {
        std::fprintf(stderr, "hostfxr_initialize_for_runtime_config failed: 0x%x\n", rc);
        return 1;
    }
    std::printf("[host] CoreCLR initialized via hostfxr\n");

    void* delegate = nullptr;
    rc = get_delegate(ctx, hdt_load_assembly_and_get_function_pointer, &delegate);
    if (rc < 0 || !delegate) {
        std::fprintf(stderr, "hostfxr_get_runtime_delegate failed: 0x%x\n", rc);
        close(ctx);
        return 1;
    }

    auto load_assembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(delegate);

    // 取 Entry.Initialize(IntPtr, int)：普通 public static 方法，签名匹配即可取函数指针。
    using InitializeFn = void (*)(const void**, int32_t);
    InitializeFn initialize = nullptr;
    rc = load_assembly(
        assembly.c_str(),
        L"ManagedSmoke.Entry, ManagedSmoke",
        L"Initialize",
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        reinterpret_cast<void**>(&initialize));
    if (rc < 0 || !initialize) {
        std::fprintf(stderr, "load Initialize failed: 0x%x\n", rc);
        close(ctx);
        return 1;
    }

    // 把函数指针表传给 managed（Godot 式：传首地址 + 字节大小）。
    int32_t table_bytes = static_cast<int32_t>(sizeof(unmanaged_callbacks));
    std::printf("[host] passing callback table: %d bytes, %zu entries\n",
                table_bytes, sizeof(unmanaged_callbacks) / sizeof(unmanaged_callbacks[0]));
    initialize(unmanaged_callbacks, table_bytes);
    std::printf("[host] managed Initialize done\n");

    // 取 Entry.Run()，普通 public static 方法。
    using RunFn = int32_t (*)();
    RunFn run = nullptr;
    rc = load_assembly(
        assembly.c_str(),
        L"ManagedSmoke.Entry, ManagedSmoke",
        L"Run",
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        reinterpret_cast<void**>(&run));
    if (rc < 0 || !run) {
        std::fprintf(stderr, "load Run failed: 0x%x\n", rc);
        close(ctx);
        return 1;
    }

    int result = run();
    std::printf("[host] managed entry returned %d\n", result);

    close(ctx);
    FreeLibrary(fxr);
    return result == 42 ? 0 : 1;
}
