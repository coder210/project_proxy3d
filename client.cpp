#include <cstring>
#include <cmath>
#include <ctime>
#include <joy/joy.h>
#include "context.h"
#include "scenes/game_scene.h"
#include "game_mode.h"
#include "systems/spawn_obstacles_system.h"
#include "systems/input_consume_join_system.h"
#include "systems/input_consume_leave_system.h"
#include "systems/input_consume_input_system.h"
#include "systems/movement_system.h"
#include "systems/cleanup_system.h"
#include "systems/collision_system.h"
#include "systems/ai_system.h"
#include "passes/interpolate_position_pass.h"
#include "systems/snapshot_system.h"
#include "components/global_state_component.h"
#include "components/ai_component.h"
#include "components/pathfinding_component.h"
#include "components/mesh_component.h"
#include "components/wave_advance_component.h"
#include "systems/spawn_def.h"
#include "systems/wave_system.h"
#include "shared/systems/character_def.h"

// ========== 客户端网络 ==========
static void handle_loading(Context* ctx, int conv, int frame_id, int eid, const S2CEntity* entities, int ec, int map_seed, int wave, [[maybe_unused]] int wave_delay_ticks, int level)
{
	log_info("handle_loading");
	ctx->ready = true;
	ctx->local_conv = conv;
	auto& gs = ctx->ecs.ctx().get<GlobalState>();
	gs.camera_follow_conv = conv;
	ctx->server_frameid = frame_id;
	ctx->server_entity_id = eid;
	// 当前固定地图关卡：房主/权威端选定，随加入统一下发，保证双端用同一关卡生成
	gs.level = level;
	// 地图 seed：换波时客户端用确定性地图布局采样敌人出生点（需与服务端一致）
	gs.map_seed = map_seed;
	// 当前波次：换波空窗期加入时据此创建 WaveAdvanceComponent 生成敌人（帧同步一致）
	gs.wave = wave;
	// 实体 ID 计数器与服务端对齐：之后任一端的 get_entity_id() 产出相同 ID（帧同步确定性）
	gs.next_entity_id = ctx->server_entity_id;

	for (int i = 0; i < ec; i++) {
		auto& entity = entities[i];

		auto e = ctx->ecs.create();
		ctx->ecs.emplace<IdComponent>(e, entity.id);
		ctx->ecs.emplace<LogicRectComponent>(e, entity.width, entity.height);
		ctx->ecs.emplace<LogicPositionComponent>(e, entity.position_x, entity.position_y, entity.position_z);

		if (entity.type == S2C_TYPE_PROP) {
			// 静态方盒障碍：服务端生成并经 loading/MAP_RELOAD 同步，客户端据此重建。
			// 碰撞体尺寸一律用定点数还原，杜绝 float 相乘，与 spawn_obstacles_system 逐位一致：
			//   半宽 = width/2、半深 = height/2、半高 = position_y（服务端已把中心 Y 设为全高一半）。
			fp_t half_sx = fp_div(entity.width,  fp_from_int(2));  // 全宽 → 半宽
			fp_t half_sz = fp_div(entity.height, fp_from_int(2));  // 全深 → 半深
			fp_t half_sy = entity.position_y;                      // 中心 Y = 全高一半

			ColliderComponent col;
			col.type = ColliderComponent::BOX;
			col.half_size = { half_sx, half_sy, half_sz };         // 半宽/半深/半高，与服务端逐位一致
			ctx->ecs.emplace<ColliderComponent>(e, col);

			// Transform 属渲染数据，允许用定点转 float 填充
			vec3_t pos   = { fp_to_float(entity.position_x), fp_to_float(entity.position_y), fp_to_float(entity.position_z) };
			vec3_t scale = { fp_to_float(entity.width), fp_to_float(half_sy) * 2.0f, fp_to_float(entity.height) };
			ctx->ecs.emplace<TransformComponent>(e, pos, 0.0f, scale);
			StaticMeshComponent smc;
			smc.mesh = ctx->mesh_prop;
			smc.wire_mesh = ctx->mesh_prop_wire;
			smc.color = { 45, 45, 50, 255 };
			smc.stroke_color = { 240, 240, 244, 255 };
			ctx->ecs.emplace<StaticMeshComponent>(e, smc);
			log_info("[obs] id=%d x=%.1f z=%.1f sx=%.1f sz=%.1f", entity.id,
					fp_to_float(entity.position_x), fp_to_float(entity.position_z),
					fp_to_float(entity.width), fp_to_float(entity.height));
			continue;
		}

		ctx->ecs.emplace<TransformComponent>(e,
			vec3_t{ fp_to_float(entity.position_x), fp_to_float(entity.position_y), fp_to_float(entity.position_z) },
			(float)entity.yaw_quantized / 32767.0f * 3.14159265f,
			vec3_t{ 1.0f, 1.0f, 1.0f });
		if (entity.type == S2C_TYPE_PLAYER) {
			ctx->ecs.emplace<LogicVelocityComponent>(e, fp_from_float(0), fp_from_float(0), fp_from_float(0));
			PlayerComponent pc;
			pc.conv = entity.player_conv;
			pc.char_id = entity.char_id;
			snprintf(pc.nickname, sizeof(pc.nickname), "%s", entity.nickname);
			ctx->ecs.emplace<PlayerComponent>(e, pc);
			ctx->ecs.emplace<ActionStateComponent>(e);
			// 血量与出生点（来自服务端 loading 快照，帧同步确定性；上限按角色定义表）
			const CharacterDef& cd = character_def(entity.char_id);
			ctx->ecs.emplace<HealthComponent>(e, (int)entity.hp, cd.max_hp);
			ctx->ecs.emplace<SpawnPointComponent>(e, entity.position_x, entity.position_y, entity.position_z);
			MeshComponent mc;
			mc.mesh = ctx->mesh_player;
			mc.wire_mesh = ctx->mesh_player_wire;
			mc.color = { cd.fill_r, cd.fill_g, cd.fill_b, 255 };        // 填充按角色色
			mc.stroke_color = { cd.stroke_r, cd.stroke_g, cd.stroke_b, 255 };    // 描边按角色色
			ctx->ecs.emplace<MeshComponent>(e, mc);
		}
		else if (entity.type == S2C_TYPE_ENEMY) {
			ctx->ecs.emplace<EnemyComponent>(e);
			ctx->ecs.emplace<ActionStateComponent>(e);
			ctx->ecs.emplace<HealthComponent>(e, (int)entity.hp, ENEMY_MAX_HP);
			ColliderComponent ecol;
			ecol.type = ColliderComponent::BOX;
			ecol.half_size = { fp_from_float(0.35f), fp_from_float(0.9f), fp_from_float(0.35f) };
			ctx->ecs.emplace<ColliderComponent>(e, ecol);
			MeshComponent mc;
			mc.mesh = ctx->mesh_enemy;
			mc.wire_mesh = ctx->mesh_enemy_wire;
			mc.color = { 22, 22, 26, 255 };           // 深填充
			mc.stroke_color = { 255, 255, 255, 255 }; // 白描边
			ctx->ecs.emplace<MeshComponent>(e, mc);
			int si = (int)entity.ai_type;
				if (si >= 0 && si < ENEMY_SPAWN_COUNT) {
					auto& sd = ENEMY_SPAWNS[si];
					// AI home = 实际出生位置（快照），与权威端波次生成保持一致
					ctx->ecs.emplace<AIComponent>(e,
					entity.position_x, entity.position_z,
					fp_from_float(sd.patrol_radius),
					fp_from_float(sd.detect_radius),
					fp_from_float(sd.attack_radius),
					fp_from_float(sd.guard_radius));
					// 恢复服务端下发的 AI 运行时状态（timer/state/target），
					// 否则这些持续累积的状态从 0/出生点开始，与服务端接入时值不同步
					auto& ai = ctx->ecs.get<AIComponent>(e);
					ai.timer    = entity.ai_timer;
					ai.state    = entity.ai_state;
					ai.target_x = (fp_t)entity.ai_target_x;
					ai.target_z = (fp_t)entity.ai_target_z;
					// 恢复 PathComponent 状态：若不恢复，set_target 新建 PathComponent 时
					// target_gx=0 → 与实际目标格不等 → dirty=true → 不必要的路径重算 → 双端分叉
					PathComponent pc;
					pc.target_gx = entity.pf_target_gx;
					pc.target_gz = entity.pf_target_gz;
					pc.path_len  = 0;  // path[] 数组未同步，强制重算前不能沿旧路径走
					pc.next_idx  = 0;
					pc.recompute_cooldown = entity.pf_recompute_cd;
					// 服务端有活跃路径时强制重算（A* 确定性，双端结果一致）；
					// 服务端路径已耗尽（path_len==0）时不重算，避免误触发移动
					pc.dirty = (entity.pf_path_len > 0);
					ctx->ecs.emplace<PathComponent>(e, pc);
					ctx->ecs.emplace<SpawnPointComponent>(e, entity.position_x, fp_from_float(0), entity.position_z);
					log_info("[DIAG] enemy id=%d ai_type=%d state=%d timer=%d tgt=(%.2f,%.2f) pos=(%.2f,%.2f)",
						entity.id, si, ai.state, ai.timer,
						fp_to_float(ai.target_x), fp_to_float(ai.target_z),
						fp_to_float(entity.position_x), fp_to_float(entity.position_z));
				} else {
					log_info("[DIAG] enemy id=%d ai_type=%d OUT OF RANGE, NO AIComponent!", entity.id, si);
				}
		}
	}

	// 新模型为"单关单波"：关卡起始敌人来自 loading 快照；换关经 MAP_RELOAD 确定性重建，
	// 此处无需创建 WaveAdvanceComponent（无波次延迟空窗）。
	std::map<int, state_snapshot> snapshots;
	auto pv = ctx->ecs.view<PlayerComponent, IdComponent, LogicRectComponent, LogicPositionComponent>();
	for (auto e : pv) {
		auto& id = pv.get<IdComponent>(e);
		auto& pos = pv.get<LogicPositionComponent>(e);
		snapshots.insert({ id.id, { pos.x, pos.y, pos.z } });
	}
	ctx->snapshots.emplace(ctx->server_frameid, std::move(snapshots));

	std::string data = pack_c2s(CMD_PLAYER_JOIN, fp_from_float(3.2f), fp_from_float(0.0f), fp_from_float(2.3f), 0, 0, 0, (int32_t)ctx->selected_char_id, ctx->selected_nickname);
	netclient_send(ctx->netclient, data.c_str(), (int)data.size());
}

