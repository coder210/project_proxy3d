using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>加载过渡（C++ loading_scene.cpp）：0.5s 重置状态，1.5s 进战斗。</summary>
    internal sealed class LoadingScene : IScene3D
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly UiParticles _particles;
        private readonly GameMode _targetMode;
        private float _elapsed;
        private int _dots;
        private float _dotTimer;
        private bool _loaded;

        public string Name => "Loading";
        public LoadingScene(GameContext ctx, Ui3D ui, GameMode mode) { _ctx = ctx; _ui = ui; _targetMode = mode; _particles = new UiParticles(); }

        public void Enter() { _elapsed = 0f; _loaded = false; _dots = 0; Joy.Host.log("[scene] Loading mode=" + _targetMode); }
        public void Update(float dt) {
            _elapsed += dt;
            _particles.Update(dt);
            _dotTimer += dt;
            if (_dotTimer >= 0.4f) { _dotTimer = 0f; _dots = (_dots + 1) % 4; }
            if (!_loaded && _elapsed >= 0.5f) {
                _ctx.Mode = _targetMode;
                _ctx.State.Reset();
                _loaded = true;
            }
            if (_loaded && _elapsed >= 1.5f)
                _ctx.Scenes.Change(new BattleScene(_ctx, _ui));
        }
        public void OnEvent(UserEvent e) { }
        public void Destroy() { }

        public void Render() {
            _ui.BeginFrame();
            _particles.Draw(_ui);
            string text = "加载中" + new string('.', _dots);
            _ui.Text(text, 180, 148, 24, 60, 60, 70);
            _ui.EndFrame();
        }
    }
}
