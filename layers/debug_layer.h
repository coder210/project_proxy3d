#ifndef DEBUG_LAYER_H
#define DEBUG_LAYER_H

#include <joy/joy.h>

struct Context;

void debug_layer_render_hud(Context* ctx, render_mesh_p quad);

#endif