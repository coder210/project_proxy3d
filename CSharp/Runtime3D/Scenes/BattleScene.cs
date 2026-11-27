using System;
using Joy.Api;
using ProxyGame;

namespace JoyGame
{
    /// <summary>
    /// 战斗场景（C# 版 game_scene）：持有 GameState + Renderer3D，固定步跑逻辑系统，
    /// 键盘 WASD/JKL 与暂停面板写输入掩码，胜利后切 VictoryScene。
    /// 网络 host/client/server 模式框架见 NetworkMode.cs（单机路径完整可用）。
    /// </summary>
    internal sealed class BattleScene : IScene3D, IFixedUpdateScene
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly Renderer3D _renderer;
        private readonly NetworkMode _net;
        private readonly VirtualControls _virtual = new VirtualControls();
        private bool _victoryStarted;
        private float _victoryDelay;
        private bool _paused;
        private bool _prevEsc, _prevUp, _prevBullet, _prevRay;
        private bool _prevF1, _prevF2;

        public string Name => "Battle";
        public BattleScene(GameContext ctx, Ui3D ui) {
            _ctx = ctx; _ui = ui;
            _renderer = new Renderer3D(ui.R, 480, 320);
            _net = new NetworkMode(ctx);
        }

        public void Enter() {
            _victoryStarted = false;
            _victoryDelay = 0f;
            _paused = false;
            // 应用选关页选定的关卡（C++ game_scene.cpp:58 gs.level = ctx->selected_level）
            _ctx.State.Level = Math.Max(1, Math.Min(ProxyGame.MapTables.LevelCount, _ctx.SelectedLevel));
            _ctx.State.PfWalkableDirty = true;

            // 单机/本地：确保有一个本地玩家作为相机跟随目标
            bool hasLocal = false;
            for (int i = 0; i < GameConst.MaxPlayers; i++)
                if (_ctx.State.Players[i].Alive) { hasLocal = true; break; }
            if (!hasLocal && _ctx.Mode != GameMode.Server) {
                _ctx.State.Players[0] = new Entity {
                    Id = _ctx.State.AllocId(), Kind = EntityKind.Player, Alive = true,
                    Conv = 1, CharId = _ctx.SelectedCharId, Nickname = _ctx.SelectedNickname,
                    X = Fp.FromInt(0), Y = Fp.FromInt(0), Z = Fp.FromInt(0),
                    Hp = _ctx.State.Characters[_ctx.SelectedCharId].MaxHp,
                    MaxHp = _ctx.State.Characters[_ctx.SelectedCharId].MaxHp,
                    // ColliderComponent（对齐 InputConsumeJoinSystem：BOX 0.5×1.2×0.5）
                    ColliderType = ColliderType.Box,
                    ColHx = Fp.FromFloat(0.3f), ColHy = Fp.FromFloat(0.6f), ColHz = Fp.FromFloat(0.3f),
                    OnGround = true
                };
                _ctx.State.CameraFollowConv = 1;
            }
            _net.Init();
            Joy.Host.log("[scene] Battle mode=" + _ctx.Mode);
        }

        public void Update(float dt) {
            if (dt < 0f) dt = 0f;
            if (dt > 0.1f) dt = 0.1f;

            // ESC 暂停（边沿触发）
            bool escNow = Joy.Input.key_down(Keys.Escape);
            if (escNow && !_prevEsc) _paused = !_paused;
            _prevEsc = escNow;

            // F1 调试 HUD / F2 碰撞盒（边沿触发）
            bool f1Now = Joy.Input.key_down(Keys.F1);
            if (f1Now && !_prevF1) _ctx.ShowDebugHud = !_ctx.ShowDebugHud;
            _prevF1 = f1Now;
            bool f2Now = Joy.Input.key_down(Keys.F2);
            if (f2Now && !_prevF2) _ctx.ShowCollisionBoxes = !_ctx.ShowCollisionBoxes;
            _prevF2 = f2Now;

            if (_paused) return;

            CollectInput();
            _virtual.SyncKeyboardVisuals(_ctx.InputMask,
                Joy.Input.key_down(Keys.J) || Joy.Input.key_down(Keys.Space));
            _net.Update(dt);

            // 胜利检测（权威 LevelCleared 优先，兜底实体扫描；对齐 C++ game_scene.cpp:129-146）
            if (!_victoryStarted && _ctx.State.Wave > 0) {
                bool cleared = _ctx.State.LevelCleared;
                if (!cleared) {
                    cleared = true;
                    for (int i = 0; i < GameConst.MaxEnemies; i++)
                        if (_ctx.State.Enemies[i].Alive && !_ctx.State.Enemies[i].Dead) { cleared = false; break; }
                }
                if (cleared) { _victoryDelay += dt; if (_victoryDelay >= 1.2f) { _victoryStarted = true; _ctx.Scenes.Change(new VictoryScene(_ctx, _ui)); return; } }
            }
        }

