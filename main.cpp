#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include "clr_host.h"

// 纯 C# 3D 瘦入口：窗口 / r3d / 逻辑 / 场景全部在 JoyGame.dll 中，
// 本宿主只创建 MonoVM、转发 SDL 事件与帧回调到 clr_host_*。
// 原 C++ 游戏路径已完整迁移到 CSharp/ 并移除（--cpp 回退不再存在）。

extern "C" SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    (void)appstate;
    return clr_host_init(argc, argv) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

extern "C" SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* e) {
    (void)appstate;
    return clr_host_event(e);
}

extern "C" SDL_AppResult SDL_AppIterate(void* appstate) {
    (void)appstate;
    return clr_host_iterate();
}

extern "C" void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
    clr_host_quit();
}
