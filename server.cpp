#include <ctime>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include <joy/joy.h>
#include "context.h"
#include "scenes/game_scene.h"
#include "game_mode.h"
#include "protocol.h"
#include "passes/interpolate_position_pass.h"
#include "components/static_mesh_component.h"
#include "systems/input_consume_join_system.h"
#include "systems/input_consume_leave_system.h"
#include "systems/input_consume_input_system.h"
#include "systems/movement_system.h"
#include "systems/cleanup_system.h"
#include "systems/collision_system.h"
#include "systems/ai_system.h"
#include "systems/spawn_obstacles_system.h"
#include "systems/snapshot_system.h"
#include "systems/level_maps.h"
#include "components/connection_component.h"
#include "components/wave_advance_component.h"

// 当前换波待生成剩余延迟（逻辑帧数）；无待生成（敌人已生成/未换波）返回 -1
static int current_wave_delay_ticks(Context* ctx)
{
	auto view = ctx->ecs.view<WaveAdvanceComponent>();
	for (auto e : view)
		return view.get<WaveAdvanceComponent>(e).delay_ticks;
	return -1;
}

// ========== 服务端网络 ==========
static void handle_cmd_loading(Context* ctx, int conv)
{
	log_info("C2S_CMD_LOADING");
	auto view = ctx->ecs.view<ConnectionComponent>();
	entt::entity conn = entt::null;
	for (auto e : view) {
		if (ctx->ecs.get<ConnectionComponent>(e).conv == conv) {
			conn = e;
			break;
		}
	}
	if (conn == entt::null) {
		conn = ctx->ecs.create();
		ctx->ecs.emplace<ConnectionComponent>(conn, conv);
	}
	auto& comp = ctx->ecs.get<ConnectionComponent>(conn);
	comp.is_loaded = false;
	comp.frameid = ctx->ecs.ctx().get<GlobalState>().next_frame_id;
}

static void handle_cmd_player_join(Context* ctx, int conv, int64_t x, int64_t y, int64_t z, int32_t char_id, const char* nickname)
{
	// 固定关卡：玩家出生点由关卡 P 标记确定性决定（替换原先随机撒点，符合求生之路式固定出生）。
	// 仅权威端创建玩家，出生坐标经 loading/命令快照同步给客户端，故无需客户端再算一遍。
	fp_t sx = fp_zero(), sz = fp_zero();
	int level = level_index_for_wave(ctx->ecs.ctx().get<GlobalState>());
	level_player_spawn(level, &sx, &sz);
	x = sx; y = fp_zero(); z = sz;
	S2CPlayerJoin j = make_join(conv, x, y, z, char_id, nickname);
	ctx->player_joins.push_back(j);
}

static void handle_cmd_player_leave(Context* ctx, int conv)
{
	S2CPlayerLeave l = { conv };
	ctx->player_leaves.push_back(l);
}

static void handle_cmd_player_input(Context* ctx, int conv, uint32_t seq, int dir, int attack, int16_t yaw)
{
	log_info("[SERVER] handle_input: conv=%d dir=%d attack=%d yaw=%d", conv, dir, attack, yaw);
	S2CPlayerInput i = { seq, conv, dir, (int8_t)attack, yaw };
	ctx->player_inputs.push_back(i);
}

static void handle_cmd_heartbeat(Context* ctx, int conv)
{
	log_info("%d: C2S_CMD_HEARTBEAT", conv);
	uint64_t ms_ticks;
	utils_get_current_time(&ms_ticks);
	auto view = ctx->ecs.view<ConnectionComponent>();
	entt::entity conn = entt::null;
	for (auto e : view) {
		if (ctx->ecs.get<ConnectionComponent>(e).conv == conv) {
			conn = e;
			break;
		}
	}
	if (conn == entt::null) {
		log_debug("Heartbeat from unknown conv: %d", conv);
		return;
	}
	auto& comp = ctx->ecs.get<ConnectionComponent>(conn);
	comp.health = 10;
	comp.last_heartbeat = (uint32_t)ms_ticks;
}