// 换关（客户端）：收到换关信号后在对应逻辑帧确定性重建障碍+敌人+重定位玩家。
// 客户端与服务端权威跑同一 apply_level_change（输入仅新关卡号）→ 双端逐位一致，维持帧同步。
static void handle_map_reload(Context* ctx, int level)
{
	log_info("handle_map_reload level=%d", level);
	apply_level_change(ctx, ctx->ecs, level);
}

static void send_input(Context* ctx)
{
	if (!ctx->netclient) return;
	int dir = ctx->current_input_mask & (INPUT_DIR_MASK | INPUT_JUMP);
	int attack = (ctx->current_input_mask & INPUT_ATTACK) ? 1 : 0;
	bool k_down = (ctx->current_input_mask & INPUT_BULLET) != 0;
	if (k_down && !ctx->bullet_prev) attack = 2;   // K 键上升沿：点按单发子弹
	ctx->bullet_prev = k_down;
	bool l_down = (ctx->current_input_mask & INPUT_RAY) != 0;
	if (l_down && !ctx->ray_prev) attack = 3;   // L 键上升沿：点按单发射线
	ctx->ray_prev = l_down;
	if (ctx->bullet_triggered) { attack = 2; ctx->bullet_triggered = false; } // 屏幕"射"按钮：点按单发子弹
	if (ctx->ray_triggered)    { attack = 3; ctx->ray_triggered = false; }    // 屏幕"线"按钮：点按单发射线
	if (ctx->attack_triggered) { if (attack != 2 && attack != 3) attack = 1; ctx->attack_triggered = false; }
	// 冷却禁用：近战/射击/射线未到时间时不发送对应攻击指令
	if (local_attack_cooldown_ratio(ctx, false) > 0.0f && attack == 1) attack = 0;
	if (local_attack_cooldown_ratio(ctx, true)  > 0.0f && attack == 2) attack = 0;
	if (local_ray_cooldown_ratio(ctx)           > 0.0f && attack == 3) attack = 0;
	// Q32 定点弧度 → int16 量化：*32767 / PI（纯定点，替代浮点乘除）
	int16_t yaw = (int16_t)((fp_add(fp_div(fp_mul(ctx->local_yaw, fp_from_int(32767)), fp_pi()),
		fp_from_float(0.5f))) >> 32);
	auto data = pack_c2s(CMD_PLAYER_INPUT, 0, 0, 0, dir, attack, yaw);
	netclient_send(ctx->netclient, data.c_str(), (int)data.size());
}

