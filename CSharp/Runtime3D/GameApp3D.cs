using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>
    /// C# 3D 应用外壳（对齐 joy examples/csharp 的 GameApp，但用 r3d）。
    /// 自建窗口 + r3d，持有 GameContext 与 SceneManager3D，驱动全部场景。
    /// 启动流程对齐 C++：Splash -> MainMenu（除非 --single 直接战斗）。
    /// </summary>
    internal sealed class GameApp3D
    {
        public Jwindow Window { get; }
        public Jr3d R3d { get; }
        public Ui3D Ui { get; }
        public GameContext Context { get; } = new GameContext();
        public SceneManager3D Scenes { get; }
        public int Frame { get; private set; }

        public GameApp3D(int width, int height) {
            // 资源根即当前目录，所有资源路径一律相对资源根
            Window = Bindings.CreateWindow("Project Proxy3D (C#)", width, height);
            if (Window == null) throw new InvalidOperationException("CreateWindow failed");
            R3d = R3dExt.R3dCreate(Window, "shaders");
            if (R3d == null) throw new InvalidOperationException("R3dCreate failed (GPU/shaders)");
            Bindings.R3dSetLogicalPresentationMode(R3d, width, height, 2);

            Ui = new Ui3D(R3d, Bindings.R3dMeshQuad(R3d));
            Context.Scenes = Scenes = new SceneManager3D(Context);
            Context.LoadSettings();

            string mode = Environment.GetEnvironmentVariable("PROXY_MODE");
            switch (mode) {
                case "host": Context.Mode = GameMode.Host; break;
                case "client": Context.Mode = GameMode.Client; break;
                case "server": Context.Mode = GameMode.Server; break;
                default: Context.Mode = GameMode.Single; break;
            }
        }

        public void Start() {
            // 与 C++ 启动一致：闪屏 -> 主菜单；SERVER 模式直接进加载->战斗。
            if (Context.Mode == GameMode.Server)
                Scenes.Change(new LoadingScene(Context, Ui, GameMode.Server));
            else
                Scenes.Change(new SplashScene(Context, Ui));
            Joy.Host.log("[cs3d] started scene=" + Scenes.CurrentName);
        }

        public void Tick(float dt) {
            if (dt < 0f) dt = 0f;
            if (dt > 0.1f) dt = 0.1f;
            Frame++;
            Context.RenderDt = dt;
            Scenes.Update(dt);
            Scenes.Render();
        }
    }
}
