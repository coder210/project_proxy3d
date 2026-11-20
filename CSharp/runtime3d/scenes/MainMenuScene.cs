using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>主菜单（C++ main_menu_scene.cpp）：4 按钮 + 呼吸标题 + 粒子。</summary>
    internal sealed class MainMenuScene : IScene3D
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly UiParticles _particles;
        private float _elapsed;

        private Jr3dButton _start, _online, _settings, _exit;

        public string Name => "MainMenu";
        public MainMenuScene(GameContext ctx, Ui3D ui) { _ctx = ctx; _ui = ui; _particles = new UiParticles(); }

        public void Enter() {
            _elapsed = 0f;
            _start = _ui.Button(180, 30, 120, 50, "单人游戏");
            _online = _ui.Button(180, 100, 120, 50, "联机游戏");
            _settings = _ui.Button(180, 170, 120, 50, "设置");
            _exit = _ui.Button(180, 240, 120, 50, "退出");
            Joy.Host.log("[scene] MainMenu");
        }

        public void Update(float dt) {
            _elapsed += dt;
            _particles.Update(dt);
            float mx = Joy.Input.mouse_x(), my = Joy.Input.mouse_y();
            bool down = Joy.Input.mouse_button(1);

            if (_ui.Clicked(_start, mx, my, down)) _ctx.Scenes.Change(new CharacterSelectScene(_ctx, _ui, GameMode.Single));
            else if (_ui.Clicked(_online, mx, my, down)) _ctx.Scenes.Change(new OnlineScene(_ctx, _ui));
            else if (_ui.Clicked(_settings, mx, my, down)) _ctx.Scenes.Change(new SettingsScene(_ctx, _ui));
            else if (_ui.Clicked(_exit, mx, my, down)) Joy.Host.quit();

            if (Joy.Input.key_down((int)Keys.Escape)) Joy.Host.quit();
        }

        public void OnEvent(UserEvent e) { }
        public void Destroy() {
            Bindings.R3dButtonDestroy(_start); Bindings.R3dButtonDestroy(_online);
            Bindings.R3dButtonDestroy(_settings); Bindings.R3dButtonDestroy(_exit);
        }

        public void Render() {
            _ui.BeginFrame();
            _particles.Draw(_ui);
            float pulse = 0.8f + 0.2f * (float)Math.Sin(_elapsed * 1.8);
            byte c = (byte)(60 * pulse);
            _ui.Text("主菜单", 210, 28, 24, c, c, (byte)(70 * pulse));
            _ui.Text("v1.0.0", 215, 300, 12, 60, 60, 70);
            _ui.DrawButton(_start); _ui.DrawButton(_online);
            _ui.DrawButton(_settings); _ui.DrawButton(_exit);
            _ui.EndFrame();
        }
    }
}
