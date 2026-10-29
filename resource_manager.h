#pragma once
#include <joy/joy.h>

// 资源集中管理：仿 joy_topdown_template 的 AssetManager 思路，但使用本项目的 r3d 渲染 API。
// 字体 / 贴图的加载只走这一处，路径按"可执行文件目录"稳健解析，
// 不再依赖运行时工作目录(CWD)，避免从其它目录启动 exe 时资源全部找不到。
class ResourceManager {
public:
    // 初始化：集中加载字体（按 exe 目录稳健解析）。纹理走 r3d_load_texture_png 的兜底。
    // 返回是否成功加载字体（失败仅打日志，游戏仍可运行，文字位置留空）。
    static bool Init(jr3d_p r3d);

    // 把相对路径解析为首个"存在且可读"的路径；都失败则返回原路径（交由下游兜底成色块/失败）。
    // 解析顺序：CWD 相对 → exe 目录相对 → exe 目录/assets/ 相对。
    static const char* Resolve(const char* relPath);

    static void Destroy();

private:
    static bool inited_;
    static char resolved_buf_[2048];
};
