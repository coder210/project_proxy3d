#include "floor_pass.h"
#include <joy/joy.h>
#include "../context.h"

void floor_pass(Context* ctx) {
	if (!ctx->r3d) return;

	// 地面：浅灰填充 + 网格线（proxy2d 的图纸底纹）
	// 地板面下沉 0.05 米：避免玩家/障碍脚底(y=0)与地面(y=0)完全共面导致 z-fighting 穿模。
	// 纯渲染调整，不动逻辑位置 y，帧同步不受影响。
	if (ctx->mesh_floor) {
		jcolor_t floor_fill = { 228, 228, 232, 255 };
		r3d_draw_mesh_solid(ctx->r3d, ctx->mesh_floor,
			{ 0, -0.05f, 0 }, { 1, 1, 1 }, floor_fill);
	}
	if (ctx->mesh_floor_wire) {
		jcolor_t floor_line = { 205, 205, 212, 255 };
		r3d_draw_wire(ctx->r3d, ctx->mesh_floor_wire,
			{ 0, -0.05f, 0 }, { 1, 1, 1 }, floor_line);
	}
}