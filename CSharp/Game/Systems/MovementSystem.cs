using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/movement_system.h。
    /// 所有运算经 Fp（joy 的 joy_math_fp_* C API），与 C++ 同一份实现，保证帧同步确定性。
    /// 只处理玩家（C++ 的 view&lt;PlayerStateInput, LogicPositionComponent&gt; 实际只有玩家带这两个组件）。
    /// </summary>
    public static class MovementSystem
    {
        // 常量（movement_system.h:16-24；PF_GRID_OFFSET=16，地图 32×32）
        private static readonly long GRAVITY = Fp.FromFloat(9.8f);
        private static readonly long JUMP_SPEED = Fp.FromFloat(4.0f);
        private static readonly long GROUND_Y = Fp.FromFloat(0.6f);
        private static readonly long WALL_HALF = Fp.Div(Fp.FromInt(1), Fp.FromInt(2));
        private static readonly long WALL_LIMIT = Fp.Sub(Fp.FromInt(16), WALL_HALF);    // +15.5
        private static readonly long WALL_NEG = Fp.Sub(Fp.FromInt(-16), -WALL_HALF);    // -15.5

        // 输入位（protocol.h:14-17）
        private const byte INPUT_UP = 1, INPUT_DOWN = 2, INPUT_LEFT = 4, INPUT_RIGHT = 8;

        public static void Update(GameState state)
        {
            // FIXED_TIMESTEP = 1/20，C++ 用 float 再 fp_from_float
            long dt = Fp.FromFloat(0.05f);

            for (int i = 0; i < state.Players.Length; ++i)
            {
                var p = state.Players[i];
                if (!p.Alive || p.Dead) continue;   // 死亡玩家不移动

                // 移速按角色定义表
                CharacterDef cd = state.Characters[p.CharId >= 0 && p.CharId < GameConst.CharCount
                    ? p.CharId : GameConst.CharWarrior];
                long step = Fp.Mul(cd.MoveSpeed, dt);

                // 朝向：量化角度 → 弧度（纯定点）
                long yawFp = Fp.Mul(
                    Fp.Div(Fp.FromInt(p.YawQuantized), Fp.FromInt(32767)), Fp.Pi());
                p.Yaw = Fp.ToFloat(yawFp);   // 仅表现（ActionStateComponent.yaw）

                // MOBA 标准：WASD 相对相机（cam_yaw=0 → sy=0, cy=1）
                long sy = Fp.Zero;
                long cy = Fp.One();
                long fdx = Fp.Zero, fdz = Fp.Zero;
                if ((p.MoveBits & INPUT_UP) != 0)    { fdx = Fp.Add(fdx, sy); fdz = Fp.Add(fdz, cy); }
                if ((p.MoveBits & INPUT_DOWN) != 0)  { fdx = Fp.Sub(fdx, sy); fdz = Fp.Sub(fdz, cy); }
                if ((p.MoveBits & INPUT_LEFT) != 0)  { fdx = Fp.Add(fdx, cy); fdz = Fp.Sub(fdz, sy); }
                if ((p.MoveBits & INPUT_RIGHT) != 0) { fdx = Fp.Sub(fdx, cy); fdz = Fp.Add(fdz, sy); }
                if (fdx != Fp.Zero || fdz != Fp.Zero)
                {
                    long len = Fp.Sqrt(Fp.Add(Fp.Mul(fdx, fdx), Fp.Mul(fdz, fdz)));
                    p.X = Fp.Add(p.X, Fp.Mul(Fp.Div(fdx, len), step));
                    p.Z = Fp.Add(p.Z, Fp.Mul(Fp.Div(fdz, len), step));
                }

                // 垂直物理（重力/跳跃/落地）
                if (p.JumpRequested && p.OnGround)
                {
                    p.VerticalVelocity = JUMP_SPEED;
                    p.OnGround = false;
                }
                p.JumpRequested = false;
                p.VerticalVelocity = Fp.Sub(p.VerticalVelocity, Fp.Mul(GRAVITY, dt));
                p.Y = Fp.Add(p.Y, Fp.Mul(p.VerticalVelocity, dt));
                if (Fp.ToFloat(p.Y) <= Fp.ToFloat(GROUND_Y))
                {
                    p.Y = GROUND_Y;
                    p.VerticalVelocity = Fp.Zero;
                    p.OnGround = true;
                }

                // 空气墙
                p.X = Fp.Clamp(p.X, WALL_NEG, WALL_LIMIT);
                p.Z = Fp.Clamp(p.Z, WALL_NEG, WALL_LIMIT);

                state.Players[i] = p;   // struct 是值类型，写回
            }
        }
    }
}