static void network_receive_server(Context* ctx)
{
	net_message_t msg;
	while (netserver_poll_message(ctx->net_server, &msg)) {
		if (msg.type == NET_TYPE_CONNECTED) {
			log_info("connected=%d", msg.conv);
		}
		else if (msg.type == NET_TYPE_DISCONNECTED) {
			log_info("disconnected=%d", msg.conv);
			S2CPlayerLeave l = { msg.conv };
			ctx->player_leaves.push_back(l);
		}
		else if (msg.type == NET_TYPE_MESSAGE) {
			int cmd, input_dir = 0, input_attack = 0; int64_t join_x = 0, join_y = 0, join_z = 0; int16_t input_yaw = 0;
			int32_t join_char_id = 0;
			char join_nickname[32] = "";
			if (unpack_c2s((const char*)msg.data, (int)msg.len, &cmd, &join_x, &join_y, &join_z, &input_dir, &input_attack, &input_yaw, &join_char_id, join_nickname, (int)sizeof(join_nickname))) {
				switch (cmd) {
				case CMD_LOADING:       handle_cmd_loading(ctx, msg.conv); break;
				case CMD_PLAYER_JOIN:   handle_cmd_player_join(ctx, msg.conv, join_x, join_y, join_z, join_char_id, join_nickname); break;
				case CMD_PLAYER_LEAVE:  handle_cmd_player_leave(ctx, msg.conv); break;
				case CMD_PLAYER_INPUT:  handle_cmd_player_input(ctx, msg.conv, 0, input_dir, input_attack, input_yaw); break;
				case CMD_PLAYER_HEART:  handle_cmd_heartbeat(ctx, msg.conv); break;
				}
			}
			else {
				log_info("Failed to parse message from conv=%d", msg.conv);
			}
		}
		if (msg.type == NET_TYPE_MESSAGE && msg.data) {
			netmessage_free_data(&msg);
		}
	}
}

static int get_target_frame_id(int curr_frameid, int context_frameid)
{
	// 帧同步要求客户端逐帧执行所有命令，不能跳帧（跳帧会导致客户端永久漏帧、状态分叉）。
	// 始终返回当前帧，由 input_forwarding 逐帧补发；客户端落后太多时改走快照重同步。
	(void)curr_frameid;
	return context_frameid;
}

static void collect_input(Context* ctx)
{
	int frame_id = ctx->ecs.ctx().get<GlobalState>().next_frame_id++;
	S2CPlayerJoin joins[32]; int num_joins = 0;
	for (auto& j : ctx->player_joins)
		if (num_joins < 32) joins[num_joins++] = j;
	S2CPlayerLeave leaves[32]; int num_leaves = 0;
	for (auto& l : ctx->player_leaves)
		if (num_leaves < 32) leaves[num_leaves++] = l;
	S2CPlayerInput inputs[64]; int num_inputs = 0;
	for (auto& inp : ctx->player_inputs)
		if (num_inputs < 64) inputs[num_inputs++] = inp;

	// 帧同步状态指纹（严格版）：与客户端共用同一公式（shared/systems/state_checksum.h）。
	// 纳入所有带 IdComponent + LogicPositionComponent 的实体（玩家 + 敌人 + 障碍物），
	// 敌人位置/数量不一致也能立刻暴露（例如中途加入者漏建/多建敌人、敌人 AI 推进错位）。
	std::vector<ChecksumEntry> entries;
	collect_checksum_entries(ctx->ecs, entries);
	char checksum[64] = { 0 };
	compute_full_md5(entries, checksum, sizeof(checksum));

	auto command_data = pack_s2c(S2C_CMD_COMMAND, frame_id, joins, num_joins, leaves, num_leaves, inputs, num_inputs, checksum);
	ctx->commands[frame_id] = command_data;
	ctx->command_queue.push(command_data);

	// 缓存本帧校验记录（退出时落盘 checksum_log_server.txt，与客户端对称）：
	// checksum 填服务端权威值；inputs 用本帧实际 inputs；entities 用本帧实体快照。
	{
		std::string input_finger;
		for (int k = 0; k < num_inputs; k++) {
			char b[64];
			snprintf(b, sizeof(b), "%d:%d:%d:%d;",
				inputs[k].conv, inputs[k].dir,
				(int)inputs[k].attack, (int)inputs[k].yaw_quantized);
			input_finger += b;
		}
		ChecksumRecord rec;
		rec.frame_id = frame_id;
		rec.inputs = input_finger;
		rec.entities = checksum_entries_string(entries);
		memcpy(rec.checksum, checksum, sizeof(rec.checksum));
		ctx->checksum_records.push_back(rec);
	}

	ctx->player_joins.clear();
	ctx->player_leaves.clear();
	ctx->player_inputs.clear();
}

