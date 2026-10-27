#ifndef HUD_PASS_H
#define HUD_PASS_H

struct Context;

// HUD pass：调试 HUD + 虚拟操作层（方向键/攻击键）叠加在最上层
void hud_pass(Context* ctx);

#endif