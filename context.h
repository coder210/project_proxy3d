#ifndef CONTEXT_H
#define CONTEXT_H
#include <map>
#include <vector>
#include <unordered_map>
#include <queue>
#include <string>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include "entt.hpp"
#include <joy/joy.h>
#include "protocol.h"
#include "scenes/scene_manager.h"
#include "components/action_state_component.h"
#include "components/id_component.h"
#include "components/ai_component.h"
#include "components/logic_rect_component.h"
#include "components/logic_position_component.h"
#include "components/logic_velocity_component.h"
#include "components/enemy_component.h"
#include "components/player_state_input.h"
#include "components/player_event_input.h"
#include "components/player_component.h"
#include "components/transform_component.h"
#include "components/attack_ray_effect_component.h"
#include "components/health_component.h"
#include "components/spawn_point_component.h"
#include "components/net_component.h"
#include "components/global_state_component.h"
#include "components/collider_component.h"
#include "components/mesh_component.h"
#include "components/static_mesh_component.h"
#include "components/connection_component.h"
#include "components/local_only_component.h"
#include "shared/systems/state_checksum.h"

// 用户设置（自动保存到 settings.cfg，切换模式/进出场景时保留）
struct Settings {
	char  server_ip[64] = "192.168.2.31";
	int   server_port = 30000;
	float bgm_volume = 0.5f;   // 背景音乐音量 0~1
	float sfx_volume = 1.0f;   // 音效音量 0~1
	bool  fullscreen = false;
};

// 保存设置到 settings.cfg（key=value 文本格式）
static void settings_save(const Settings* s)
{
	FILE* fp = fopen("settings.cfg", "w");
	if (!fp) return;
	fprintf(fp, "server_ip=%s\n", s->server_ip);
	fprintf(fp, "server_port=%d\n", s->server_port);
	fprintf(fp, "bgm_volume=%f\n", s->bgm_volume);
	fprintf(fp, "sfx_volume=%f\n", s->sfx_volume);
	fprintf(fp, "fullscreen=%d\n", s->fullscreen ? 1 : 0);
	fclose(fp);
}

// 从 settings.cfg 加载设置（文件不存在时保留默认值）
static void settings_load(Settings* s)
{
	FILE* fp = fopen("settings.cfg", "r");
	if (!fp) return;
	char key[64], value[128];
	// 格式串前导空格跳过行首换行/空白，否则第二行起 key 会带上 '\n' 导致匹配失败
	while (fscanf(fp, " %63[^=]=%127s", key, value) == 2) {
		if (strcmp(key, "server_ip") == 0) {
			snprintf(s->server_ip, sizeof(s->server_ip), "%s", value);
		} else if (strcmp(key, "server_port") == 0) {
			s->server_port = atoi(value);
		} else if (strcmp(key, "bgm_volume") == 0) {
			s->bgm_volume = (float)atof(value);
		} else if (strcmp(key, "sfx_volume") == 0) {
			s->sfx_volume = (float)atof(value);
		} else if (strcmp(key, "fullscreen") == 0) {
			s->fullscreen = (atoi(value) != 0);
		}
	}
	fclose(fp);
}

// 运行模式（单二进制，运行时通过 --mode 参数切换）
enum GameMode {
	MODE_SINGLE = 0,   // 单机：本地推逻辑，不连网络
	MODE_HOST,         // 房主：本地推逻辑 + 广播给其他玩家
	MODE_CLIENT,       // 客户端：连服务端/Host，消费命令 + 渲染
	MODE_SERVER,       // 独立服务端：只推逻辑 + 转发，无本地玩家
};

// 用户自定义事件类型
#define MENU_EVENT    (0x2)
#define ONLINE_EVENT  (0x3)
#define MODE_EVENT    (0x4)
#define TRANSITION_EVENT (0x6)

// 菜单事件码
enum menu_event_code {
	MENU_EVENT_START = 1,
	MENU_EVENT_ONLINE = 2,
	MENU_EVENT_SETTINGS = 3,
	MENU_EVENT_EXIT = 4,
};

// 联机事件码
enum online_event_code {
	ONLINE_EVENT_HOST = 1,
	ONLINE_EVENT_CLIENT = 2,
	ONLINE_EVENT_SERVER = 3,
	ONLINE_EVENT_BACK = 4,
};