static void input_forwarding(Context* ctx)
{
	auto view = ctx->ecs.view<ConnectionComponent>();
	for (auto e : view) {
		auto& comp = view.get<ConnectionComponent>(e);
		if (comp.is_loaded) {
			int taget_frameid = get_target_frame_id(comp.frameid,
				ctx->ecs.ctx().get<GlobalState>().next_frame_id);
			for (int i = comp.frameid; i < taget_frameid; i++) {
				auto it = ctx->commands.find(i);
				if (it != ctx->commands.end()) {
					netserver_send(ctx->net_server, comp.conv, it->second.c_str(), it->second.size());
				}
				else {
					log_error("Frame %d not found in commands, skip sending.", i);
				}
			}
			comp.frameid = taget_frameid;
		}
		else {
			auto it = ctx->world_snapshots.lower_bound(comp.frameid);
			if (it == ctx->world_snapshots.end() || it->first > comp.frameid) {
				if (it == ctx->world_snapshots.begin()) {
					log_info("WARNING: no snapshot cache found for frame %d (client %d), sending empty loading", comp.frameid, comp.conv);
					auto data = pack_s2c_loading(comp.conv, comp.frameid,
							ctx->ecs.ctx().get<GlobalState>().next_entity_id, nullptr, 0,
							(int32_t)ctx->ecs.ctx().get<GlobalState>().random_seed,
							(int32_t)ctx->ecs.ctx().get<GlobalState>().map_seed,
							(int32_t)ctx->ecs.ctx().get<GlobalState>().wave,
							current_wave_delay_ticks(ctx),
							(int32_t)ctx->ecs.ctx().get<GlobalState>().level);
					netserver_send(ctx->net_server, comp.conv, data.c_str(), data.size());
					return;
				}
				--it;
			}
			S2CEntity entities[512];
			int ec = 0, eid = 0;
			ec = unpack_world(it->second.c_str(), (int)it->second.size(), &eid, entities, 512);
			// 关键：loading 快照是帧 it->first 的，上报的 frame_id 必须也是 it->first，
			// 客户端才能用它精确初始化 AI 状态并从 it->first+1 开始演算。
			// 之前误用 comp.frameid 导致"快照帧 ≠ 上报帧"，AI 状态初始化错位。
			int snap_frame = it->first;
			auto data = pack_s2c_loading(comp.conv, snap_frame, eid, entities, ec,
					(int32_t)ctx->ecs.ctx().get<GlobalState>().random_seed,
					(int32_t)ctx->ecs.ctx().get<GlobalState>().map_seed,
					(int32_t)ctx->ecs.ctx().get<GlobalState>().wave,
					current_wave_delay_ticks(ctx),
					(int32_t)ctx->ecs.ctx().get<GlobalState>().level);
			netserver_send(ctx->net_server, comp.conv, data.c_str(), data.size());
			comp.frameid = snap_frame;   // 快照 key=K 内容是帧 K-1 执行后状态；客户端需从帧 K 开始消费命令
			comp.is_loaded = true;
		}
	}
}

