using System;

namespace ProxyGame
{
    /// <summary>
    /// 全局游戏状态（无 ECS）。所有系统读写这一份确定性的 struct 数组，
    /// 引用用整数 id（取代 EnTT 的 entity 句柄）。
    /// 字段摊平自 shared/components/ 的 26 个组件；定点数用 long（Q32）。
    /// </summary>
    public static class GameConst
    {
        public const int PlayerMaxHp = 100;
        public const int EnemyMaxHp = 50;
        public const int PlayerRespawnTicks = 90;
        public const int SpawnInvulnTicks = 60;

        public const int MaxPlayers = 16;
        public const int MaxEnemies = 64;
        public const int MaxBullets = 256;

        public const int CharWarrior = 0;
        public const int CharGunner = 1;
        public const int CharAssassin = 2;
        public const int CharCount = 3;

        // 输入位掩码（复刻 shared/protocol.h）
        public const int InputUp = 1, InputDown = 2, InputLeft = 4, InputRight = 8;
        public const int InputAttack = 16, InputJump = 32, InputBullet = 64, InputRay = 128;
        public const int InputDirMask = InputUp | InputDown | InputLeft | InputRight;

        // 攻击动作类型（复刻 PlayerEventInput::ActionType）
        public const byte AttackNone = 0, AttackMelee = 1, AttackBullet = 2, AttackRay = 3;
    }

    /// <summary>实体种类（取代多组件组合的 tag）。</summary>
    public enum EntityKind : byte { None = 0, Player = 1, Enemy = 2 }

    /// <summary>
    /// 统一实体布局（玩家/敌人共用，按 Kind 分支）。这样既避免 ECS，
    /// 又让遍历/checksum 顺序完全确定（按下标升序）。
    /// </summary>
    public struct Entity
    {
        public int Id;                 // 稳定实体 id
        public EntityKind Kind;
        public bool Alive;

        // LogicPositionComponent / LogicVelocityComponent
        public long X, Y, Z;
        public long VX, VY, VZ;

        // HealthComponent
        public int Hp, MaxHp;
        public bool Dead;
        public int RespawnTimer, SpawnInvuln;

        // IdComponent / PlayerComponent
        public int Conv;
        public int CharId;
        public string Nickname;

        // AIComponent（敌人用）
        public long PatrolCenterX, PatrolCenterZ;
        public long PatrolRadius, DetectRadius, AttackRadius, GuardRadius;
        public int AiState;            // 0=巡逻 1=追击 2=攻击 3=返回
        public int AiTimer;
        public long TargetX, TargetZ;

        // SpawnIdxComponent
        public int SpawnIdx;

        // SpawnPointComponent（重生点，player_death_system 用）
        public long SpawnX, SpawnY, SpawnZ;

        // KnockbackComponent（受击击退，knockback_system 用）
        public long KnockVx, KnockVz;
        public int KnockTicksLeft;

        // PlayerStateInput（输入位掩码 + 量化朝向）
        public byte MoveBits;            // bit0=W bit1=S bit2=A bit3=D
        public short YawQuantized, PitchQuantized;

        // PlayerPhysicsComponent（垂直物理）
        public long VerticalVelocity;    // fp
        public bool OnGround, JumpPrev, JumpRequested;

        // ActionStateComponent
        public int ActionState;          // 0=IDLE 1=WALK 2=ATTACK
        public float Yaw;                // 弧度（仅表现，不参与 logic 判定）
        public int MeleeCooldown, BulletCooldown, RayCooldown;

        // PlayerEventInput（本帧待处理攻击动作；C++ 用 pending_actions 队列，语义等价）
        public bool PendingMelee, PendingBullet, PendingRay;

        // PathComponent（ai_system 写目标格并置 dirty，pathfinding 消费）
        public int PathTargetGx, PathTargetGz;
        public bool PathDirty;

        // ColliderComponent（项目里玩家/敌人均为 BOX，half_size 米→fp）
        public byte ColliderType;         // 0=NONE 1=BOX 2=SPHERE 3=OBB 4=MESH
        public long ColHx, ColHy, ColHz;  // 半尺寸（BOX/OBB）
        public long ColRadius;            // SPHERE
    }

    public static class ColliderType
    {
        public const byte None = 0, Box = 1, Sphere = 2, Obb = 3, Mesh = 4;
    }

    /// <summary>静态障碍（摊平自 StaticMeshComponent + ColliderComponent + LogicPositionComponent）。</summary>
    public struct Obstacle
    {
        public int Id;
        public long X, Y, Z;      // 中心（fp）
        public long Hx, Hz;       // XZ 半尺寸（fp）
        public long Sy;           // 全高（fp）
        public int Gx0, Gz0, Gw, Gh;
    }

    /// <summary>子弹（敌我共用），摊平自 BulletComponent。</summary>
    public struct Bullet
    {
        public bool Alive;
        public long X, Y, Z;
        public long DX, DZ;
        public long Speed;
        public long RangeLeft;
        public int Damage;
        public bool FromEnemy;
        public int OwnerId;
    }

