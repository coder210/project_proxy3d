using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>通关结算（C++ victory_scene.cpp）：再战一局 / 回主菜单。</summary>
    internal sealed class VictoryScene : IScene3D
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly UiParticles _particles;
        private readonly Jr3dButton[] _btns = new Jr3dButton[2];

        public string Name => "Victory";
        public VictoryScene(GameContext ctx, Ui3D ui) { _ctx = ctx; _ui = ui; _particles = new UiParticles(); }

        public void Enter() {
            _btns[0] = _ui.Button(70, 250, 160, 50, "再战一局");
            _btns[1] = _ui.Button(250, 250, 160, 50, "回主菜单");
            Joy.Host.log("[scene] Victory");
        }
        public void Update(float dt) {
            _particles.Update(dt);
            float mx = Joy.Input.mouse_x(), my = Joy.Input.mouse_y();
            bool down = Joy.Input.mouse_button(1);
            if (_ui.Clicked(_btns[0], mx, my, down)) _ctx.Scenes.Change(new LevelSelectScene(_ctx, _ui, _ctx.Mode));
            else if (_ui.Clicked(_btns[1], mx, my, down)) _ctx.Scenes.Change(new MainMenuScene(_ctx, _ui));
        }
        public void OnEvent(UserEvent e) { }
        public void Destroy() { Bindings.R3dButtonDestroy(_btns[0]); Bindings.R3dButtonDestroy(_btns[1]); }

        public void Render() {
            _ui.BeginFrame();
            _particles.Draw(_ui);
            _ui.Text("通  关", 196, 70, 40, 20, 120, 60);
            _ui.Text("第 " + _ctx.SelectedLevel + " 关的所有敌人已被肃清", 130, 132, 18, 60, 60, 70);
            _ui.Text("恭喜通关！", 198, 168, 16, 110, 110, 118);
            _ui.DrawButton(_btns[0]); _ui.DrawButton(_btns[1]);
            _ui.EndFrame();
        }
    }
}
