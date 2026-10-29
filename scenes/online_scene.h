#ifndef ONLINE_SCENE_3D_H
#define ONLINE_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"

// 联机大厅：仿 joy_topdown_template 的 online_scene 版式
//  顶部墨带 + 右上「直连」 / 左侧房间列表 + 右侧详情 / 底部按钮条 / 直连弹窗
class OnlineScene : public Scene {
public:
    OnlineScene(Context* ctx);
    void HandleEvent(const void* ev) override;
    void Load() override;
    void Update(float dt) override;
    void Render() override;
    void Destroy() override;
private:
    void set_direct_open(bool open);
    void direct_connect();

    jr3d_button_p btn_create  = NULL;   // 创建房间(HOST)
    jr3d_button_p btn_join    = NULL;   // 加入房间(CLIENT)
    jr3d_button_p btn_server  = NULL;   // 服务端(SERVER)
    jr3d_button_p btn_back    = NULL;   // 返回(BACK)
    jr3d_button_p btn_direct  = NULL;   // 右上「直连」
    jr3d_button_p btn_cancel  = NULL;   // 弹窗「取消」
    jr3d_button_p btn_connect = NULL;   // 弹窗「连接」
    jr3d_input_p  ip_input    = NULL;
    jr3d_input_p  port_input  = NULL;
    bool direct_open_ = false;
};

#endif