static void network_receive_client(Context* ctx)
{
	net_message_t msg;
	while (netclient_poll_message(ctx->netclient, &msg)) {
		if (msg.type == NET_TYPE_CONNECTED) {
			auto d = pack_c2s(CMD_LOADING, 0, 0, 0, 0, 0, 0);
			netclient_send(ctx->netclient, d.c_str(), (int)d.size());
		}
		else if (msg.type == NET_TYPE_DISCONNECTED) {
			log_info("disconnected=%d", msg.conv);
		}
		else if (msg.type == NET_TYPE_MESSAGE) {
			jmsgpack_unpacked_t r;
			jmsgpack_unpacked_init(&r);
			size_t off = 0;
			int cmd = S2C_CMD_NONE;
			if (jmsgpack_unpack_next(&r, (const char*)msg.data, (int)msg.len, &off) >= 0) {
				const jmsgpack_object_t* root = jmsgpack_unpacked_data(&r);
				if (root && jmsgpack_object_type(root) == JMSGPACK_OBJECT_ARRAY
						&& jmsgpack_object_array_size(root) >= 2) {
					const jmsgpack_object_t* first = jmsgpack_object_array_at(root, 0);
					if (first && jmsgpack_object_type(first) == JMSGPACK_OBJECT_POSITIVE_INTEGER)
						cmd = (int)jmsgpack_object_u64(first);
				}
			}
			jmsgpack_unpacked_destroy(&r);

			if (cmd == S2C_CMD_LOADING) {
				int conv, frame_id, eid, seed, map_seed = 0, wave = 0, wave_delay = -1, level = 1;
				S2CEntity entities[512];
				int ec = unpack_loading((const char*)msg.data, (int)msg.len, &conv, &frame_id, &eid, &seed, &map_seed, &wave, &wave_delay, &level, entities, 512);
				if (ec >= 0) {
					if (ctx->random) utils_random_destroy(ctx->random);
					ctx->random = utils_random_create((uint32_t)seed);
					handle_loading(ctx, conv, frame_id, eid, entities, ec, map_seed, wave, wave_delay, level);
				}
			}
			else if (cmd == S2C_CMD_MAP_RELOAD) {
					int reload_frame = 0, level = 0;
					int ok = unpack_map_reload((const char*)msg.data, (int)msg.len, &reload_frame, &level);
					if (ok) {
						// 不立即重建：换关必须与逻辑帧同步。先按 frame_id 缓存，
						// 待本地逻辑帧推进到 reload_frame 时（client_update 里）才应用，
						// 在相同时刻确定性重建障碍与敌人，维持帧同步一致性。
						Context::PendingMapReload pr;
						pr.level = level;
						ctx->map_reload_pending[reload_frame] = std::move(pr);
						log_info("map_reload queued frame=%d level=%d", reload_frame, level);
					}
				}
			else {
				ctx->command_queue.push(std::string(msg.data, msg.len));
			}
		}
		if (msg.type == NET_TYPE_MESSAGE && msg.data)
			netmessage_free_data(&msg);
	}
}