// 广播波次换图（S2C_CMD_MAP_RELOAD）给所有已加载客户端
static void broadcast_map_reload(Context* ctx)
{
    if (ctx->map_reload_data.empty() || !ctx->net_server) return;
    auto view = ctx->ecs.view<ConnectionComponent>();
    for (auto e : view) {
        auto& comp = view.get<ConnectionComponent>(e);
        if (comp.is_loaded) {
            netserver_send(ctx->net_server, comp.conv, ctx->map_reload_data.c_str(), ctx->map_reload_data.size());
        }
    }
    ctx->map_reload_data.clear();
}

// ========== 服务端事件处理 ==========
static void server_on_keypressed(const char* key, int scancode, bool repeat, bool ctrl, bool shift, bool alt, void* arg)
{
	(void)arg;
	if (strcmp(key, "q") == 0 || strcmp(key, "Q") == 0) {
		push_quit_event();
	}
}

// ========== 接口实现 ==========
static void server_init(game_scene_p self)
{
	Context* ctx = self->ctx();
	log_info("server mode init");

	// 从设置（IP 下拉列表）取监听地址和端口
	ctx->net_server = netserver_create(NET_SERVER_WEBSOCKET, ctx->settings.server_ip, ctx->settings.server_port);

	// r3d 已在 main.cpp 统一创建，服务端复用（避免重复创建导致 "Window already claimed"）
	// 创建 mesh
	ctx->mesh_floor = r3d_mesh_plane(ctx->r3d, (float)PF_GRID_N, (float)PF_GRID_N, 1);
	ctx->mesh_floor_wire = r3d_mesh_plane_wire(ctx->r3d, (float)PF_GRID_N, (float)PF_GRID_N, (int)PF_GRID_N);
	ctx->mesh_player = r3d_mesh_cube(ctx->r3d, 0.6f, 1.2f, 0.6f);
	ctx->mesh_player_wire = r3d_mesh_cube_wire(ctx->r3d, 0.6f, 1.2f, 0.6f);
	ctx->mesh_enemy = r3d_mesh_cube(ctx->r3d, 0.5f, 1.0f, 0.5f);
	ctx->mesh_enemy_wire = r3d_mesh_cube_wire(ctx->r3d, 0.5f, 1.0f, 0.5f);
	ctx->mesh_prop = r3d_mesh_cube(ctx->r3d, 1.0f, 1.0f, 1.0f);
	ctx->mesh_prop_wire = r3d_mesh_cube_wire(ctx->r3d, 1.0f, 1.0f, 1.0f);
	ctx->mesh_sky = r3d_mesh_cube(ctx->r3d, 200.0f, 200.0f, 200.0f);

	// 注册服务端特有 system
	add_system(ctx, PostUpdate, "snapshot", snapshot_system);
}

static void server_handle_event(game_scene_p self, const void* ev)
{
	jevent_callbacks_t cbs = {};
	cbs.on_keypressed = server_on_keypressed;
	dispatch_event(cbs, ev, NULL);
}

static void server_fixed_update(game_scene_p self, float fixed_step)
{
	(void)fixed_step;
	Context* ctx = self->ctx();
	collect_input(ctx);
	input_forwarding(ctx);
	// 逻辑推进必须在 fixed_update（固定 20Hz）执行，与渲染帧率解耦：
	// 渲染掉帧时命令积压会产生"过期 checksum"，客户端按新状态重算后与服务端旧 checksum 比对 → MISMATCH。
	process_command_authority(ctx);
}

static void server_update(game_scene_p self, float dt)
{
	Context* ctx = self->ctx();

	netserver_update(ctx->net_server);
	network_receive_server(ctx);

	broadcast_map_reload(ctx);

	interpolate_position_pass(ctx, dt);
}

