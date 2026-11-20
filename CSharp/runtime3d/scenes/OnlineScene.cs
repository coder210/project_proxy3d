using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>联机大厅（C++ online_scene.cpp）：创建/加入/服务端/返回 + 直连弹窗。</summary>
    internal sealed class OnlineScene : IScene3D
    {
        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private Jr3dButton _create, _join, _server, _back, _direct, _cancel, _connect;
        private Jr3dInput _ip, _port;
        private bool _directOpen;

        public string Name => "Online";
        public OnlineScene(GameContext ctx, Ui3D ui) { _ctx = ctx; _ui = ui; }

        public void Enter() {
            _directOpen = false;
            _create = _ui.Button(20, 280, 108, 30, "创建房间");
            _join = _ui.Button(134, 280, 108, 30, "加入房间");
            _server = _ui.Button(248, 280, 108, 30, "服务端");
            _back = _ui.Button(362, 280, 108, 30, "返回");
            _direct = _ui.Button(400, 6, 68, 24, "直连");
            _ip = _ui.Input(150, 128, 220, 28, 16, 50, 50, 60, _ctx.ServerIp);
            _port = _ui.Input(150, 166, 110, 28, 16, 50, 50, 60, _ctx.ServerPort.ToString());
            _cancel = _ui.Button(196, 212, 90, 26, "取消");
            _connect = _ui.Button(316, 212, 90, 26, "连接");
            Joy.Host.log("[scene] Online");
        }

        public void Update(float dt) {
            float mx = Joy.Input.mouse_x(), my = Joy.Input.mouse_y();
            bool down = Joy.Input.mouse_button(1);

            if (_directOpen) {
                if (Bindings.R3dInputIsHovered(_ui.R, _ip, mx, my) && down) Bindings.R3dInputClick(_ui.R, _ip, mx, my);
                if (Bindings.R3dInputIsHovered(_ui.R, _port, mx, my) && down) Bindings.R3dInputClick(_ui.R, _port, mx, my);
                if (_ui.Clicked(_cancel, mx, my, down)) _directOpen = false;
                if (_ui.Clicked(_connect, mx, my, down)) DirectConnect();
                return;
            }

            if (_ui.Clicked(_create, mx, my, down)) _ctx.Scenes.Change(new CharacterSelectScene(_ctx, _ui, GameMode.Host));
            else if (_ui.Clicked(_join, mx, my, down)) _ctx.Scenes.Change(new CharacterSelectScene(_ctx, _ui, GameMode.Client));
            else if (_ui.Clicked(_server, mx, my, down)) _ctx.Scenes.Change(new LoadingScene(_ctx, _ui, GameMode.Server));
            else if (_ui.Clicked(_back, mx, my, down)) _ctx.Scenes.Change(new MainMenuScene(_ctx, _ui));
            else if (_ui.Clicked(_direct, mx, my, down)) {
                _directOpen = true;
                Bindings.R3dInputFocus(_ip);
                Bindings.R3dInputStartTextInput(_ui.R, _ip);
            }
        }

        private void DirectConnect() {
            string ip = R3dExtPlus.InputGetText(_ip);
            int port;
            if (string.IsNullOrEmpty(ip) || !int.TryParse(R3dExtPlus.InputGetText(_port), out port) || port < 1 || port > 65535) {
                Joy.Host.log("[online] invalid ip/port");
                return;
            }
            _ctx.ServerIp = ip;
            _ctx.ServerPort = port;
            _ctx.SaveSettings();
            _directOpen = false;
            Bindings.R3dInputStopTextInput(_ui.R, _ip);
            _ctx.Scenes.Change(new CharacterSelectScene(_ctx, _ui, GameMode.Client));
        }

        public void OnEvent(UserEvent e) { }
        public void Destroy() {
            Bindings.R3dButtonDestroy(_create); Bindings.R3dButtonDestroy(_join);
            Bindings.R3dButtonDestroy(_server); Bindings.R3dButtonDestroy(_back);
            Bindings.R3dButtonDestroy(_direct); Bindings.R3dButtonDestroy(_cancel);
            Bindings.R3dButtonDestroy(_connect);
            Bindings.R3dInputStopTextInput(_ui.R, _ip);
            Bindings.R3dInputDestroy(_ip); Bindings.R3dInputDestroy(_port);
        }

        public void Render() {
            _ui.BeginFrame();
            _ui.Rect(0, 0, 480, 320, 238, 234, 226);
            _ui.Rect(0, 0, 480, 36, 40, 40, 48);
            _ui.Text("联机大厅", 16, 6, 24, 245, 243, 237);
            _ui.Text("目标  " + _ctx.ServerIp + ":" + _ctx.ServerPort, 200, 50, 16, 40, 40, 48);
            _ui.Text("暂无房间", 12, 60, 16, 90, 90, 98);
            _ui.Text("用右上角『直连』加入", 12, 84, 14, 110, 110, 118);
            _ui.DrawButton(_create); _ui.DrawButton(_join);
            _ui.DrawButton(_server); _ui.DrawButton(_back); _ui.DrawButton(_direct);
            if (_directOpen) {
                _ui.Rect(90, 85, 300, 150, 250, 248, 242);
                _ui.Border(90, 85, 300, 150, 150, 146, 138);
                _ui.Text("直连服务器", 110, 95, 20, 40, 40, 48);
                _ui.Text("IP 地址", 110, 130, 16, 50, 50, 60);
                _ui.Text("端口", 110, 168, 16, 50, 50, 60);
                _ui.DrawInput(_ip); _ui.DrawInput(_port);
                _ui.DrawButton(_cancel); _ui.DrawButton(_connect);
            }
            _ui.EndFrame();
        }
    }
}