// ========== 客户端事件处理 ==========
static void client_on_keypressed(const char* key, int scancode, bool repeat, bool ctrl, bool shift, bool alt, void* arg)
{
	game_scene_p self = (game_scene_p)arg;
	int key_mask = 0;
	if (strcmp(key, "w") == 0) key_mask = INPUT_UP;
	else if (strcmp(key, "W") == 0) key_mask = INPUT_UP;
	else if (strcmp(key, "s") == 0) key_mask = INPUT_DOWN;
	else if (strcmp(key, "S") == 0) key_mask = INPUT_DOWN;
	else if (strcmp(key, "a") == 0) key_mask = INPUT_LEFT;
	else if (strcmp(key, "A") == 0) key_mask = INPUT_LEFT;
	else if (strcmp(key, "d") == 0) key_mask = INPUT_RIGHT;
	else if (strcmp(key, "D") == 0) key_mask = INPUT_RIGHT;
	else if (strcmp(key, "space") == 0) key_mask = INPUT_JUMP;
	else if (strcmp(key, "j") == 0 || strcmp(key, "J") == 0) key_mask = INPUT_ATTACK;
	else if (strcmp(key, "k") == 0 || strcmp(key, "K") == 0) key_mask = INPUT_BULLET;
	else if (strcmp(key, "l") == 0 || strcmp(key, "L") == 0) key_mask = INPUT_RAY;
	if (key_mask) {
		self->ctx()->current_input_mask |= key_mask;
		if (key_mask & INPUT_DIR_MASK)
			self->ctx()->local_dir = input_to_dir(key_mask);
	}
}

static void client_on_keyreleased(const char* key, int scancode, void* arg)
{
	game_scene_p self = (game_scene_p)arg;
	int key_mask = 0;
	if (strcmp(key, "w") == 0) key_mask = INPUT_UP;
	else if (strcmp(key, "W") == 0) key_mask = INPUT_UP;
	else if (strcmp(key, "s") == 0) key_mask = INPUT_DOWN;
	else if (strcmp(key, "S") == 0) key_mask = INPUT_DOWN;
	else if (strcmp(key, "a") == 0) key_mask = INPUT_LEFT;
	else if (strcmp(key, "A") == 0) key_mask = INPUT_LEFT;
	else if (strcmp(key, "d") == 0) key_mask = INPUT_RIGHT;
	else if (strcmp(key, "D") == 0) key_mask = INPUT_RIGHT;
	else if (strcmp(key, "space") == 0) key_mask = INPUT_JUMP;
	else if (strcmp(key, "j") == 0 || strcmp(key, "J") == 0) key_mask = INPUT_ATTACK;
	else if (strcmp(key, "k") == 0 || strcmp(key, "K") == 0) key_mask = INPUT_BULLET;
	else if (strcmp(key, "l") == 0 || strcmp(key, "L") == 0) key_mask = INPUT_RAY;
	if (key_mask) self->ctx()->current_input_mask &= ~key_mask;
}



static void client_on_mousemoved(float x, float y, float /*dx*/, float /*dy*/, void* arg)
{
	game_scene_p self = (game_scene_p)arg;
	float cx = 240.0f;
	float cy = 160.0f;
	// 俯视相机朝 +Z 且无 roll：屏幕上右=世界 -X、屏幕上=世界 +Z，所以世界 X 与屏幕左右是镜像的。
	// 朝向 dir=(sin yaw, cos yaw)=世界(x,z)，须取 -X 镜像：world_x=-(x-cx)、world_z=-(y-cy)。
	// 全定点：su=屏幕右正、sm=屏幕下正，则 yaw=atan2(-su, -sm)。
	int su = (int)x - (int)cx;
	int sm = (int)y - (int)cy;
	fp_t yaw_fp = fp_atan2(fp_from_int(-su), fp_from_int(-sm));
	self->ctx()->local_yaw = yaw_fp;   // 全程定点，字段即 Q32 定点弧度
}