// 设置场景事件码
#define SETTINGS_EVENT  (0x5)
enum settings_event_code {
	SETTINGS_EVENT_BACK = 1,
};

// 模式选择事件码（点击按钮直接进入对应模式）
enum mode_event_code {
	MODE_EVENT_SINGLE = 1,
	MODE_EVENT_HOST = 2,
	MODE_EVENT_CLIENT = 3,
	MODE_EVENT_SERVER = 4,
};

// 跨场景过渡事件码
enum transition_event_code {
	TRANSITION_SPLASH_DONE = 1,
	TRANSITION_LOADING_DONE = 2,
};

struct state_snapshot {
	int64_t position_x, position_y, position_z;
};

// 渲染端攻击挥击特效请求：由逻辑层（melee_attack_system）在命中结算后产出，
// 供渲染 pass 消费。字段均为逻辑帧定点数（帧同步确定性），渲染只读、不参与判定。
// 注意：这不是 ECS 组件，仅为逻辑层 → 渲染层之间的借道缓冲，避免在逻辑 system 里建渲染实体。
struct AttackFlashRequest {
	fp_t x, z;          // 攻击者中心（XZ 平面）
	fp_t dir_x, dir_z;  // 刀光朝向（单位向量，定点归一化）
	fp_t range;         // 攻击距离（米）
	bool is_enemy;      // 是否敌方攻击（用于水墨刀光用色区分，仅渲染）
};

// 伤害飘字请求：由逻辑 system 在玩家命中敌人时写入（仅非服务端）。
// 字段均为逻辑帧定点数（帧同步确定性）；渲染只读、不参与判定。
struct DamageTextRequest {
	fp_t x, z;     // 目标（被命中敌人）中心 XZ
	int  amount;   // 造成的伤害数值
	bool kill;     // 是否击杀（爆字加粗加大）
};

// 射线特效请求：由逻辑 system（melee_attack_system 的射线分支）在结算后产出（仅非服务端），
// 供 ray_pass 消费绘制一条瞬时光束。字段为逻辑帧定点数；渲染只读、不参与帧同步判定。
struct RayFlashRequest {
	fp_t x, z;         // 射线起点（XZ 平面，玩家中心）
	fp_t dir_x, dir_z; // 射线朝向（单位向量，定点归一化）
	fp_t range;        // 光束长度（米；命中时为命中点距离，未命中为最大射程）
};

// 击杀反馈请求：由逻辑 system 在击杀时写入（仅非服务端），渲染层据此显示
// "击杀/双杀/三杀"连杀反馈并播放击杀音效。纯客户端表现，不参与帧同步。
struct KillEvent {
	uint64_t time_ms;   // 击杀发生时间（渲染时钟）
	int  combo;         // 连杀计数（2 秒窗口内连续击杀递增）
};

// 战斗音效请求：由逻辑 system 在攻击/命中/击杀/换波时写入（仅非服务端），
// 渲染层消费并播放对应音效。纯客户端表现，不参与帧同步。
enum SfxType {
	SFX_MELEE = 0,   // 近战挥砍
	SFX_BULLET,      // 子弹发射
	SFX_HIT,         // 命中
	SFX_KILL,        // 击杀
	SFX_WAVE,        // 换波
};
struct SfxRequest {
	SfxType type;
};

struct Context {
	GameMode mode = MODE_SINGLE;

	Settings settings;   // 用户设置（切换模式/进出场景时保留）

	bool is_server = false;
	bool is_authority = true;   // 是否权威端（单机/Host/服务端），决定是否执行波次换图与地图重建
	bool running;
	game_timer_t game_timer;
	jwindow_p window;
	jr3d_p r3d = NULL;

	// UI rendering (2D overlay)
	render_mesh_p ui_quad = NULL;  // 单位 quad 用于绘制 UI

	// Meshes
	render_mesh_p mesh_player = NULL;
	render_mesh_p mesh_enemy = NULL;
	render_mesh_p mesh_prop = NULL;
	render_mesh_p mesh_sky = NULL;
	render_mesh_p mesh_floor = NULL;
	render_mesh_p mesh_floor_wire = NULL;  // 线框地面网格
	// 线框网格（"填充+描边"画风的描边 pass 用）
	render_mesh_p mesh_player_wire = NULL;
	render_mesh_p mesh_enemy_wire = NULL;
	render_mesh_p mesh_prop_wire = NULL;
	void* floor_tex = NULL;
	void* logo_tex = NULL;  // splash screen logo texture