static void server_destroy(game_scene_p self)
{
	Context* ctx = self->ctx();
	// 障碍实体共享 ctx->mesh_prop/mesh_prop_wire，不能逐实体销毁（会重复释放共享 mesh 崩溃）；
	// 共享 mesh 统一在下方 ctx 级销毁。
	if (ctx->mesh_floor_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_floor_wire);
	if (ctx->mesh_floor) r3d_mesh_destroy(ctx->r3d, ctx->mesh_floor);
	if (ctx->mesh_sky) r3d_mesh_destroy(ctx->r3d, ctx->mesh_sky);
	if (ctx->mesh_player) r3d_mesh_destroy(ctx->r3d, ctx->mesh_player);
	if (ctx->mesh_player_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_player_wire);
	if (ctx->mesh_enemy) r3d_mesh_destroy(ctx->r3d, ctx->mesh_enemy);
	if (ctx->mesh_enemy_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_enemy_wire);
	if (ctx->mesh_prop) r3d_mesh_destroy(ctx->r3d, ctx->mesh_prop);
	if (ctx->mesh_prop_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_prop_wire);
	// r3d 为进程级资源，由 main.cpp 的 SDL_AppQuit 统一销毁，此处只销毁 mesh
	if (ctx->random) utils_random_destroy(ctx->random);
	netserver_destroy(ctx->net_server);
}

// ========== Host 模式 (MODE_HOST) ==========
// Host = 服务端 + 本地玩家(conv=1)：本地 WASD 输入直接注入，同时监听/转发客户端
static void host_on_keypressed(const char* key, int scancode, bool repeat, bool ctrl, bool shift, bool alt, void* arg)
{
	game_scene_p self = (game_scene_p)arg;
	Context* ctx = self->ctx();
	int key_mask = 0;
	if (strcmp(key, "w") == 0 || strcmp(key, "W") == 0) key_mask = INPUT_UP;
	else if (strcmp(key, "s") == 0 || strcmp(key, "S") == 0) key_mask = INPUT_DOWN;
	else if (strcmp(key, "a") == 0 || strcmp(key, "A") == 0) key_mask = INPUT_LEFT;
	else if (strcmp(key, "d") == 0 || strcmp(key, "D") == 0) key_mask = INPUT_RIGHT;
	else if (strcmp(key, "space") == 0) key_mask = INPUT_JUMP;
	else if (strcmp(key, "j") == 0 || strcmp(key, "J") == 0) key_mask = INPUT_ATTACK;
	else if (strcmp(key, "k") == 0 || strcmp(key, "K") == 0) key_mask = INPUT_BULLET;
	else if (strcmp(key, "l") == 0 || strcmp(key, "L") == 0) key_mask = INPUT_RAY;
	if (key_mask) {
		ctx->current_input_mask |= key_mask;
		if (key_mask & INPUT_DIR_MASK)
			ctx->local_dir = input_to_dir(key_mask);
	}
}

static void host_on_keyreleased(const char* key, int scancode, void* arg)
{
	game_scene_p self = (game_scene_p)arg;
	Context* ctx = self->ctx();
	int key_mask = 0;
	if (strcmp(key, "w") == 0 || strcmp(key, "W") == 0) key_mask = INPUT_UP;
	else if (strcmp(key, "s") == 0 || strcmp(key, "S") == 0) key_mask = INPUT_DOWN;
	else if (strcmp(key, "a") == 0 || strcmp(key, "A") == 0) key_mask = INPUT_LEFT;
	else if (strcmp(key, "d") == 0 || strcmp(key, "D") == 0) key_mask = INPUT_RIGHT;
	else if (strcmp(key, "space") == 0) key_mask = INPUT_JUMP;
	else if (strcmp(key, "j") == 0 || strcmp(key, "J") == 0) key_mask = INPUT_ATTACK;
	else if (strcmp(key, "k") == 0 || strcmp(key, "K") == 0) key_mask = INPUT_BULLET;
	else if (strcmp(key, "l") == 0 || strcmp(key, "L") == 0) key_mask = INPUT_RAY;
	if (key_mask) ctx->current_input_mask &= ~key_mask;
}