// ========== 接口实现 ==========
static void client_init(game_scene_p self)
{
	Context* ctx = self->ctx();
	log_info("client mode init");

	// 从设置（IP 下拉列表）取连接地址和端口
	ctx->netclient = netclient_create(NET_CLIENT_WEBSOCKET, ctx->settings.server_ip, ctx->settings.server_port);

	// 创建 mesh（"填充+描边"画风：solid 填充 + wire 描边）
	ctx->mesh_player = r3d_mesh_cube(ctx->r3d, 0.6f, 1.2f, 0.6f);
	ctx->mesh_player_wire = r3d_mesh_cube_wire(ctx->r3d, 0.6f, 1.2f, 0.6f);
	ctx->mesh_enemy = r3d_mesh_cube(ctx->r3d, 0.5f, 1.0f, 0.5f);
	ctx->mesh_enemy_wire = r3d_mesh_cube_wire(ctx->r3d, 0.5f, 1.0f, 0.5f);
	ctx->mesh_prop = r3d_mesh_cube(ctx->r3d, 1.0f, 1.0f, 1.0f);
	ctx->mesh_prop_wire = r3d_mesh_cube_wire(ctx->r3d, 1.0f, 1.0f, 1.0f);
	ctx->mesh_floor = r3d_mesh_plane(ctx->r3d, (float)PF_GRID_N, (float)PF_GRID_N, 1);
	ctx->mesh_floor_wire = r3d_mesh_plane_wire(ctx->r3d, (float)PF_GRID_N, (float)PF_GRID_N, (int)PF_GRID_N);
	ctx->mesh_sky = r3d_mesh_cube(ctx->r3d, 200.0f, 200.0f, 200.0f);

	// 音频
	ctx->audio_dev = audio_open_device();
	ctx->step_sound = audio_shot_create("project_proxy3d/sounds/grass-step.wav", ctx->audio_dev);
	audio_shot_set_gain(ctx->step_sound, 1.0f);
	ctx->bgm = audio_bgm_create("project_proxy3d/sounds/bgm.wav", ctx->audio_dev);
	audio_bgm_set_gain(ctx->bgm, 0.5f);
	// 战斗音效（缺失文件时返回 NULL，播放时跳过）
	ctx->sfx_melee  = audio_shot_create("project_proxy3d/sounds/melee.wav", ctx->audio_dev);
	ctx->sfx_bullet = audio_shot_create("project_proxy3d/sounds/bullet.wav", ctx->audio_dev);
	ctx->sfx_hit    = audio_shot_create("project_proxy3d/sounds/hit.wav", ctx->audio_dev);
	ctx->sfx_kill   = audio_shot_create("project_proxy3d/sounds/kill.wav", ctx->audio_dev);
	ctx->sfx_wave   = audio_shot_create("project_proxy3d/sounds/wave.wav", ctx->audio_dev);
	audio_shot_set_gain(ctx->sfx_melee, ctx->settings.sfx_volume);
	audio_shot_set_gain(ctx->sfx_bullet, ctx->settings.sfx_volume);
	audio_shot_set_gain(ctx->sfx_hit, ctx->settings.sfx_volume);
	audio_shot_set_gain(ctx->sfx_kill, ctx->settings.sfx_volume);
	audio_shot_set_gain(ctx->sfx_wave, ctx->settings.sfx_volume);
}

static void client_handle_event(game_scene_p self, const void* ev)
{
	jevent_callbacks_t cbs = {};
	cbs.on_keypressed = client_on_keypressed;
	cbs.on_keyreleased = client_on_keyreleased;
	cbs.on_mousemoved = client_on_mousemoved;
	dispatch_event(cbs, ev, self);
}

