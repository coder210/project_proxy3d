#include "resource_manager.h"
#include <SDL3/SDL_filesystem.h>
#include <cstdio>
#include <cstring>

bool ResourceManager::inited_ = false;
char ResourceManager::resolved_buf_[2048] = { 0 };

// 探测文件是否可读（不关心内容，仅判断存在性）
static bool file_readable(const char* p) {
    if (!p || !p[0]) return false;
    size_t sz = 0;
    void* d = SDL_LoadFile(p, &sz);
    if (d) { SDL_free(d); return true; }
    return false;
}

bool ResourceManager::Init(jr3d_p r3d) {
    if (!r3d) return false;

    // 注意: SDL_GetBasePath() 返回的是 SDL 内部缓存指针(const char*),
    // 由 SDL 在退出时(SDL_QuitFilesystem)统一释放，调用方绝不能 SDL_free,
    // 否则会在退出时二次释放导致堆损坏崩溃。
    const char* base = SDL_GetBasePath();
    log_info("[ResourceManager] exeDir='%s'", base ? base : "(null)");

    // 1) 先按 CWD 相对尝试
    const char* cwd_cands[] = {
        "fonts/simhei.ttf",
        "assets/fonts/simhei.ttf",
        "simhei.ttf",
        "data/fonts/simhei.ttf",
        nullptr
    };
    for (int i = 0; cwd_cands[i]; i++) {
        if (r3d_font_init(r3d, cwd_cands[i], 24)) {
            log_info("[ResourceManager] 字体加载成功(CWD): %s", cwd_cands[i]);
            inited_ = true;
            return true;
        }
    }

    // 2) 再按 exe 所在目录相对尝试（无论 CWD 在哪，只要 fonts/ 在 exe 旁边即可）
    if (base) {
        const char* rels[] = {
            "fonts/simhei.ttf",
            "assets/fonts/simhei.ttf",
            "../fonts/simhei.ttf",
            "fonts\\simhei.ttf",
            "assets\\fonts\\simhei.ttf",
            nullptr
        };
        for (int i = 0; rels[i]; i++) {
            char p[2048];
            SDL_snprintf(p, sizeof(p), "%s%s", base, rels[i]);
            if (file_readable(p))
                log_info("[ResourceManager] 候选可读: %s", p);
            if (r3d_font_init(r3d, p, 24)) {
                log_info("[ResourceManager] 字体加载成功(exeDir): %s", p);
                inited_ = true;
                return true;
            }
        }
    }

    log_error("[ResourceManager] 字体加载失败：所有候选路径均未找到 fonts/simhei.ttf。\n"
             "            请确认 exe 同级目录下存在 fonts/simhei.ttf"
             "（CMake 会把 assets/ 拷贝到构建输出目录；若你是从别处启动 exe，"
             "请把 assets/ 目录一起带上，或把 exe 放到含 fonts/ 的目录里运行）。");
    return false;
}

const char* ResourceManager::Resolve(const char* relPath) {
    if (!relPath || !relPath[0]) return relPath;
    // CWD 直接可用
    if (file_readable(relPath)) return relPath;
    // base 由 SDL 管理，不能 free
    const char* base = SDL_GetBasePath();
    if (base) {
        SDL_snprintf(resolved_buf_, sizeof(resolved_buf_), "%s%s", base, relPath);
        if (file_readable(resolved_buf_)) return resolved_buf_;
        SDL_snprintf(resolved_buf_, sizeof(resolved_buf_), "%sassets/%s", base, relPath);
        if (file_readable(resolved_buf_)) return resolved_buf_;
    }
    return relPath; // 都失败，原样返回（下游兜底成色块/失败）
}

void ResourceManager::Destroy() {
    inited_ = false;
}
