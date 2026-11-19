using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/input_consume_join_system.h：消费网络加入请求、创建玩家实体。
    /// 去重：同 conv 已有存活玩家则跳过；空槽位按数组下标升序取（确定性）。
    /// 血量上限取角色表（character_def），出生点=加入位置；首次加入者作为摄像机跟随目标。
    /// 渲染 Mesh 不参与帧同步，不迁移。
    /// </summary>
    public static class InputConsumeJoinSystem
    {
        public static void Update(GameState state)
        {
            var net = state.Net;
            if (net.FrameId < 0) return;

            for (int i = 0; i < net.NumJoins; ++i)
            {
                var j = net.Joins[i];

                // 去重：同 conv 的存活玩家已存在则跳过
                bool exists = false;
                for (int k = 0; k < state.Players.Length; ++k)
                {
                    var p = state.Players[k];
                    if (p.Alive && p.Conv == j.Conv) { exists = true; break; }
                }
                if (exists) continue;

                // 取第一个空槽（等价 registry.create；新 id 由 AllocId 分配）
                int slot = -1;
                for (int k = 0; k < state.Players.Length; ++k)
                {
                    if (!state.Players[k].Alive) { slot = k; break; }
                }
                if (slot < 0) continue;

                int charId = (j.CharId >= 0 && j.CharId < GameConst.CharCount)
                    ? j.CharId : GameConst.CharWarrior;  // character_def() 越界回退战士
                var cd = state.Characters[charId];

                state.Players[slot] = new Entity {
                    Id = state.AllocId(),
                    Kind = EntityKind.Player,
                    Alive = true,
                    Conv = j.Conv,
                    CharId = j.CharId,
                    Nickname = j.Nickname,
                    X = j.X, Y = j.Y, Z = j.Z,
                    Hp = cd.MaxHp, MaxHp = cd.MaxHp,
                    SpawnX = j.X, SpawnY = j.Y, SpawnZ = j.Z,
                    OnGround = true,                      // PlayerPhysicsComponent 默认
                    ActionState = 0,                      // ActionStateComponent 默认 IDLE
                    ColliderType = ColliderType.Box,      // ColliderComponent 默认 BOX
                    ColHx = Fp.FromFloat(0.3f), ColHy = Fp.FromFloat(0.6f), ColHz = Fp.FromFloat(0.3f),
                    ColRadius = Fp.FromFloat(0.3f),
                };

                if (state.CameraFollowConv < 0) state.CameraFollowConv = j.Conv;
            }
            net.NumJoins = 0;
        }
    }
}