    /// <summary>网络命令队列，摊平自 components/net_component.h（由网络层/宿主在 tick 前填充）。</summary>
    public struct NetJoin
    {
        public int Conv;
        public long X, Y, Z;
        public int CharId;
        public string Nickname;
    }
    public struct NetLeave { public int Conv; }
    public struct NetInput
    {
        public uint ClientSeq;
        public int Conv, Dir;
        public int Attack;      // 0=无 1=近战 2=射击 3=射线
        public short YawQuantized;
    }
    public sealed class NetCommands
    {
        public int FrameId;
        public NetJoin[] Joins = new NetJoin[32];
        public int NumJoins;
        public NetLeave[] Leaves = new NetLeave[32];
        public int NumLeaves;
        public NetInput[] Inputs = new NetInput[64];
        public int NumInputs;
    }

    /// <summary>角色定义表，摊平自 character_def.h（帧同步数值为整数/定点）。</summary>
    public struct CharacterDef
    {
        public int CharId;
        public string Name;
        public int MaxHp;
        public int MeleeDamage;
        public long MeleeRange;
        public int BulletDamage;
        public int RayDamage;
        public long RayRange;
        public long MoveSpeed;
    }

    /// <summary>全部可变状态。系统的入口就是 Update(GameState, dt)。</summary>
    public sealed class GameState
    {
        public readonly Entity[] Players = new Entity[GameConst.MaxPlayers];
        public readonly Entity[] Enemies = new Entity[GameConst.MaxEnemies];
        public readonly Bullet[] Bullets = new Bullet[GameConst.MaxBullets];
        public readonly Obstacle[] Obstacles = new Obstacle[MapConst.MaxObs];
        public int ObstacleCount = 0;

        public int NextEntityId = 1;
        public int Frame = 0;
        public int Wave = 0;
        public int Level = 0;
        public int MapSeed = 0;
        public bool LevelCleared = false;
        public int Kills = 0;
        public int BulletCount = 0;

        // 网络命令队列（等价 C++ registry.ctx().get<NetComponent>()）
        public readonly NetCommands Net = new NetCommands();
        public int CameraFollowConv = -1;  // 首次加入的玩家作为摄像机跟随目标

        // -character_def.h 的角色表（骨架先填常量，具体数值迁移 character_def 系统时校准）
        public readonly CharacterDef[] Characters = new CharacterDef[GameConst.CharCount];

        public GameState()
        {
            InitCharacters();
        }

        private void InitCharacters()
        {
            // 数值逐项复刻自 character_def.h（定点数经 Fp.Div，与 C++ 逐位一致）。
            Characters[GameConst.CharWarrior] = new CharacterDef {
                CharId = GameConst.CharWarrior, Name = "战士", MaxHp = 150,
                MeleeDamage = 35, MeleeRange = Fp.Div(Fp.FromInt(9), Fp.FromInt(5)),
                BulletDamage = 8, RayDamage = 25,
                RayRange = Fp.Div(Fp.FromInt(8), Fp.FromInt(1)),
                MoveSpeed = Fp.Div(Fp.FromInt(9), Fp.FromInt(2)) };
            Characters[GameConst.CharGunner] = new CharacterDef {
                CharId = GameConst.CharGunner, Name = "射手", MaxHp = 80,
                MeleeDamage = 20, MeleeRange = Fp.Div(Fp.FromInt(3), Fp.FromInt(2)),
                BulletDamage = 15, RayDamage = 30,
                RayRange = Fp.Div(Fp.FromInt(12), Fp.FromInt(1)),
                MoveSpeed = Fp.Div(Fp.FromInt(11), Fp.FromInt(2)) };
            Characters[GameConst.CharAssassin] = new CharacterDef {
                CharId = GameConst.CharAssassin, Name = "刺客", MaxHp = 70,
                MeleeDamage = 30, MeleeRange = Fp.Div(Fp.FromInt(8), Fp.FromInt(5)),
                BulletDamage = 10, RayDamage = 40,
                RayRange = Fp.Div(Fp.FromInt(6), Fp.FromInt(1)),
                MoveSpeed = Fp.Div(Fp.FromInt(13), Fp.FromInt(2)) };
        }

        /// <summary>分配一个稳定实体 id（帧同步确定：只在双端同序调用）。</summary>
        public int AllocId() => NextEntityId++;

        public void Reset()
        {
            Array.Clear(Players, 0, Players.Length);
            Array.Clear(Enemies, 0, Enemies.Length);
            Array.Clear(Bullets, 0, Bullets.Length);
            NextEntityId = 1;
            Frame = 0;
            Wave = 0;
            CameraFollowConv = -1;
            Net.FrameId = 0;
            Net.NumJoins = 0;
            Net.NumLeaves = 0;
            Net.NumInputs = 0;
        }
    }
}
