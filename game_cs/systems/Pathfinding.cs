using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// A* 寻路（逐位复刻 shared/systems/pathfinding_system.h）。
    /// 全整数运算：曼哈顿启发 + 4 方向 + O(log N) 二叉堆，帧同步确定性。
    /// 含静态障碍 → 网格可通行性（0.6m 外扩 pad）的构建。
    /// </summary>
    public static class Pathfinding
    {
        public const int GridN = MapConst.GridN;         // 64
        public const int Cells = MapConst.Cells;         // 4096
        private const int Shift = 32;                    // Q32
        private static readonly long PF_PAD = Fp.Div(Fp.FromInt(3), Fp.FromInt(5)); // 0.6m
        public static readonly long PF_STEP = Fp.Div(Fp.FromInt(1), Fp.FromInt(12));
        public const int RecomputeInterval = 10;

        /// <summary>世界坐标 → 格子坐标（fp 算术右移即 floor）。</summary>
        public static int WorldToGrid(long v) => (int)((v >> Shift) + MapConst.GridOffset);

        /// <summary>格子坐标 → 格子中心世界坐标。</summary>
        public static long GridToWorld(int g) => MapData.GridToWorld(g);

        /// <summary>
        /// 由静态障碍构建可通行栅格（复刻 pf_build_walkable）：
        /// 每个障碍的 XZ 包围盒外扩 PF_PAD，覆盖到的格子标记不可通行。
        /// 障碍全部为 BOX（spawn_obstacles_from_layout 只产 BOX），矩形坐标与 C++ 逐位一致。
        /// </summary>
        public static bool[] BuildWalkable(GameState state)
        {
            var walkable = new bool[Cells];
            for (int i = 0; i < Cells; ++i) walkable[i] = true;

            float pad = Fp.ToFloat(PF_PAD);
            for (int k = 0; k < state.ObstacleCount; ++k)
            {
                var o = state.Obstacles[k];
                float cx = Fp.ToFloat(o.X);
                float cz = Fp.ToFloat(o.Z);
                float hx = Fp.ToFloat(o.Hx);
                float hz = Fp.ToFloat(o.Hz);

                int gx0 = (int)Math.Floor(cx - hx - pad + MapConst.GridOffset);
                int gx1 = (int)Math.Floor(cx + hx + pad + MapConst.GridOffset);
                int gz0 = (int)Math.Floor(cz - hz - pad + MapConst.GridOffset);
                int gz1 = (int)Math.Floor(cz + hz + pad + MapConst.GridOffset);
                if (gx0 < 0) gx0 = 0;
                if (gx1 > GridN - 1) gx1 = GridN - 1;
                if (gz0 < 0) gz0 = 0;
                if (gz1 > GridN - 1) gz1 = GridN - 1;
                for (int gz = gz0; gz <= gz1; ++gz)
                    for (int gx = gx0; gx <= gx1; ++gx)
                        walkable[gz * GridN + gx] = false;
            }
            return walkable;
        }

        /// <summary>由关卡布局生成障碍（复刻 spawn_obstacles_from_layout 的坐标部分）。</summary>
        public static void BuildObstaclesFromLayout(GameState state, PmLayout layout, ref int nextId)
        {
            state.ObstacleCount = 0;
            for (int k = 0; k < layout.Count && k < MapConst.MaxObs; ++k)
            {
                var o = layout.Obs[k];
                long fx = Fp.Div(Fp.FromInt(o.X10), Fp.FromInt(10));
                long fz = Fp.Div(Fp.FromInt(o.Z10), Fp.FromInt(10));
                long fhx = Fp.Div(Fp.FromInt(o.Hx10), Fp.FromInt(10));
                long fhz = Fp.Div(Fp.FromInt(o.Hz10), Fp.FromInt(10));
                long fsy = Fp.Div(Fp.FromInt(o.Sy10), Fp.FromInt(10));
                long fy = Fp.Div(fsy, Fp.FromInt(2));
                state.Obstacles[state.ObstacleCount++] = new Obstacle {
                    Id = nextId++, X = fx, Y = fy, Z = fz,
                    Hx = fhx, Hz = fhz, Sy = fsy,
                    Gx0 = o.Gx0, Gz0 = o.Gz0, Gw = o.Gw, Gh = o.Gh
                };
            }
        }

        private static int NearestWalkable(bool[] walkable, int cx, int cz)
        {
            for (int r = 0; r < GridN; ++r)
            {
                for (int dz = -r; dz <= r; ++dz)
                {
                    for (int dx = -r; dx <= r; ++dx)
                    {
                        int gx = cx + dx, gz = cz + dz;
                        if (gx < 0 || gz < 0 || gx >= GridN || gz >= GridN) continue;
                        if (walkable[gz * GridN + gx]) return gz * GridN + gx;
                    }
                }
            }
            return -1;
        }

        /// <summary>
        /// 二叉堆 A*：起点/终点格 → 路径格索引序列（不含起点含终点）。
        /// outPath 容量需 >= 4096。返回路径长度。
        /// </summary>
        public static int AStar(bool[] walkable, int sx, int sz, int tx, int tz,
                                int[] outPath, int maxOut,
                                int[] gscore, int[] fscore, int[] came, bool[] closed, bool[] inOpen, int[] heap)
        {
            const int N = Cells;
            if (sx < 0) sx = 0; else if (sx >= GridN) sx = GridN - 1;
            if (sz < 0) sz = 0; else if (sz >= GridN) sz = GridN - 1;
            if (tx < 0) tx = 0; else if (tx >= GridN) tx = GridN - 1;
            if (tz < 0) tz = 0; else if (tz >= GridN) tz = GridN - 1;

            int start = sz * GridN + sx;
            int goal = tz * GridN + tx;

            if (!walkable[start]) {
                int ns = NearestWalkable(walkable, sx, sz);
                if (ns < 0) return 0;
                start = ns; sx = ns % GridN; sz = ns / GridN;
            }
            if (!walkable[goal]) {
                int ng = NearestWalkable(walkable, tx, tz);
                if (ng < 0) return 0;
                goal = ng; tx = ng % GridN; tz = ng / GridN;
            }

            int heapLen = 0;
            for (int i = 0; i < N; ++i) {
                gscore[i] = 0x3fffffff;
                fscore[i] = 0x3fffffff;
                came[i] = -1;
                closed[i] = false;
                inOpen[i] = false;
            }

            gscore[start] = 0;
            fscore[start] = (sx > tx ? sx - tx : tx - sx) + (sz > tz ? sz - tz : tz - sz);

            heap[heapLen++] = start;
            inOpen[start] = true;

            int[][] dirs = { new[]{1,0}, new[]{-1,0}, new[]{0,1}, new[]{0,-1} };

            while (heapLen > 0)
            {
                int cur = heap[0];
                inOpen[cur] = false;
                heap[0] = heap[--heapLen];
                if (heapLen > 0) HeapDown(heap, fscore, heapLen, 0);

                if (cur == goal)
                {
                    int len = 0;
                    int node = cur;
                    while (node != start && node >= 0) {
                        if (len < maxOut) outPath[len++] = node;
                        node = came[node];
                    }
                    for (int i = 0, j = len - 1; i < j; ++i, --j) {
                        int t = outPath[i]; outPath[i] = outPath[j]; outPath[j] = t;
                    }
                    return len;
                }

                closed[cur] = true;
                int cx = cur % GridN, cy = cur / GridN;
                for (int d = 0; d < 4; ++d)
                {
                    int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
                    if (nx < 0 || ny < 0 || nx >= GridN || ny >= GridN) continue;
                    int ni = ny * GridN + nx;
                    if (!walkable[ni]) continue;
                    if (closed[ni]) continue;

                    int ng = gscore[cur] + 1;
                    if (ng < gscore[ni]) {
                        gscore[ni] = ng;
                        fscore[ni] = ng + (nx > tx ? nx - tx : tx - nx) + (ny > tz ? ny - tz : tz - ny);
                        came[ni] = cur;
                        if (!inOpen[ni]) {
                            inOpen[ni] = true;
                            heap[heapLen++] = ni;
                            HeapUp(heap, fscore, heapLen - 1);
                        } else {
                            for (int i = 0; i < heapLen; ++i)
                                if (heap[i] == ni) { HeapUp(heap, fscore, i); break; }
                        }
                    }
                }
            }
            return 0;
        }

        private static void HeapUp(int[] heap, int[] fscore, int i)
        {
            while (i > 0) {
                int p = (i - 1) >> 1;
                if (fscore[heap[p]] <= fscore[heap[i]]) break;
                int t = heap[i]; heap[i] = heap[p]; heap[p] = t;
                i = p;
            }
        }

        private static void HeapDown(int[] heap, int[] fscore, int heapLen, int i)
        {
            while (true) {
                int l = i * 2 + 1, r = i * 2 + 2, m = i;
                if (l < heapLen && fscore[heap[l]] < fscore[heap[m]]) m = l;
                if (r < heapLen && fscore[heap[r]] < fscore[heap[m]]) m = r;
                if (m == i) break;
                int t = heap[i]; heap[i] = heap[m]; heap[m] = t;
                i = m;
            }
        }

        /// <summary>为一次性调用分配工作区并执行 A*（parity/调试用）。</summary>
        public static int FindPath(bool[] walkable, int sx, int sz, int tx, int tz, out int[] path)
        {
            path = new int[Cells];
            var g = new int[Cells];
            var f = new int[Cells];
            var came = new int[Cells];
            var closed = new bool[Cells];
            var inOpen = new bool[Cells];
            var heap = new int[Cells];
            int len = AStar(walkable, sx, sz, tx, tz, path, Cells, g, f, came, closed, inOpen, heap);
            return len;
        }
    }
}
