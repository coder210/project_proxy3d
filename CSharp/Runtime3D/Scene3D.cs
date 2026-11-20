using System;
using Joy.Api;
using ProxyGame;

namespace JoyGame
{
    /// <summary>用户事件码（复刻 context.h:91-131）。</summary>
    public static class Events
    {
        public const int MENU = 0x2, ONLINE = 0x3, MODE = 0x4, SETTINGS = 0x5, TRANSITION = 0x6;
        public static class Menu { public const int Start = 1, Online = 2, Settings = 3, Exit = 4; }
        public static class Online { public const int Host = 1, Client = 2, Server = 3, Back = 4; }
        public static class Settings { public const int Back = 1; }
        public static class Transition { public const int SplashDone = 1, LoadingDone = 2; }
    }

    public enum GameMode { Single = 0, Host = 1, Client = 2, Server = 3 }

    /// <summary>场景接口（对齐 C++ Scene：Enter/Update/Render/OnEvent/Destroy）。</summary>
    public interface IScene3D
    {
        string Name { get; }
        void Enter();
        void Update(float dt);
        void Render();
        void OnEvent(UserEvent e);
        void Destroy();
    }

    /// <summary>需要 20Hz 固定步逻辑的场景实现此接口（Unity FixedUpdate 语义）。</summary>
    public interface IFixedUpdateScene
    {
        void FixedUpdate(float dt);
    }

    /// <summary>简化的用户事件（替代 SDL_Event 直达 UI）。UI 用轮询，场景间用此事件通信。</summary>
    public sealed class UserEvent
    {
        public int Type;
        public int Code;
        public UserEvent(int type, int code) { Type = type; Code = code; }
    }

    /// <summary>
    /// 跨场景共享的游戏上下文（替代 C++ Context 的表现/设置部分）。
    /// 持有 GameState、设置、选择结果与瘦宿主引用。
    /// </summary>
    public sealed class GameContext
    {
        public GameState State { get; } = new GameState();
        public SceneManager3D Scenes { get; set; }
        public GameMode Mode = GameMode.Single;

        // 设置（复刻 context.h Settings）
        public string ServerIp = "192.168.2.31";
        public int ServerPort = 30000;
        public float BgmVolume = 0.5f;
        public float SfxVolume = 1.0f;
        public bool Fullscreen = false;

        // 跨场景选择结果
        public int SelectedCharId = 0;
        public string SelectedNickname = "玩家";
        public int SelectedLevel = 1;

        // 战斗瞬时输入（替代 Context.current_input_mask / *_triggered）
        public byte InputMask;
        public bool AttackTriggered, BulletTriggered, RayTriggered;
        public bool ShowDebugHud = false;

        // 本帧真实渲染 dt（供相机平滑等表现层使用，逻辑仍走固定步）
        public float RenderDt = 1f / 60f;

        public void LoadSettings()
        {
            try {
                if (!System.IO.File.Exists("settings.cfg")) return;
                foreach (var raw in System.IO.File.ReadAllLines("settings.cfg")) {
                    var line = raw.Trim();
                    int eq = line.IndexOf('=');
                    if (eq < 0) continue;
                    string k = line.Substring(0, eq), v = line.Substring(eq + 1);
                    switch (k) {
                        case "server_ip": ServerIp = v; break;
                        case "server_port": int.TryParse(v, out ServerPort); break;
                        case "bgm_volume": float.TryParse(v, out BgmVolume); break;
                        case "sfx_volume": float.TryParse(v, out SfxVolume); break;
                        case "fullscreen": Fullscreen = v == "1"; break;
                    }
                }
            } catch { /* 设置文件损坏则用默认值 */ }
        }

        public void SaveSettings()
        {
            try {
                System.IO.File.WriteAllLines("settings.cfg", new[] {
                    "server_ip=" + ServerIp,
                    "server_port=" + ServerPort,
                    "bgm_volume=" + BgmVolume,
                    "sfx_volume=" + SfxVolume,
                    "fullscreen=" + (Fullscreen ? 1 : 0)
                });
            } catch { /* 只读目录则忽略 */ }
        }

        public void ClearInput()
        {
            AttackTriggered = BulletTriggered = RayTriggered = false;
        }
    }
}