static void client_fixed_update(game_scene_p self, float fixed_step)
{
	(void)fixed_step;
	Context* ctx = self->ctx();
	send_input(ctx);

	// 逻辑推进必须在 fixed_update（固定 20Hz）执行，与渲染帧率解耦：
	// 渲染掉帧时若按渲染帧率消费命令，客户端会落后于服务端（服务端已固定 20Hz 推进），
	// 逐帧比对 checksum 时因状态滞后产生 MISMATCH。
	if (!ctx->command_queue.empty()) {
		std::string command_data = ctx->command_queue.front();
		ctx->command_queue.pop();

		int frame_id, jn = 0, ln = 0, in = 0;
		S2CPlayerJoin joins[32];
		S2CPlayerLeave leaves[32];
		S2CPlayerInput inputs[64];
		char server_checksum[64] = { 0 };
		bool ok = unpack_command(command_data.c_str(), (int)command_data.size(),
			&frame_id, joins, 32, &jn, leaves, 32, &ln, inputs, 64, &in,
			server_checksum, sizeof(server_checksum));
		if (ok) {
			auto& cmd = ctx->ecs.ctx().get<NetComponent>();
			cmd.frame_id = ok ? frame_id : -1;
			cmd.num_joins = jn;  memcpy(cmd.joins, joins, jn * sizeof(S2CPlayerJoin));
			cmd.num_leaves = ln; memcpy(cmd.leaves, leaves, ln * sizeof(S2CPlayerLeave));
			cmd.num_inputs = in; memcpy(cmd.inputs, inputs, in * sizeof(S2CPlayerInput));

			// 帧同步校验：与服务端 collect_input 同一指纹公式、同一时机（模拟本帧之前）。
			// 不一致即时告警，逐帧记录供退出后 diff（结构见 state_checksum.h）。
			std::vector<ChecksumEntry> entries;
			collect_checksum_entries(ctx->ecs, entries);
			char local_checksum[64] = { 0 };
			compute_full_md5(entries, local_checksum, sizeof(local_checksum));

			std::string input_finger;
			for (int k = 0; k < in; k++) {
				char b[64];
				snprintf(b, sizeof(b), "%d:%d:%d:%d;",
					inputs[k].conv, inputs[k].dir,
					(int)inputs[k].attack, (int)inputs[k].yaw_quantized);
				input_finger += b;
			}

			int mismatched = (strcmp(local_checksum, server_checksum) != 0) ? 1 : 0;
			ctx->checksum_records.push_back(
				{ frame_id, "", input_finger, checksum_entries_string(entries) });
			auto& rec = ctx->checksum_records.back();
			memcpy(rec.checksum, local_checksum, sizeof(rec.checksum));
			if (mismatched) {
				// 只提示控制台，不把服务端 checksum 落盘；分叉细节退出后 diff 两端对称日志
				log_info("[MISMATCH] frame=%d", frame_id);
			}
		}

		// 换关按帧对齐应用：到达权威端换关的那个逻辑帧号时才确定性重建障碍与敌人，
			// 使换关经由 command 帧管线驱动，而非收到消息的渲染帧驱动。
			auto rit = ctx->map_reload_pending.find(ctx->ecs.ctx().get<NetComponent>().frame_id);
			if (rit != ctx->map_reload_pending.end()) {
				handle_map_reload(ctx, rit->second.level);
				ctx->map_reload_pending.erase(rit);
			}

		run_phase(ctx, PreUpdate, ctx->FIXED_TIMESTEP);
		run_phase(ctx, OnUpdate, ctx->FIXED_TIMESTEP);
		run_phase(ctx, PostUpdate, ctx->FIXED_TIMESTEP);
	}
}

static void client_update(game_scene_p self, float dt)
{
	Context* ctx = self->ctx();
	if (ctx->netclient) {
		netclient_update(ctx->netclient);
		network_receive_client(ctx);
	}

	interpolate_position_pass(ctx, dt);
}

static void client_destroy(game_scene_p self)
{
	Context* ctx = self->ctx();
	if (ctx->netclient) {
		netclient_destroy(ctx->netclient);
		ctx->netclient = NULL;
	}
	audio_shot_destroy(ctx->step_sound);
	audio_bgm_destroy(ctx->bgm);
	audio_shot_destroy(ctx->sfx_melee);
	audio_shot_destroy(ctx->sfx_bullet);
	audio_shot_destroy(ctx->sfx_hit);
	audio_shot_destroy(ctx->sfx_kill);
	audio_shot_destroy(ctx->sfx_wave);
	if (ctx->audio_dev) audio_close_device(ctx->audio_dev);
	if (ctx->mesh_floor_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_floor_wire);
	if (ctx->mesh_floor) r3d_mesh_destroy(ctx->r3d, ctx->mesh_floor);
	if (ctx->mesh_sky) r3d_mesh_destroy(ctx->r3d, ctx->mesh_sky);
	if (ctx->mesh_enemy) r3d_mesh_destroy(ctx->r3d, ctx->mesh_enemy);
	if (ctx->mesh_enemy_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_enemy_wire);
	if (ctx->mesh_player) r3d_mesh_destroy(ctx->r3d, ctx->mesh_player);
	if (ctx->mesh_player_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_player_wire);
	if (ctx->mesh_prop) r3d_mesh_destroy(ctx->r3d, ctx->mesh_prop);
	if (ctx->mesh_prop_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_prop_wire);
	if (ctx->random) utils_random_destroy(ctx->random);
}

// ========== 单机模式 (MODE_SINGLE) ==========
// 本地推逻辑（同服务端权威），无网络；本地玩家 conv=1 直接注入 join/input
static void single_inject_local_join(Context* ctx)
{
	ctx->local_conv = 1;
	// 固定关卡：本地玩家出生点 = 当前关卡 P 标记（确定性，两端一致）
	auto& gs = ctx->ecs.ctx().get<GlobalState>();
	fp_t sx = fp_zero(), sz = fp_zero();
	level_player_spawn(level_index_for_wave(gs), &sx, &sz);
	S2CPlayerJoin j = make_join((int32_t)ctx->local_conv, sx, fp_zero(), sz,
		(int32_t)ctx->selected_char_id, ctx->selected_nickname);
	ctx->player_joins.push_back(j);
}