static void host_collect_local_input(Context* ctx)
{
	int dir = ctx->current_input_mask & INPUT_DIR_MASK;
	int attack = (ctx->current_input_mask & INPUT_ATTACK) ? 1 : 0;
	bool k_down = (ctx->current_input_mask & INPUT_BULLET) != 0;
	if (k_down && !ctx->bullet_prev) attack = 2;   // K 键上升沿：点按单发子弹
	ctx->bullet_prev = k_down;
	bool l_down = (ctx->current_input_mask & INPUT_RAY) != 0;
	if (l_down && !ctx->ray_prev) attack = 3;   // L 键上升沿：点按单发射线
	ctx->ray_prev = l_down;
	if (ctx->bullet_triggered) { attack = 2; ctx->bullet_triggered = false; } // 屏幕"射"按钮：点按单发子弹
	if (ctx->attack_triggered) { if (attack != 2 && attack != 3) attack = 1; ctx->attack_triggered = false; }
	// 冷却禁用：近战/射击/射线未到时间时不生成对应攻击指令
	if (local_attack_cooldown_ratio(ctx, false) > 0.0f && attack == 1) attack = 0;
	if (local_attack_cooldown_ratio(ctx, true)  > 0.0f && attack == 2) attack = 0;
	if (local_ray_cooldown_ratio(ctx)           > 0.0f && attack == 3) attack = 0;
	// Q32 定点弧度 → int16 量化：*32767 / PI（纯定点，替代浮点乘除）
	int16_t yaw = (int16_t)((fp_add(fp_div(fp_mul(ctx->local_yaw, fp_from_int(32767)), fp_pi()),
		fp_from_float(0.5f))) >> 32);
	if (dir || attack) {
		S2CPlayerInput i = { 0, (int32_t)ctx->local_conv, dir, (int8_t)attack, yaw };
		ctx->player_inputs.push_back(i);
	}
}

static void host_init(game_scene_p self)
{
	Context* ctx = self->ctx();
	log_info("host mode init");

	// 网络服务端（监听客户端连接）
	ctx->net_server = netserver_create(NET_SERVER_WEBSOCKET, ctx->settings.server_ip, ctx->settings.server_port);

	// 创建 mesh
	ctx->mesh_floor = r3d_mesh_plane(ctx->r3d, (float)PF_GRID_N, (float)PF_GRID_N, 1);
	ctx->mesh_floor_wire = r3d_mesh_plane_wire(ctx->r3d, (float)PF_GRID_N, (float)PF_GRID_N, (int)PF_GRID_N);
	ctx->mesh_player = r3d_mesh_cube(ctx->r3d, 0.6f, 1.2f, 0.6f);
	ctx->mesh_player_wire = r3d_mesh_cube_wire(ctx->r3d, 0.6f, 1.2f, 0.6f);
	ctx->mesh_enemy = r3d_mesh_cube(ctx->r3d, 0.5f, 1.0f, 0.5f);
	ctx->mesh_enemy_wire = r3d_mesh_cube_wire(ctx->r3d, 0.5f, 1.0f, 0.5f);
	ctx->mesh_prop = r3d_mesh_cube(ctx->r3d, 1.0f, 1.0f, 1.0f);
	ctx->mesh_prop_wire = r3d_mesh_cube_wire(ctx->r3d, 1.0f, 1.0f, 1.0f);
	ctx->mesh_sky = r3d_mesh_cube(ctx->r3d, 200.0f, 200.0f, 200.0f);

	// 服务端权威系统
	add_system(ctx, PostUpdate, "snapshot", snapshot_system);

	// 随机种子（host 非 is_server，需自行创建）
	auto& gs = ctx->ecs.ctx().get<GlobalState>();
	gs.random_seed = (int)time(nullptr);
	ctx->random = utils_random_create((uint32_t)gs.random_seed);

	// 注入本地玩家（conv=1，netserver 客户端 conv 从 1000 起，不冲突）
	ctx->local_conv = 1;
	fp_t sx = fp_zero(), sz = fp_zero();
	level_player_spawn(level_index_for_wave(gs), &sx, &sz);
	S2CPlayerJoin j = make_join((int32_t)ctx->local_conv, sx, fp_zero(), sz,
		(int32_t)ctx->selected_char_id, ctx->selected_nickname);
	ctx->player_joins.push_back(j);

	// 音频
	ctx->audio_dev = audio_open_device();
	ctx->step_sound = audio_shot_create("sounds/grass-step.wav", ctx->audio_dev);
	audio_shot_set_gain(ctx->step_sound, ctx->settings.sfx_volume);
	ctx->bgm = audio_bgm_create("sounds/bgm.wav", ctx->audio_dev);
	audio_bgm_set_gain(ctx->bgm, ctx->settings.bgm_volume);
}

