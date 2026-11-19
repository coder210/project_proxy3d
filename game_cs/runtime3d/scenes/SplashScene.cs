using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>闪屏：3 秒后切主菜单（C++ splash_scene.cpp）。</summary>
    internal sealed class SplashScene : IScene3D
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly UiParticles _particles;
        private float _elapsed;

        public string Name => "Splash";
        public SplashScene(GameContext ctx, Ui3D ui) { _ctx = ctx; _ui = ui; _particles = new UiParticles(); }

        public void Enter() { _elapsed = 0f; Joy.Host.log("[scene] Splash"); }
        public void Update(float dt) {
            _elapsed += dt;
            _particles.Update(dt);
            if (_elapsed >= 3.0f) _ctx.Scenes.Change(new MainMenuScene(_ctx, _ui));
        }
        public void OnEvent(UserEvent e) { }
        public void Destroy() { }

        public void Render() {
            _ui.BeginFrame();
            _particles.Draw(_ui);
            _ui.Text("Project Proxy 3D", 140, 170, 24, 60, 60, 70);
            _ui.Text("Developed by 大懒虫工作室, 228407354@qq.com", 80, 296, 14, 60, 60, 70);
            _ui.EndFrame();
        }
    }
}