	// Network
	netclient_p netclient = NULL;
	netserver_p net_server = NULL;

	// ECS
	entt::registry ecs;
	random_num_p random = NULL;
	entt::entity singleton_net = entt::null;

	// Audio (client-only)
	uint32_t            audio_dev = 0;
	audio_shot_p        step_sound = NULL;
	audio_bgm_p         bgm = NULL;
	audio_shot_p        sfx_melee = NULL;   // 近战挥砍
	audio_shot_p        sfx_bullet = NULL;  // 子弹发射
	audio_shot_p        sfx_hit = NULL;     // 命中
	audio_shot_p        sfx_kill = NULL;    // 击杀
	audio_shot_p        sfx_wave = NULL;    // 换波

	// Input (client)
	int local_dir = 0;
	fp_t local_yaw = fp_zero();   // 朝向（Q32 定点弧度），全程定点计算不含浮点
	std::map<entt::entity, int>  remote_dirs;

	// Server state
	std::map<int, int> server_inputs;
	bool ready = false;
	int server_frameid = 0;
	int server_entity_id = 0;

	uint64_t lastTime = get_ticks();
	int current_input_mask = 0;
	bool attack_triggered = false;
	bool bullet_triggered = false;   // 屏幕"射"按钮完整点击触发一次射击（点按单发）
	bool bullet_prev = false;   // K 键上一帧按下状态：上升沿检测，实现点按单发
	bool ray_triggered = false;   // 屏幕/键盘"射线"触发一次射线（点按单发，hitscan）
	bool ray_prev = false;   // L 键上一帧按下状态：上升沿检测
	float heartbeatTimer = 0.0f;

	std::queue<std::string> command_queue;

	// 最近 1s 内推进的逻辑帧数（逻辑 FPS，固定步长约 20Hz），供 HUD 显示
	int logic_fps = 0;

	// 调试 HUD 开关（F1 切换，仿 joy_topdown_template 的 show_debug_hud）：
	// 控制 Frame/LogicFPS/Enemies/EID 调试行是否显示，默认关闭。
	bool show_debug_hud = false;

	// 是否启用“自适应逻辑宽度”（仿 joy_topdown_template 的 presentation_set_adaptive）。
	// 开启后逻辑宽度 = 320 * 输出宽/输出高（铺满屏幕、无留边，屏幕按钮由 layer 锚定到真实边缘）；
	// 关闭则用固定 480x320 LETTERBOX（自动居中）。仅对战场景开启——
	// 其余全屏场景(主菜单/设置/结算等)文字按 480 居中排布，自适应宽度会导致整体偏左。
	bool ui_adaptive_layout = false;

	// 逻辑层 → 渲染层 借道缓冲：逻辑 system 把攻击挥击特效请求写入，渲染 pass 每帧消费并清空。
	std::vector<AttackFlashRequest> attack_flashes;

	// 射线特效请求：由 melee_attack_system 射线分支写入（仅玩家），ray_pass 每帧消费并清空。
	std::vector<RayFlashRequest> ray_effects;

	// 伤害飘字请求：仅玩家造成伤害时由逻辑 system 写入（目标位置 XZ + 数值 + 是否击杀），
	// 由 damage_pass 消费生成世界空间飘字（纯渲染，不参与帧同步判定）。
	std::vector<DamageTextRequest> damage_texts;

	// 击杀反馈请求：由逻辑 system 在击杀时写入（仅非服务端），渲染层消费显示连杀并播击杀音效。
	std::vector<KillEvent> kill_events;
	int kill_combo = 0;            // 连杀计数（客户端本地，2 秒窗口内连续击杀递增）
	uint64_t last_kill_ms = 0;     // 上次击杀时间（渲染时钟，客户端本地）

	// 战斗音效请求：由逻辑 system 在攻击/命中/击杀/换波时写入（仅非服务端），渲染层消费播放。
	std::vector<SfxRequest> sfx_requests;