static void single_collect_local_input(Context* ctx)
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
	if (ctx->ray_triggered)    { attack = 3; ctx->ray_triggered = false; }    // 屏幕"线"按钮：点按单发射线
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
		log_info("[input-raw] dir=%d attack=%d mask=%d", dir, attack, ctx->current_input_mask);
	}
}

static void single_collect_input(Context* ctx)
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

	// 与 server collect_input 同一指纹公式（模拟本帧之前计算），保证单机本地校验也走同管线
	std::vector<ChecksumEntry> entries;
	collect_checksum_entries(ctx->ecs, entries);
	char checksum[64] = { 0 };
	compute_full_md5(entries, checksum, sizeof(checksum));

	auto command_data = pack_s2c(S2C_CMD_COMMAND, frame_id, joins, num_joins, leaves, num_leaves, inputs, num_inputs, checksum);
	ctx->commands[frame_id] = command_data;
	ctx->command_queue.push(command_data);

	// 缓存本帧校验记录（退出时落盘 checksum_log_server.txt，与客户端对称）
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

static void single_init(game_scene_p self)
{
	Context* ctx = self->ctx();
	log_info("single mode init");

	// 创建 mesh（"填充+描边"画风：solid 填充 + wire 描边）
	ctx->mesh_player = r3d_mesh_cube(ctx->r3d, 0.6f, 1.2f, 0.6f);
	ctx->mesh_player_wire = r3d_mesh_cube_wire(ctx->r3d, 0.6f, 1.2f, 0.6f);
	ctx->mesh_enemy = r3d_mesh_cube(ctx->r3d, 0.5f, 1.0f, 0.5f);
	ctx->mesh_enemy_wire = r3d_mesh_cube_wire(ctx->r3d, 0.5f, 1.0f, 0.5f);
	ctx->mesh_prop = r3d_mesh_cube(ctx->r3d, 1.0f, 1.0f, 1.0f);
	ctx->mesh_prop_wire = r3d_mesh_cube_wire(ctx->r3d, 1.0f, 1.0f, 1.0f);
	ctx->mesh_floor = r3d_mesh_plane(ctx->r3d, (float)PF_GRID_N, (float)PF_GRID_N, 1);
	ctx->mesh_floor_wire = r3d_mesh_plane_wire(ctx->r3d, (float)PF_GRID_N, (float)PF_GRID_N, (int)PF_GRID_N);
	ctx->mesh_sky = r3d_mesh_cube(ctx->r3d, 200.0f, 200.0f, 200.0f);

	// 服务端权威系统（单机本地推逻辑）
	add_system(ctx, PostUpdate, "snapshot", snapshot_system);

	// 随机种子
	auto& gs = ctx->ecs.ctx().get<GlobalState>();
	gs.random_seed = (int)time(nullptr);
	ctx->random = utils_random_create((uint32_t)gs.random_seed);

	// 注入本地玩家（conv=1）
	single_inject_local_join(ctx);

	// 音频
	ctx->audio_dev = audio_open_device();
	ctx->step_sound = audio_shot_create("project_proxy3d/sounds/grass-step.wav", ctx->audio_dev);
	audio_shot_set_gain(ctx->step_sound, ctx->settings.sfx_volume);
	ctx->bgm = audio_bgm_create("project_proxy3d/sounds/bgm.wav", ctx->audio_dev);
	audio_bgm_set_gain(ctx->bgm, ctx->settings.bgm_volume);
	// 战斗音效（缺失文件时返回 NULL，播放时跳过）
	ctx->sfx_melee  = audio_shot_create("project_proxy3d/sounds/melee.wav", ctx->audio_dev);
	ctx->sfx_bullet = audio_shot_create("project_proxy3d/sounds/bullet.wav", ctx->audio_dev);
	ctx->sfx_hit    = audio_shot_create("project_proxy3d/sounds/hit.wav", ctx->audio_dev);
	ctx->sfx_kill   = audio_shot_create("project_proxy3d/sounds/kill.wav", ctx->audio_dev);
	ctx->sfx_wave   = audio_shot_create("project_proxy3d/sounds/wave.wav", ctx->audio_dev);
	audio_shot_set_gain(ctx->sfx_melee, ctx->settings.sfx_volume);
	audio_shot_set_gain(ctx->sfx_bullet, ctx->settings.sfx_volume);
	audio_shot_set_gain(ctx->sfx_hit, ctx->settings.sfx_volume);
	audio_shot_set_gain(ctx->sfx_kill, ctx->settings.sfx_volume);
	audio_shot_set_gain(ctx->sfx_wave, ctx->settings.sfx_volume);
}

static void single_handle_event(game_scene_p self, const void* ev)
{
	jevent_callbacks_t cbs = {};
	cbs.on_keypressed = client_on_keypressed;
	cbs.on_keyreleased = client_on_keyreleased;
	cbs.on_mousemoved = client_on_mousemoved;
	dispatch_event(cbs, ev, self);
}

