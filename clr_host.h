#pragma once

#include <SDL3/SDL.h>

/// <summary>
/// C# 3D 瘦宿主：创建 MonoVM、加载 JoyGame.dll、反射调用 GameApplication.Initialize/Update/Shutdown、
/// 把 SDL 事件写进 cs 输入桥。窗口与 r3d 由 C# 侧 GameApp3D 自建。
/// main.cpp 的四个 SDL 回调全部转发到这里（游戏仅此一条 C# 路径）。
/// </summary>
bool clr_host_init(int argc, char* argv[]);
SDL_AppResult clr_host_event(SDL_Event* e);
SDL_AppResult clr_host_iterate();
void clr_host_quit();
bool clr_host_quit_requested();
int clr_host_exit_code();
