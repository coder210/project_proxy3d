using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>
    /// C# 3D 应用外壳（对齐 joy examples/csharp 的 GameApp，但用 r3d 而非 r2d）。
    /// 窗口与 r3d 由本类通过生成链路 + R3dExt 手写适配自建，C++ 宿主只泵事件/主循环。
    /// 当前里程碑：单个战斗场景，逻辑来自 ProxyGame.GameState，渲染走 Renderer3D。
    /// </summary>
    internal sealed class GameApp3D
    {
        public Jwindow Window { get; }
        public Jr3d R3d { get; }
        public int Width { get; }
        public int Height { get; }

        public Renderer3D Renderer { get; private set; }
        public ProxyGame.GameState State { get; } = new ProxyGame.GameState();

        public int Frame { get; private set; }

        // 逻辑固定步累加器（与 C++ FIXED_TIMESTEP=1/20 一致）
        private const float FixedDt = 1f / 20f;
        private float _accumulator;

        public GameApp3D(int width, int height)
        {
            Width = width;
            Height = height;

            Window = Bindings.CreateWindow("Project Proxy3D (C#)", width, height);
            if (Window == null) throw new InvalidOperationException("CreateWindow failed");
            R3d = R3dExt.R3dCreate(Window, "shaders");
            if (R3d == null) throw new InvalidOperationException("R3dCreate failed (GPU/shaders)");

            // 设计分辨率 + LETTERBOX（2 = JOY_LOGICAL_PRESENTATION_LETTERBOX）
            Bindings.R3dSetLogicalPresentationMode(R3d, width, height, 2);
            Renderer = new Renderer3D(R3d, width, height);
        }

        public void Start()
        {
            State.Reset();
            // 单机默认场景：放一个本地玩家作为相机跟随目标，便于首帧可见。
            var p = new ProxyGame.Entity {
                Id = State.AllocId(), Kind = ProxyGame.EntityKind.Player, Alive = true,
                Conv = 1, CharId = ProxyGame.GameConst.CharWarrior, Nickname = "P1",
                X = ProxyGame.Fp.FromInt(0), Y = ProxyGame.Fp.FromInt(0), Z = ProxyGame.Fp.FromInt(0),
                Hp = ProxyGame.GameConst.PlayerMaxHp, MaxHp = ProxyGame.GameConst.PlayerMaxHp,
                ColHx = ProxyGame.Fp.Div(ProxyGame.Fp.One(), ProxyGame.Fp.FromInt(2)),
                ColHz = ProxyGame.Fp.Div(ProxyGame.Fp.One(), ProxyGame.Fp.FromInt(2))
            };
            State.Players[0] = p;
            State.CameraFollowConv = 1;
            Joy.Host.log("[cs3d] started");
        }

        public void Tick(float dt)
        {
            if (dt < 0f) dt = 0f;
            if (dt > 0.1f) dt = 0.1f;

            _accumulator += dt;
            while (_accumulator >= FixedDt)
            {
                LogicTick(FixedDt);
                _accumulator -= FixedDt;
            }

            Frame++;
            Renderer.RenderFrame(State, dt);
        }

        private void LogicTick(float dt)
        {
            State.Frame++;
            ProxyGame.Systems.InputConsumeJoinSystem.Update(State);
            ProxyGame.Systems.InputConsumeLeaveSystem.Update(State);
            ProxyGame.Systems.InputConsumeInputSystem.Update(State);
            ProxyGame.Systems.PlayerDeathSystem.Update(State);
            ProxyGame.Systems.MovementSystem.Update(State);
            ProxyGame.Systems.KnockbackSystem.Update(State);
            ProxyGame.Systems.CleanupSystem.Update(State);
        }
    }
}