static void host_handle_event(game_scene_p self, const void* ev)
{
	jevent_callbacks_t cbs = {};
	cbs.on_keypressed = host_on_keypressed;
	cbs.on_keyreleased = host_on_keyreleased;
	dispatch_event(cbs, ev, self);
}

static void host_fixed_update(game_scene_p self, float fixed_step)
{
	(void)fixed_step;
	Context* ctx = self->ctx();
	host_collect_local_input(ctx);
	collect_input(ctx);
	input_forwarding(ctx);
	// 逻辑推进必须在 fixed_update（固定 20Hz）执行，与渲染帧率解耦（同 server_fixed_update）
	process_command_authority(ctx);
}

static void host_update(game_scene_p self, float dt)
{
	Context* ctx = self->ctx();

	netserver_update(ctx->net_server);
	network_receive_server(ctx);

	broadcast_map_reload(ctx);

	interpolate_position_pass(ctx, dt);
}

static void host_destroy(game_scene_p self)
{
	Context* ctx = self->ctx();
	audio_shot_destroy(ctx->step_sound);
	audio_bgm_destroy(ctx->bgm);
	if (ctx->audio_dev) audio_close_device(ctx->audio_dev);
	// 障碍实体共享 ctx->mesh_prop/mesh_prop_wire，不能逐实体销毁（会重复释放共享 mesh 崩溃）；
	// 共享 mesh 统一在下方 ctx 级销毁。
	if (ctx->mesh_floor_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_floor_wire);
	if (ctx->mesh_floor) r3d_mesh_destroy(ctx->r3d, ctx->mesh_floor);
	if (ctx->mesh_sky) r3d_mesh_destroy(ctx->r3d, ctx->mesh_sky);
	if (ctx->mesh_player) r3d_mesh_destroy(ctx->r3d, ctx->mesh_player);
	if (ctx->mesh_player_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_player_wire);
	if (ctx->mesh_enemy) r3d_mesh_destroy(ctx->r3d, ctx->mesh_enemy);
	if (ctx->mesh_enemy_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_enemy_wire);
	if (ctx->mesh_prop) r3d_mesh_destroy(ctx->r3d, ctx->mesh_prop);
	if (ctx->mesh_prop_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_prop_wire);
	if (ctx->random) utils_random_destroy(ctx->random);
	netserver_destroy(ctx->net_server);
}

GameModeInterface g_host_mode = {
	host_init,
	host_handle_event,
	host_fixed_update,
	host_update,
	host_destroy
};

GameModeInterface g_server_mode = {
	server_init,
	server_handle_event,
	server_fixed_update,
	server_update,
	server_destroy
};