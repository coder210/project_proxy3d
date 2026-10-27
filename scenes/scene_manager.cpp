#include "scene_manager.h"
#include "context.h"
#include "scene.h"

// 接管 next 为当前场景：先清理旧场景资源，再加载新场景，最后删除旧对象
static void adopt(std::unique_ptr<Scene>& slot, Scene* next) {
    if (!next) return;
    if (slot) slot->Destroy();
    next->Load();
    slot.reset(next);
}

void SceneManager::push(Scene* next) {
    adopt(current_, next);
}

void SceneManager::change(Scene* next) {
    adopt(current_, next);
}

void SceneManager::setPending(Scene* next) {
    pending_.reset(next);
}

void SceneManager::requestTransition(Scene* next, int code) {
    if (!next) return;
    pending_.reset(next);
    push_user_event(TRANSITION_EVENT, code);
}

void SceneManager::processPending() {
    if (!pending_) return;
    // 先清理旧场景资源，再加载新场景，最后接管（删除旧对象）
    if (current_) current_->Destroy();
    pending_->Load();
    current_ = std::move(pending_);
}

void SceneManager::HandleEvent(const SDL_Event* e) {
    if (!current_ || !e) return;
    // 模拟原 joy 的双回调：先场景级事件，再 UI 级事件
    current_->HandleEvent((const void*)e);
    current_->HandleUiEvent((const void*)e);
}

void SceneManager::Update(float dt) {
    if (!current_) return;
    // 固定步长累加（复刻 joy C scene 的 fixed_step 调度），驱动帧同步锁步逻辑
    if (current_->fixed_step() > 0.0f) {
        current_->fixed_accum_ += dt;
        while (current_->fixed_accum_ >= current_->fixed_step()) {
            current_->fixed_accum_ -= current_->fixed_step();
            current_->FixedUpdate(current_->fixed_step());
        }
    }
    current_->Update(dt);
}

void SceneManager::Render(jr3d_p r3d) {
    (void)r3d;  // 场景内部统一经 ctx_->r3d 取渲染器
    if (current_) current_->Render();
}

void SceneManager::reset() {
    // 必须先 Destroy 再释放：否则旧场景（如 GameScene）的资源不会被清理，
    // 其 static 纹理缓存会留下指向已释放纹理的悬空指针（与 push/change 的
    // adopt() 行为保持一致）。原 joy C 的 R 键"重建场景"只 destroy 管理器，
    // 旧场景 Destroy 未被调用，导致泄漏；此处显式 Destroy 后释放。
    if (current_) { current_->Destroy(); current_.reset(); }
    if (pending_) { pending_.reset(); }
}

const char* SceneManager::currentName() const {
    return current_ ? current_->name() : "";
}
