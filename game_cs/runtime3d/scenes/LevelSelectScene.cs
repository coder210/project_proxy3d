using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>选关（C++ level_select_scene.cpp）：简化为 3 关按钮 + 开始。</summary>
    internal sealed class LevelSelectScene : IScene3D
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly GameMode _mode;
        private readonly Jr3dButton[] _levels = new Jr3dButton[3];
        private Jr3dButton _start, _back;
        private int _selectedLevel = 1;

        public string Name => "LevelSelect";
        public LevelSelectScene(GameContext ctx, Ui3D ui, GameMode mode) { _ctx = ctx; _ui = ui; _mode = mode; }

        public void Enter() {
            _selectedLevel = Math.Max(1, Math.Min(3, _ctx.SelectedLevel));
            for (int i = 0; i < 3; i++)
                _levels[i] = _ui.Button(180, 50 + i * 50, 120, 40, "第 " + (i + 1) + " 关");
            _start = _ui.Button(180, 210, 120, 40, "进入");
            _back = _ui.Button(180, 262, 120, 40, "返回");
            Joy.Host.log("[scene] LevelSelect");
        }

        public void Update(float dt) {
            float mx = Joy.Input.mouse_x(), my = Joy.Input.mouse_y();
            bool down = Joy.Input.mouse_button(1);
            for (int i = 0; i < 3; i++)
                if (_ui.Clicked(_levels[i], mx, my, down)) _selectedLevel = i + 1;
            if (_ui.Clicked(_start, mx, my, down)) {
                _ctx.SelectedLevel = _selectedLevel;
                _ctx.Scenes.Change(new LoadingScene(_ctx, _ui, _mode));
            } else if (_ui.Clicked(_back, mx, my, down)) {
                _ctx.Scenes.Change(new CharacterSelectScene(_ctx, _ui, _mode));
            }
        }

        public void OnEvent(UserEvent e) { }
        public void Destroy() {
            for (int i = 0; i < 3; i++) Bindings.R3dButtonDestroy(_levels[i]);
            Bindings.R3dButtonDestroy(_start); Bindings.R3dButtonDestroy(_back);
        }

        public void Render() {
            _ui.BeginFrame();
            _ui.Text("选择关卡", 190, 12, 28, 30, 30, 40);
            for (int i = 0; i < 3; i++) _ui.DrawButton(_levels[i]);
            _ui.DrawButton(_start); _ui.DrawButton(_back);
            _ui.EndFrame();
        }
    }
}
