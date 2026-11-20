using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>设置页（C++ settings_scene.cpp）：IP/端口/音量/全屏。</summary>
    internal sealed class SettingsScene : IScene3D
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly UiParticles _particles;
        private float _elapsed;

        private Jr3dComboboxInput _ip;
        private Jr3dInput _port;
        private Jr3dSlider _bgm, _sfx;
        private Jr3dCheckbox _fullscreen;
        private Jr3dButton _back;

        public string Name => "Settings";
        public SettingsScene(GameContext ctx, Ui3D ui) { _ctx = ctx; _ui = ui; _particles = new UiParticles(); }

        public void Enter() {
            _elapsed = 0f;
            _ip = _ui.Combobox(120, 50, 200, 24, 16, 50, 50, 60, _ctx.ServerIp);
            // 本地 IPv4（跳过 IPv6）；简化为回环 + 已保存地址
            Bindings.R3dComboboxInputAddItem(_ip, "127.0.0.1");
            if (!string.IsNullOrEmpty(_ctx.ServerIp) && _ctx.ServerIp != "127.0.0.1")
                Bindings.R3dComboboxInputAddItem(_ip, _ctx.ServerIp);
            _port = _ui.Input(120, 90, 100, 24, 16, 50, 50, 60, _ctx.ServerPort.ToString());
            _bgm = _ui.Slider(120, 130, 200, 14, 0f, 1f, _ctx.BgmVolume);
            _sfx = _ui.Slider(120, 170, 200, 14, 0f, 1f, _ctx.SfxVolume);
            R3dExtPlus.SliderSetColors(_bgm, 200,200,200,255, 50,50,60,255, 80,80,80,255, 100,100,100,255);
            R3dExtPlus.SliderSetColors(_sfx, 200,200,200,255, 50,50,60,255, 80,80,80,255, 100,100,100,255);
            _fullscreen = _ui.Checkbox(120, 212, 20, "全屏");
            R3dExtPlus.CheckboxSetState(_fullscreen, _ctx.Fullscreen ? 1 : 0);
            _back = _ui.Button(180, 260, 100, 40, "返回");
            Joy.Host.log("[scene] Settings");
        }

        public void Update(float dt) {
            _elapsed += dt;
            _particles.Update(dt);
            float mx = Joy.Input.mouse_x(), my = Joy.Input.mouse_y();
            bool down = Joy.Input.mouse_button(1);

            // 端口输入框点击聚焦 + 文本输入（键盘边沿）
            if (Bindings.R3dInputIsHovered(_uiR(), _port, mx, my) && down)
                Bindings.R3dInputClick(_uiR(), _port, mx, my);
            HandleTextInput(_port, down);

            // 滑块拖动
            if (Bindings.R3dSliderIsPressed(_uiR(), _bgm, mx, my)) Bindings.R3dSliderDragMove(_uiR(), _bgm, mx, my);
            if (Bindings.R3dSliderIsPressed(_uiR(), _sfx, mx, my)) Bindings.R3dSliderDragMove(_uiR(), _sfx, mx, my);
            if (Bindings.R3dCheckboxIsPressed(_uiR(), _fullscreen, mx, my) && down)
                R3dExtPlus.CheckboxSetState(_fullscreen, 1 - R3dExtPlus.CheckboxGetState(_fullscreen));

            SyncSettings();
            if (_ui.Clicked(_back, mx, my, down)) _ctx.Scenes.Change(new MainMenuScene(_ctx, _ui));
        }

        private Jr3d _uiR() => _ui.R;

        private void HandleTextInput(Jr3dInput inp, bool mouseDown) {
            // 逐字符键盘边沿输入（简化：只处理数字与退格）
            for (int k = Keys.Digit0; k <= Keys.Digit9; k++)
                if (Joy.Input.key_down(k)) { /* SDL 文本输入已由 csx 桥处理；此处不重复 */ }
            if (Joy.Input.key_down(Keys.Backspace)) Bindings.R3dInputBackspace(inp);
        }

        private void SyncSettings() {
            string ip = R3dExtPlus.ComboboxInputGetText(_ip);
            if (!string.IsNullOrEmpty(ip)) _ctx.ServerIp = ip;
            int port;
            if (int.TryParse(R3dExtPlus.InputGetText(_port), out port) && port >= 1 && port <= 65535)
                _ctx.ServerPort = port;
            _ctx.BgmVolume = Bindings.R3dSliderGetValue(_bgm);
            _ctx.SfxVolume = Bindings.R3dSliderGetValue(_sfx);
            _ctx.Fullscreen = R3dExtPlus.CheckboxGetState(_fullscreen) == 1;
            _ctx.SaveSettings();
        }

        public void OnEvent(UserEvent e) { }
        public void Destroy() {
            Bindings.R3dComboboxInputDestroy(_ip); Bindings.R3dInputDestroy(_port);
            Bindings.R3dSliderDestroy(_bgm); Bindings.R3dSliderDestroy(_sfx);
            Bindings.R3dCheckboxDestroy(_fullscreen); Bindings.R3dButtonDestroy(_back);
        }

        public void Render() {
            _ui.BeginFrame();
            _particles.Draw(_ui);
            _ui.Text("设置", 216, 12, 28, 30, 30, 40);
            _ui.Text("服务器IP", 20, 52, 16, 50, 50, 60);
            _ui.Text("端口", 20, 92, 16, 50, 50, 60);
            _ui.Text("音乐", 20, 132, 16, 50, 50, 60);
            _ui.Text("音效", 20, 172, 16, 50, 50, 60);
            _ui.Text((int)(_ctx.BgmVolume * 100) + "%", 330, 132, 16, 50, 50, 60);
            _ui.Text((int)(_ctx.SfxVolume * 100) + "%", 330, 172, 16, 50, 50, 60);
            _ui.DrawCombobox(_ip); _ui.DrawInput(_port);
            _ui.DrawSlider(_bgm); _ui.DrawSlider(_sfx);
            _ui.DrawCheckbox(_fullscreen); _ui.DrawButton(_back);
            _ui.EndFrame();
        }
    }
}
