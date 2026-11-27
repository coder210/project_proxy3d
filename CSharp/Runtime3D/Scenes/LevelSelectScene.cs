using System;
using Joy.Api;
using ProxyGame;

namespace JoyGame
{
    /// <summary>
    /// 选关（C++ level_select_scene.cpp）：与选角页同一套版式——
    /// 顶部墨带 + 左侧列表(序号/名称/敌人数) + 右侧详情(迷你地图/名称/简介/敌人数) + 底部开始/返回。
    /// 逻辑坐标 480x320。
    /// </summary>
    internal sealed class LevelSelectScene : IScene3D
    {
        private const float ListX = 12f, ListY = 44f, ListW = 200f, ListH = 224f;
        private const float PanelX = 224f, PanelY = 44f, PanelW = 244f, PanelH = 224f;

        // 配色（复刻 C++ level_select_scene.cpp:21-31 的墨/纸风格）
        private const byte InkR = 40, InkG = 40, InkB = 48;
        private const byte PapR = 245, PapG = 243, PapB = 237;
        private const byte BgR = 238, BgG = 234, BgB = 226;
        private const byte PnlR = 250, PnlG = 248, PnlB = 242;
        private const byte BdrR = 150, BdrG = 146, BdrB = 138;
        private const byte SelR = 38, SelG = 38, SelB = 46;
        private const byte RowR = 176, RowG = 172, RowB = 164;
        private const byte HvR = 120, HvG = 120, HvB = 128;
        private const byte RedR = 200, RedG = 60, RedB = 50;
        private const byte WallR = 60, WallG = 60, WallB = 68;
        private const byte FlrR = 232, FlrG = 230, FlrB = 224;

        // LEVEL_META（复刻 C++ level_select_scene.cpp:53-59）
        private static readonly string[] LevelNames = { "训练场", "回廊", "军营", "绝地", "迷宫" };
        private static readonly string[] LevelDescs = {
            "开阔立柱，练手入门", "立柱渐多，新手进阶", "障碍密集，节奏加快",
            "高难绝地，背水一战", "之字回廊，步步深入"
        };

        private readonly GameContext _ctx;
        private readonly Ui3D _ui;
        private readonly GameMode _mode;
        private readonly Jr3dButton[] _levels = new Jr3dButton[MapTables.LevelCount];
        private readonly int[] _enemyCnt = new int[MapTables.LevelCount];
        private readonly bool[][] _miniWall = new bool[MapTables.LevelCount][];
        private Jr3dButton _confirm, _back;
        private int _selected, _hovered = -1;

        public string Name => "LevelSelect";
        public LevelSelectScene(GameContext ctx, Ui3D ui, GameMode mode) { _ctx = ctx; _ui = ui; _mode = mode; }

        public void Enter() {
            _selected = (_ctx.SelectedLevel >= 1 && _ctx.SelectedLevel <= MapTables.LevelCount)
                ? _ctx.SelectedLevel - 1 : 0;
            _hovered = -1;

            for (int i = 0; i < MapTables.LevelCount; ++i) {
                _enemyCnt[i] = MapData.BuildLayout(i + 1).EnemyN;
                _miniWall[i] = BuildMiniWall(i + 1);
            }

            float pitch = ListH / MapTables.LevelCount;
            float rowH = pitch - 6f;
            for (int i = 0; i < _levels.Length; ++i)
                _levels[i] = _ui.Button(ListX, ListY + i * pitch + 3f, ListW, rowH, "");
            _confirm = _ui.Button(356, 276, 112, 34, "开始");
            _back = _ui.Button(12, 276, 96, 34, "返回");
            Joy.Host.log("[scene] LevelSelect");
        }

        /// <summary>粗采样每关 ASCII 地图的 16x16 墙位图（grid_char(src, gy*2, gx*2)=='#'）。</summary>
        private static bool[] BuildMiniWall(int level) {
            var wall = new bool[16 * 16];
            string[] src = MapTables.Levels[level - 1];
            for (int gy = 0; gy < 16; ++gy)
                for (int gx = 0; gx < 16; ++gx)
                    wall[gy * 16 + gx] = src[gy * 2][gx * 2] == '#';
            return wall;
        }

        public void Update(float dt) {
            float mx = Joy.Input.mouse_x(), my = Joy.Input.mouse_y();
            bool down = Joy.Input.mouse_button(1);

            _hovered = -1;
            for (int i = 0; i < _levels.Length; ++i) {
                if (Bindings.R3dButtonIsHovered(_ui.R, _levels[i], mx, my)) _hovered = i;
                if (_ui.Clicked(_levels[i], mx, my, down)) _selected = i;
            }
            if (_ui.Clicked(_confirm, mx, my, down)) {
                _ctx.SelectedLevel = _selected + 1;
                _ctx.Scenes.Change(new LoadingScene(_ctx, _ui, _mode));
            } else if (_ui.Clicked(_back, mx, my, down)) {
                _ctx.Scenes.Change(new CharacterSelectScene(_ctx, _ui, _mode));
            }
        }

