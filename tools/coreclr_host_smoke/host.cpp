#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <string>

#include "hostfxr.h"
#include "coreclr_delegates.h"

#ifndef HOSTFXR_PATH
#error HOSTFXR_PATH must be defined
#endif

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
    void* entry = nullptr;
    rc = load_assembly(
        assembly.c_str(),
        L"ManagedSmoke.Entry, ManagedSmoke",
        L"Run",
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        &entry);
    if (rc < 0 || !entry) {
        std::fprintf(stderr, "load_assembly_and_get_function_pointer failed: 0x%x\n", rc);
        close(ctx);
        return 1;
    }

    auto run = reinterpret_cast<int (*)()>(entry);
    int result = run();
    std::printf("[host] managed entry returned %d\n", result);

    close(ctx);
    FreeLibrary(fxr);
    return result == 42 ? 0 : 1;
}