        private void CollectInput() {
            byte mask = 0;
            if (Joy.Input.key_down(Keys.W) || Joy.Input.key_down(Keys.Up)) mask |= GameConst.InputUp;
            if (Joy.Input.key_down(Keys.S) || Joy.Input.key_down(Keys.Down)) mask |= GameConst.InputDown;
            if (Joy.Input.key_down(Keys.A) || Joy.Input.key_down(Keys.Left)) mask |= GameConst.InputLeft;
            if (Joy.Input.key_down(Keys.D) || Joy.Input.key_down(Keys.Right)) mask |= GameConst.InputRight;
            if (Joy.Input.key_down(Keys.Space)) mask |= GameConst.InputJump;

            // 虚拟按键（手机端）合并到同一输入掩码
            bool virtualMelee;
            _virtual.Collect(ref mask, out virtualMelee);

            _ctx.InputMask = mask;

            // 攻击上升沿：J=近战 K=子弹 L=射线（虚拟攻击钮并入近战）
            bool up = Joy.Input.key_down(Keys.J) || virtualMelee;
            bool bullet = Joy.Input.key_down(Keys.K);
            bool ray = Joy.Input.key_down(Keys.L);
            _ctx.AttackTriggered = up && !_prevUp;
            _ctx.BulletTriggered = bullet && !_prevBullet;
            _ctx.RayTriggered = ray && !_prevRay;
            _prevUp = up; _prevBullet = bullet; _prevRay = ray;

            _net.CollectLocalInput();
        }

        public void FixedUpdate(float dt) {
            if (_paused) return;
            LogicTick(dt);
        }

        private void LogicTick(float dt) {
            var s = _ctx.State;
            _renderer.Snapshot(s);
            s.Frame++;
            // 系统顺序对齐 C++ game_scene.cpp:73-86
            ProxyGame.Systems.InputConsumeJoinSystem.Update(s);
            ProxyGame.Systems.InputConsumeLeaveSystem.Update(s);
            ProxyGame.Systems.InputConsumeInputSystem.Update(s);
            ProxyGame.Systems.AiSystem.Update(s);
            ProxyGame.Systems.PathSystem.Update(s);
            ProxyGame.Systems.MeleeAttackSystem.Update(s);
            ProxyGame.Systems.BulletSystem.Update(s);
            ProxyGame.Systems.PlayerDeathSystem.Update(s);
            ProxyGame.Systems.WaveSystem.Update(s);
            ProxyGame.Systems.MovementSystem.Update(s);
            ProxyGame.Systems.KnockbackSystem.Update(s);
            ProxyGame.Systems.CollisionSystem.Update(s);
            ProxyGame.Systems.CleanupSystem.Update(s);
            _ctx.ClearInput();
        }

        public void Render() {
            float alpha = GameApplication.FixedAlpha;
            if (alpha < 0f) alpha = 0f;
            if (alpha > 1f) alpha = 1f;
            _renderer.RenderFrame(_ctx.State, _ctx.RenderDt, alpha,
                _ctx.ShowDebugHud, _ctx.ShowCollisionBoxes, _virtual);
            if (_paused) _ui.Text("[暂停] ESC 继续，返回主菜单请用暂停面板", 120, 8, 14, 40, 40, 48);
        }

        public void OnEvent(UserEvent e) { }
        public void Destroy() { _net.Destroy(); }
    }
}