        public void OnEvent(UserEvent e) { }
        public void Destroy() {
            for (int i = 0; i < _levels.Length; ++i) Bindings.R3dButtonDestroy(_levels[i]);
            Bindings.R3dButtonDestroy(_confirm); Bindings.R3dButtonDestroy(_back);
        }

        private void DrawRow(int i) {
            float pitch = ListH / MapTables.LevelCount;
            float x = ListX, y = ListY + i * pitch + 3f, w = ListW, h = pitch - 6f;
            bool sel = i == _selected, hov = i == _hovered;

            byte br = sel ? SelR : (hov ? HvR : RowR);
            byte bg = sel ? SelG : (hov ? HvG : RowG);
            byte bb = sel ? SelB : (hov ? HvB : RowB);
            _ui.Rect(x, y, w, h, br, bg, bb);
            _ui.Border(x, y, w, h, BdrR, BdrG, BdrB, 2f);
            if (sel) _ui.Rect(x + 2f, y + 2f, 4f, h - 4f, RedR, RedG, RedB);

            string num = (i + 1).ToString("00");
            byte nr = sel ? PapR : (byte)130, ng = sel ? PapG : (byte)130, nb = sel ? PapB : (byte)138;
            _ui.Text(num, x + 12f, y + (h - 16f) * 0.5f, 14f, nr, ng, nb);

            byte tr = sel ? PapR : (byte)28, tg = sel ? PapG : (byte)28, tb = sel ? PapB : (byte)32;
            _ui.Text(LevelNames[i], x + 42f, y + (h - 18f) * 0.5f - 8f, 18f, tr, tg, tb);

            byte er = sel ? (byte)200 : (byte)110, eg = sel ? (byte)200 : (byte)110, eb = sel ? (byte)200 : (byte)118;
            _ui.Text("敌人 " + _enemyCnt[i], x + 42f, y + (h - 14f) * 0.5f + 8f, 12f, er, eg, eb);
        }

        private void DrawPreview(int index) {
            float px = PanelX, py = PanelY, pw = PanelW, ph = PanelH;
            _ui.Rect(px, py, pw, ph, PnlR, PnlG, PnlB);
            _ui.Border(px, py, pw, ph, BdrR, BdrG, BdrB, 2f);

            // 迷你地图 16x16 色块
            float mx = px + 12f, my = py + 12f, mw = pw - 24f, mh = 104f;
            float cell = mw / 16f;
            bool[] wall = _miniWall[index];
            for (int gy = 0; gy < 16; ++gy)
                for (int gx = 0; gx < 16; ++gx) {
                    bool w = wall[gy * 16 + gx];
                    _ui.Rect(mx + gx * cell, my + gy * cell, cell + 0.5f, cell + 0.5f,
                             w ? WallR : FlrR, w ? WallG : FlrG, w ? WallB : FlrB);
                }
            _ui.Border(mx, my, mw, mh, BdrR, BdrG, BdrB, 1.5f);

            _ui.Text(LevelNames[index], px + 12f, py + 126f, 20f, 28, 28, 32);
            _ui.Text(LevelDescs[index], px + 12f, py + 150f, 12f, 90, 90, 98);
            _ui.Text("敌人出生点  " + _enemyCnt[index], px + 12f, py + 172f, 13f, 90, 90, 98);
        }

        public void Render() {
            _ui.BeginFrame();
            _ui.Rect(0, 0, 480, 320, BgR, BgG, BgB);
            _ui.Rect(0, 0, 480, 36, InkR, InkG, InkB);
            _ui.Text("选择关卡", 16, 6, 24, PapR, PapG, PapB);

            int cid = (_ctx.SelectedCharId >= 0 && _ctx.SelectedCharId < GameConst.CharCount)
                ? _ctx.SelectedCharId : 0;
            string charName = cid == 0 ? "战士" : cid == 1 ? "射手" : "刺客";
            _ui.Text("当前角色：" + charName, 230, 9, 16, 200, 200, 208);

            for (int i = 0; i < _levels.Length; ++i) DrawRow(i);
            DrawPreview(_selected);

            _ui.DrawButton(_confirm); _ui.DrawButton(_back);
            _ui.EndFrame();
        }
    }
}
