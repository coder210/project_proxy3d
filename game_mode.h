#ifndef GAME_MODE_H
#define GAME_MODE_H

// 前向声明（不定义完整类型；game_scene_p 即 GameScene*，供 GameModeInterface 回调使用）
class GameScene;
typedef GameScene* game_scene_p;

// 模式接口：客户端和服务端各自实现 on_* 回调
typedef struct {
	void (*init)(game_scene_p);
	void (*handle_event)(game_scene_p, const void*);
	void (*fixed_update)(game_scene_p, float fixed_step); /* Unity FixedUpdate 对应：每固定步长的输入收集/转发 */
	void (*update)(game_scene_p, float dt);               /* Unity Update 对应：每帧网络 pump/命令解包/插值 */
	void (*destroy)(game_scene_p);
} GameModeInterface;

extern GameModeInterface g_single_mode;
extern GameModeInterface g_host_mode;
extern GameModeInterface g_client_mode;
extern GameModeInterface g_server_mode;

#endif