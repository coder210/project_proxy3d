using System;
using Joy.Api;
using ProxyGame;

namespace JoyGame
{
    /// <summary>选角色（C++ character_select_scene.cpp）：3 卡片 + 昵称 + 开始/返回。</summary>
    internal sealed class CharacterSelectScene : IScene3D
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly GameMode _mode;
        private readonly Jr3dButton[] _cards = new Jr3dButton[3];
        private Jr3dInput _nickname;
        private Jr3dButton _confirm, _back;
        private int _selected;

        public string Name => "CharacterSelect";
        public CharacterSelectScene(GameContext ctx, Ui3D ui, GameMode mode) { _ctx = ctx; _ui = ui; _mode = mode; }

        public void Enter() {
            _selected = Math.Max(0, Math.Min(GameConst.CharCount - 1, _ctx.SelectedCharId));
            float pitch = 224f / 3f, rowH = pitch - 8f;
            for (int i = 0; i < 3; i++)
                _cards[i] = _ui.Button(12, 44 + i * pitch + 4, 150, rowH, "");
            _nickname = _ui.Input(300, 6, 166, 24, 16, 50, 50, 60, _ctx.SelectedNickname);
            _confirm = _ui.Button(352, 276, 116, 34, "开始游戏");
            _back = _ui.Button(12, 276, 96, 34, "返回");
            Bindings.R3dInputFocus(_nickname);
            Bindings.R3dInputStartTextInput(_ui.R, _nickname);
            Joy.Host.log("[scene] CharacterSelect mode=" + _mode);
        }

        public void Update(float dt) {
            float mx = Joy.Input.mouse_x(), my = Joy.Input.mouse_y();
            bool down = Joy.Input.mouse_button(1);

            if (Bindings.R3dInputIsHovered(_ui.R, _nickname, mx, my) && down)
                Bindings.R3dInputClick(_ui.R, _nickname, mx, my);

            for (int i = 0; i < 3; i++)
                if (_ui.Clicked(_cards[i], mx, my, down)) _selected = i;

            if (_ui.Clicked(_confirm, mx, my, down)) {
                _ctx.SelectedCharId = _selected;
                string nick = R3dExtPlus.InputGetText(_nickname).TrimStart(' ', '\t');
                _ctx.SelectedNickname = string.IsNullOrEmpty(nick) ? "玩家" : nick;
                if (_mode == GameMode.Client)
                    _ctx.Scenes.Change(new LoadingScene(_ctx, _ui, _mode));
                else
                    _ctx.Scenes.Change(new LevelSelectScene(_ctx, _ui, _mode));
            } else if (_ui.Clicked(_back, mx, my, down)) {
                _ctx.Scenes.Change(_mode == GameMode.Single
                    ? (IScene3D)new MainMenuScene(_ctx, _ui)
                    : new OnlineScene(_ctx, _ui));
            }
        }

        public void OnEvent(UserEvent e) { }
        public void Destroy() {
            for (int i = 0; i < 3; i++) Bindings.R3dButtonDestroy(_cards[i]);
            Bindings.R3dInputStopTextInput(_ui.R, _nickname);
            Bindings.R3dInputDestroy(_nickname);
            Bindings.R3dButtonDestroy(_confirm); Bindings.R3dButtonDestroy(_back);
        }

        public void Render() {
            _ui.BeginFrame();
            _ui.Rect(0, 0, 480, 320, 238, 234, 226);
            _ui.Rect(0, 0, 480, 36, 40, 40, 48);
            _ui.Text("选择角色", 16, 6, 24, 245, 243, 237);
            _ui.Text("昵称", 250, 9, 16, 245, 243, 237);
            _ui.DrawInput(_nickname);

            float pitch = 224f / 3f, rowH = pitch - 8f;
            byte[] fills = { 60, 120, 220 };
            for (int i = 0; i < 3; i++) {
                float y = 44 + i * pitch + 4;
                bool sel = i == _selected;
                _ui.Rect(12, y, 150, rowH, sel ? (byte)38 : (byte)176, sel ? (byte)38 : (byte)172, sel ? (byte)46 : (byte)164);
                _ui.Border(12, y, 150, rowH, 150, 146, 138);
                if (sel) _ui.Rect(14, y + 2, 4, rowH - 4, 200, 60, 50);
                _ui.Text(CharName(i), 24, y + (rowH - 18) * 0.5f, 18, sel ? (byte)245 : (byte)28, sel ? (byte)243 : (byte)28, sel ? (byte)237 : (byte)32);
                _ui.Rect(12 + 150 - 22, y + (rowH - 12) / 2, 12, 12, fills[i], (byte)(255 - fills[i]), 120);
            }

            _ui.Rect(172, 44, 296, 224, 250, 248, 242);
            _ui.Border(172, 44, 296, 224, 150, 146, 138);
            _ui.Text("角色预览", 184, 60, 22, 28, 28, 32);
            _ui.Text("选择后点击开始游戏", 184, 168, 16, 90, 90, 98);
            _ui.DrawButton(_confirm); _ui.DrawButton(_back);
            _ui.EndFrame();
        }

        private static string CharName(int i) { return i == 0 ? "战士" : i == 1 ? "射手" : "刺客"; }
    }
}