	// Server-side buffers
	std::vector<S2CPlayerJoin> player_joins;
	std::vector<S2CPlayerLeave> player_leaves;
	std::vector<S2CPlayerInput> player_inputs;
	std::map<int, std::string> commands;
	std::map<int, std::string> world_snapshots;
	S2CEntity entities[512];
	int num_entities = 0;
	std::string map_reload_data;   // 波次换图待广播的 S2C_CMD_MAP_RELOAD 数据（空=无）

	// ========== 帧同步校验记录缓冲（性能优化：不实时打印，退出时落盘）==========
	// 每帧一条：帧号、服务端下发 checksum、本地校验 checksum、本帧各玩家输入、本帧实体快照。
	// 正常帧静默追加，仅 MISMATCH 帧实时 log_error 告警；全部攒在内存，退出时写文件。
	// 结构定义见 state_checksum.h（client/server 共用同一 ChecksumRecord）。
	std::vector<ChecksumRecord> checksum_records;

	// 客户端按帧对齐的换关信号：MAP_RELOAD 带 frame_id，先缓存，
	// 待本地逻辑帧推进到该帧号才确定性重建障碍与敌人（新关卡 level_build_layout）
	struct PendingMapReload {
		int level = 0;
	};
	std::map<int, PendingMapReload> map_reload_pending;

	// Client prediction
	uint32_t local_conv = 0;
	int selected_char_id = 0;   // 选角色页选中的角色（CharacterDef 索引），跨场景保留
     char selected_nickname[32] = "";   // 选角色页输入的昵称，跨场景保留
     int selected_level = 1;     // 选关页选中的固定地图关卡（1 基，level_maps.h），跨场景保留
	std::map<int, std::map<int, state_snapshot>> snapshots;
	std::vector<int> pending_inputs;

	// Fixed timestep
	float accumulator = 0.0f;
	float FIXED_TIMESTEP = 1.0f / 20.0f;

	// Scene manager（C++ SceneManager，对齐 joy_side_template 阶段 3）
	SceneManager* scene_mgr = NULL;

	// Systems
	std::vector<struct SystemEntry> systems;
};

// 本地玩家的攻击/射击冷却剩余比例 (0..1 = 剩余/总量)。供键盘鼠标与屏幕按钮路径共用，
// 仅用于本地输入节流，不参与帧同步判定（帧同步端由逻辑系统统一结算冷却）。
inline float local_attack_cooldown_ratio(Context* ctx, bool bullet)
{
	if (!ctx || ctx->local_conv == 0) return 0.0f;
	auto view = ctx->ecs.view<PlayerComponent, ActionStateComponent>();
	for (auto e : view) {
		auto& pc  = view.get<PlayerComponent>(e);
		if (pc.conv != (int)ctx->local_conv) continue;
		auto& act = view.get<ActionStateComponent>(e);
		if (bullet) {
			if (act.bullet_cooldown <= 0) return 0.0f;
			return (float)act.bullet_cooldown / (float)ActionStateComponent::BULLET_COOLDOWN;
		} else {
			if (act.melee_cooldown <= 0) return 0.0f;
			return (float)act.melee_cooldown / (float)ActionStateComponent::MELEE_COOLDOWN;
		}
	}
	return 0.0f;
}

// 本地玩家的射线冷却剩余比例 (0..1)。供键盘/屏幕按钮路径共用，仅本地输入节流。
inline float local_ray_cooldown_ratio(Context* ctx)
{
	if (!ctx || ctx->local_conv == 0) return 0.0f;
	auto view = ctx->ecs.view<PlayerComponent, ActionStateComponent>();
	for (auto e : view) {
		auto& pc  = view.get<PlayerComponent>(e);
		if (pc.conv != (int)ctx->local_conv) continue;
		auto& act = view.get<ActionStateComponent>(e);
		if (act.ray_cooldown <= 0) return 0.0f;
		return (float)act.ray_cooldown / (float)ActionStateComponent::RAY_COOLDOWN;
	}
	return 0.0f;
}

// ========== System dispatch infrastructure ==========
enum Phase { OnStart, PreUpdate, OnUpdate, PostUpdate };

struct SystemEntry {
	std::string name;
	Phase phase;
	std::function<void(entt::registry&, Context*, float)> fn;
};

inline void add_system(Context* ctx, Phase phase, const char* name,
	void (*fn)(entt::registry&, Context*, float))
{
	ctx->systems.push_back({ name, phase, fn });
}

