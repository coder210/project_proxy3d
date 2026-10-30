#pragma once
#include <joy/joy.h>
#include <math.h>
#include <stdlib.h>

/* 白底 UI 的背景闪烁粒子(仿 project_proxy2d main_scene):
   白底上若干深灰小点缓慢漂移, 用 r3d_draw_rect_2d 以逻辑坐标绘制 */
#define UI_PARTICLES_MAX 30

struct ui_particles {
	float x[UI_PARTICLES_MAX], y[UI_PARTICLES_MAX];
	float vx[UI_PARTICLES_MAX], vy[UI_PARTICLES_MAX];
	float alpha[UI_PARTICLES_MAX], size[UI_PARTICLES_MAX];
	float phase[UI_PARTICLES_MAX], twinkle[UI_PARTICLES_MAX];
};

static inline void ui_particles_init(ui_particles* p, int logical_w, int logical_h)
{
	for (int i = 0; i < UI_PARTICLES_MAX; i++) {
		p->x[i] = (float)(rand() % logical_w);
		p->y[i] = (float)(rand() % logical_h);
		p->vx[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * 6.0f;
		p->vy[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * 6.0f;
		p->alpha[i] = 0.08f + (float)rand() / (float)RAND_MAX * 0.15f;
		p->size[i] = 1.5f + (float)rand() / (float)RAND_MAX * 1.5f;
		p->phase[i] = (float)rand() / (float)RAND_MAX * 6.2832f;
		p->twinkle[i] = 0.8f + (float)rand() / (float)RAND_MAX * 1.5f;
	}
}

static inline void ui_particles_update(ui_particles* p, float dt, int logical_w, int logical_h)
{
	for (int i = 0; i < UI_PARTICLES_MAX; i++) {
		p->x[i] += p->vx[i] * dt;
		p->y[i] += p->vy[i] * dt;
		if (p->x[i] < 0) p->x[i] += (float)logical_w;
		if (p->x[i] > logical_w) p->x[i] -= (float)logical_w;
		if (p->y[i] < 0) p->y[i] += (float)logical_h;
		if (p->y[i] > logical_h) p->y[i] -= (float)logical_h;
	}
}

static inline void ui_particles_draw(jr3d_p r, render_mesh_p m, ui_particles* p,
									 float logical_w, float logical_h)
{
	for (int i = 0; i < UI_PARTICLES_MAX; i++) {
		r3d_draw_rect_2d(r, m, p->x[i], p->y[i], p->size[i], p->size[i],
						 150, 150, 160, logical_w, logical_h);
	}
}