static void single_fixed_update(game_scene_p self, float fixed_step)
{
	(void)fixed_step;
	Context* ctx = self->ctx();
	single_collect_local_input(ctx);
	single_collect_input(ctx);
}

static void single_update(game_scene_p self, float dt)
{
	Context* ctx = self->ctx();

	if (!ctx->command_queue.empty()) {
		std::string command_data = ctx->command_queue.front();
		ctx->command_queue.pop();

		int frame_id, jn = 0, ln = 0, in = 0;
		S2CPlayerJoin joins[32];
		S2CPlayerLeave leaves[32];
		S2CPlayerInput inputs[64];
		char server_checksum[64] = { 0 };
		bool ok = unpack_command(command_data.c_str(), (int)command_data.size(),
			&frame_id, joins, 32, &jn, leaves, 32, &ln, inputs, 64, &in,
			server_checksum, sizeof(server_checksum));
		if (ok) {
			auto& cmd = ctx->ecs.ctx().get<NetComponent>();
			cmd.frame_id = ok ? frame_id : -1;
			cmd.num_joins = jn;  memcpy(cmd.joins, joins, jn * sizeof(S2CPlayerJoin));
			cmd.num_leaves = ln; memcpy(cmd.leaves, leaves, ln * sizeof(S2CPlayerLeave));
			cmd.num_inputs = in; memcpy(cmd.inputs, inputs, in * sizeof(S2CPlayerInput));

			// 帧同步校验：与服务端 collect_input 同一指纹公式、同一时机（模拟本帧之前）。
			// 不一致即时告警，逐帧记录供退出后 diff（结构见 state_checksum.h）。
			std::vector<ChecksumEntry> entries;
			collect_checksum_entries(ctx->ecs, entries);
			char local_checksum[64] = { 0 };
			compute_full_md5(entries, local_checksum, sizeof(local_checksum));

			std::string input_finger;
			for (int k = 0; k < in; k++) {
				char b[64];
				snprintf(b, sizeof(b), "%d:%d:%d:%d;",
					inputs[k].conv, inputs[k].dir,
					(int)inputs[k].attack, (int)inputs[k].yaw_quantized);
				input_finger += b;
			}

			int mismatched = (strcmp(local_checksum, server_checksum) != 0) ? 1 : 0;
			ctx->checksum_records.push_back(
				{ frame_id, "", input_finger, checksum_entries_string(entries) });
			auto& rec = ctx->checksum_records.back();
			memcpy(rec.checksum, local_checksum, sizeof(rec.checksum));
			if (mismatched) {
				// 只提示控制台，不把服务端 checksum 落盘；分叉细节退出后 diff 两端对称日志
				log_info("[MISMATCH] frame=%d", frame_id);
			}
		}

		run_phase(ctx, PreUpdate, ctx->FIXED_TIMESTEP);
		run_phase(ctx, OnUpdate, ctx->FIXED_TIMESTEP);
		run_phase(ctx, PostUpdate, ctx->FIXED_TIMESTEP);
	}

	interpolate_position_pass(ctx, dt);
}

static void single_destroy(game_scene_p self)
{
	Context* ctx = self->ctx();
	audio_shot_destroy(ctx->step_sound);
	audio_bgm_destroy(ctx->bgm);
	audio_shot_destroy(ctx->sfx_melee);
	audio_shot_destroy(ctx->sfx_bullet);
	audio_shot_destroy(ctx->sfx_hit);
	audio_shot_destroy(ctx->sfx_kill);
	audio_shot_destroy(ctx->sfx_wave);
	if (ctx->audio_dev) audio_close_device(ctx->audio_dev);
	if (ctx->mesh_floor_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_floor_wire);
	if (ctx->mesh_floor) r3d_mesh_destroy(ctx->r3d, ctx->mesh_floor);
	if (ctx->mesh_sky) r3d_mesh_destroy(ctx->r3d, ctx->mesh_sky);
	if (ctx->mesh_enemy) r3d_mesh_destroy(ctx->r3d, ctx->mesh_enemy);
	if (ctx->mesh_enemy_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_enemy_wire);
	if (ctx->mesh_player) r3d_mesh_destroy(ctx->r3d, ctx->mesh_player);
	if (ctx->mesh_player_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_player_wire);
	if (ctx->mesh_prop) r3d_mesh_destroy(ctx->r3d, ctx->mesh_prop);
	if (ctx->mesh_prop_wire) r3d_mesh_destroy(ctx->r3d, ctx->mesh_prop_wire);
	if (ctx->random) utils_random_destroy(ctx->random);
}

GameModeInterface g_single_mode = {
	single_init,
	single_handle_event,
	single_fixed_update,
	single_update,
	single_destroy
};

GameModeInterface g_client_mode = {
	client_init,
	client_handle_event,
	client_fixed_update,
	client_update,
	client_destroy
};