inline void run_phase(Context* ctx, Phase phase, float dt)
{
	for (auto& sys : ctx->systems) {
		if (sys.phase == phase) {
			sys.fn(ctx->ecs, ctx, dt);
		}
	}
}

// 权威端（Server/Host）逐帧推进逻辑：从命令队列取一帧命令 → 设置 NetComponent → 跑三个阶段。
// 必须在 fixed_update 里调用，使逻辑推进与渲染帧率解耦（固定 20Hz）：
// 若在 update（渲染帧率）里推进，渲染掉帧时命令积压会产生"过期 checksum"，
// 客户端按新状态重算后与服务端旧 checksum 比对 → MISMATCH。
inline void process_command_authority(Context* ctx)
{
	if (ctx->command_queue.empty()) return;
	std::string command_data = ctx->command_queue.front();
	ctx->command_queue.pop();

	int frame_id, jn = 0, ln = 0, in = 0;
	S2CPlayerJoin joins[32];
	S2CPlayerLeave leaves[32];
	S2CPlayerInput inputs[64];
	bool ok = unpack_command(command_data.c_str(), (int)command_data.size(),
		&frame_id, joins, 32, &jn, leaves, 32, &ln, inputs, 64, &in);
	if (ok) {
		auto& cmd = ctx->ecs.ctx().get<NetComponent>();
		cmd.frame_id = frame_id;
		cmd.num_joins = jn;
		cmd.num_leaves = ln;
		cmd.num_inputs = in;
		memcpy(cmd.leaves, leaves, ln * sizeof(S2CPlayerLeave));
		memcpy(cmd.joins, joins, jn * sizeof(S2CPlayerJoin));
		if (cmd.num_inputs > 0) {
			memcpy(cmd.inputs, inputs, in * sizeof(S2CPlayerInput));
		}
	}

	run_phase(ctx, PreUpdate, ctx->FIXED_TIMESTEP);
	run_phase(ctx, OnUpdate, ctx->FIXED_TIMESTEP);
	run_phase(ctx, PostUpdate, ctx->FIXED_TIMESTEP);
}

static inline int get_entity_id(Context* ctx) {
	auto& gs = ctx->ecs.ctx().get<GlobalState>();
	return gs.next_entity_id++;
}

// 运行时切换模式：重置所有瞬时状态（ECS/系统注册/网络/命令缓冲），
// 保证可反复进出游戏场景而不重复注册系统或重复 emplace 单例组件。
static void reset_context_for_mode(Context* ctx, GameMode mode)
{
	if (ctx->netclient) { netclient_destroy(ctx->netclient); ctx->netclient = NULL; }
	if (ctx->net_server) { netserver_destroy(ctx->net_server); ctx->net_server = NULL; }

	ctx->ecs = entt::registry();   // 全新 registry（含 ctx() 上下文，避免重复 emplace 报错）
	ctx->systems.clear();

	ctx->command_queue = std::queue<std::string>();
	ctx->commands.clear();
	ctx->world_snapshots.clear();
	ctx->map_reload_data.clear();
	ctx->map_reload_pending.clear();
	ctx->player_joins.clear();
	ctx->player_leaves.clear();
	ctx->player_inputs.clear();
	ctx->server_inputs.clear();
	ctx->snapshots.clear();
	ctx->pending_inputs.clear();
	ctx->checksum_records.clear();
	ctx->damage_texts.clear();
	ctx->kill_events.clear();
	ctx->kill_combo = 0;
	ctx->last_kill_ms = 0;
	ctx->sfx_requests.clear();
	ctx->ray_effects.clear();

	ctx->ready = false;
	ctx->server_frameid = 0;
	ctx->local_conv = 0;
	ctx->num_entities = 0;
	ctx->accumulator = 0.0f;
	ctx->current_input_mask = 0;
	ctx->attack_triggered = false;
	ctx->bullet_triggered = false;
	ctx->bullet_prev = false;
	ctx->ray_triggered = false;
	ctx->ray_prev = false;
	ctx->local_dir = 0;
	ctx->local_yaw = fp_zero();
	ctx->heartbeatTimer = 0.0f;

	ctx->mode = mode;
	ctx->is_server = (mode == MODE_SERVER);
	ctx->is_authority = (mode != MODE_CLIENT);
}

#endif