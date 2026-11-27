using System;

namespace ProxyGame
{
    // 坐标常量（pathfinding_component.h / procedural_map.h）
    public static class MapConst
    {
        public const int GridN = 32;
        public const int GridMin = -16;
        public const int GridOffset = 16;   // -GridMin
        public const int Cells = GridN * GridN;
        public const int MaxObs = 1024;
        public const int EnemySpawnCount = 2;
        public const int WaveMaxEnemies = 64;
        public static readonly int[] LevelEnemyCount = { 8, 8, 8, 8, 8 };
    }

    /// <summary>复刻 PmObstacle（procedural_map.h）。</summary>
    public struct PmObstacle
    {
        public int X10, Z10;    // 中心坐标（十分之一米）
        public int Hx10, Hz10;  // XZ 半尺寸
        public int Sy10;        // 高度
        public int Gx0, Gz0;    // 占用格左下角
        public int Gw, Gh;      // 占用格宽深
    }

    /// <summary>复刻 PmLayout（障碍表 + 可通行栅格）。</summary>
    public sealed class PmLayout
    {
        public PmObstacle[] Obs = new PmObstacle[MapConst.MaxObs];
        public int Count;
        public int Seed;
        public bool[] Walkable = new bool[MapConst.Cells];
    }

    /// <summary>level_build_layout 的解析结果（障碍 + 出生格）。</summary>
    public sealed class LevelLayout
    {
        public PmLayout Layout;
        public int PlayerCell = -1;
        public int[] EnemyCells = new int[MapConst.WaveMaxEnemies];
        public int EnemyN;
        public int ExitCell = -1;
        public bool Valid;
    }

    /// <summary>
    /// 逐位复刻 shared/systems/level_maps.h 的 level_build_layout：
    /// 扫描 ASCII → 记录墙位图/P/E → 贪心矩形合并（行主序）→ 障碍以十分之一米整数存储
    /// → 回填 walkable。纯函数确定性，双端同序。
    /// </summary>
    public static class MapData
    {
        private const int Rows = MapTables.Rows, Cols = MapTables.Cols;

        public static LevelLayout BuildLayout(int level, bool wantExit = false)
        {
            var res = new LevelLayout();
            res.PlayerCell = -1;
            res.EnemyN = 0;
            res.ExitCell = -1;
            if (level < 1 || level > MapTables.LevelCount) return res;

            string[] src = MapTables.Levels[level - 1];
            var L = new PmLayout { Count = 0, Seed = level };
            var wall = new bool[Rows, Cols];

            int r = 0, c = 0;
            for (int p = 0; p < src.Length && r < Rows; ++p)
            {
                string line = src[p];
                for (int ci = 0; ci < line.Length; ++ci)
                {
                    char ch = line[ci];
                    if (c >= Cols) { c++; continue; }
                    if (ch == 'P') {
                        res.PlayerCell = r * MapConst.GridN + c;
                    } else if (ch == 'X') {
                        res.ExitCell = r * MapConst.GridN + c;
                    } else if (ch == 'E') {
                        if (res.EnemyN < MapConst.WaveMaxEnemies)
                            res.EnemyCells[res.EnemyN++] = r * MapConst.GridN + c;
                    } else if (ch == '#') {
                        wall[r, c] = true;
                    }
                    c++;
                }
                r++; c = 0;
            }

            // 贪心矩形合并：水平到底 → 垂直扩展（整宽列全墙）
            for (r = 0; r < Rows && L.Count < MapConst.MaxObs; ++r)
            {
                for (c = 0; c < Cols; ++c)
                {
                    if (!wall[r, c]) continue;
                    int w = 1;
                    while (c + w < Cols && wall[r, c + w]) w++;
                    int h = 1;
                    while (r + h < Rows)
                    {
                        bool full = true;
                        for (int cc = c; cc < c + w; ++cc)
                            if (!wall[r + h, cc]) { full = false; break; }
                        if (!full) break;
                        h++;
                    }
                    for (int dr = 0; dr < h; ++dr)
                        for (int dc = 0; dc < w; ++dc)
                            wall[r + dr, c + dc] = false;

                    var o = new PmObstacle {
                        Gx0 = c, Gz0 = r, Gw = w, Gh = h,
                        X10 = (2 * c + w - 2 * MapConst.GridOffset) * 5,
                        Z10 = (2 * r + h - 2 * MapConst.GridOffset) * 5,
                        Hx10 = w * 5,
                        Hz10 = h * 5,
                        Sy10 = 14
                    };
                    L.Obs[L.Count++] = o;
                }
            }

            // walkable 回填（镜像 pf_build_walkable）
            for (int i = 0; i < MapConst.Cells; ++i) L.Walkable[i] = true;
            for (int k = 0; k < L.Count; ++k)
            {
                var o = L.Obs[k];
                if (o.Gx0 < 0 || o.Gz0 < 0 || o.Gx0 + o.Gw > MapConst.GridN || o.Gz0 + o.Gh > MapConst.GridN)
                    continue;
                for (int dr = 0; dr < o.Gh; ++dr)
                    for (int dc = 0; dc < o.Gw; ++dc)
                        L.Walkable[(o.Gz0 + dr) * MapConst.GridN + (o.Gx0 + dc)] = false;
            }

            res.Layout = L;
            res.Valid = res.PlayerCell >= 0 && res.EnemyN > 0;
            return res;
        }

        /// <summary>格子坐标 → 格子中心世界坐标（fp），对应 pf_grid_to_world。</summary>
        public static long GridToWorld(int g) => Fp.Add(Fp.FromInt(g - MapConst.GridOffset), Fp.Div(Fp.FromInt(1), Fp.FromInt(2)));

        /// <summary>玩家出生点世界坐标（level_player_spawn）。</summary>
        public static void PlayerSpawn(int level, out long fx, out long fz)
        {
            var ll = BuildLayout(level);
            if (ll.PlayerCell < 0) { fx = 0; fz = 0; return; }
            fx = GridToWorld(ll.PlayerCell % MapConst.GridN);
            fz = GridToWorld(ll.PlayerCell / MapConst.GridN);
        }
    }
}
