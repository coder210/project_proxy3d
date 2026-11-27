using System;
using Joy.Api;
using ProxyGame;

namespace JoyGame
{
    /// <summary>
    /// 战斗场景 3D 渲染器（C# 版）。逐 pass 复刻 C++ render3d_pass 的绘制顺序与数值：
    /// camera -> floor -> obstacles -> entities(玩家/敌人) -> bullets -> HUD。
    /// 只用已开放的 r3d 绑定；不触碰 ECS/网络，数据源是 ProxyGame.GameState。
    /// </summary>
    internal sealed class Renderer3D
    {
        private readonly Jr3d _r;
        private readonly int _w, _h;

        // 共享 mesh（一次创建，复用）
        private readonly RenderMesh _meshFloor;
        private readonly RenderMesh _meshFloorWire;
        private readonly RenderMesh _meshCube;
        private readonly RenderMesh _meshCubeWire;
        // 实体尺寸（对齐 C++ server.cpp/client.cpp 的 mesh 尺寸 = 碰撞体尺寸）
        private const float PlayerW = 0.6f, PlayerH = 1.2f, PlayerD = 0.6f;
        private const float EnemyW = 0.5f, EnemyH = 1.0f, EnemyD = 0.5f;
        private readonly RenderMesh _meshSphere;
        private readonly RenderMesh _meshQuad;

        // 相机平滑跟随目标（对齐 C++ camera_pass 的 camera_target）
        private float _camX, _camZ;
        private bool _camInit;

        // 固定步前一 tick 的位置，用于渲染插值，消除 20Hz 逻辑带来的阶梯抖动
        private readonly float[] _prevPX = new float[GameConst.MaxPlayers];
        private readonly float[] _prevPY = new float[GameConst.MaxPlayers];
        private readonly float[] _prevPZ = new float[GameConst.MaxPlayers];
        private readonly float[] _prevEX = new float[GameConst.MaxEnemies];
        private readonly float[] _prevEY = new float[GameConst.MaxEnemies];
        private readonly float[] _prevEZ = new float[GameConst.MaxEnemies];
        private readonly float[] _prevBX = new float[GameConst.MaxBullets];
        private readonly float[] _prevBY = new float[GameConst.MaxBullets];
        private readonly float[] _prevBZ = new float[GameConst.MaxBullets];

        // 相机常量：比原 C++ 更近、更倾斜，使实体能看出长方体立体感
        // 地图 32×32（原 64×64），相机距离减半以保持相同视野占比
        private const float CamDist = 10.0f;
        private const float CamPitch = 55.0f * 3.14159265f / 180.0f;

        public Renderer3D(Jr3d r, int w, int h)
        {
            _r = r;
            _w = w;
            _h = h;

            // 地板 = 32x32 大平面（对齐地图尺寸）；线框稍小避免与填充完全共面
            _meshFloor = Bindings.R3dMeshPlane(r, 32f, 32f, 1);
            _meshFloorWire = Bindings.R3dMeshPlaneWire(r, 32f, 32f, 1);
            // 实体用完整 cube（6 面实心，对齐 C++ r3d_mesh_cube）+ 缩放，避免分面 mesh 缺面导致空心
            _meshCube = Bindings.R3dMeshCube(r, 1f, 1f, 1f);
            _meshCubeWire = Bindings.R3dMeshCubeWire(r, 1f, 1f, 1f);
            _meshSphere = Bindings.R3dMeshSphere(r, 0.5f, 12, 8);
            _meshQuad = Bindings.R3dMeshQuad(r);

            Bindings.R3dFontInit(r, "fonts/simhei.ttf", 24);
        }

        // C 侧 read_mat44 按 joy_mat44_t 字段声明顺序读 float[16]：
        // p[0..3]=m0,m4,m8,m12; p[4..7]=m1,m5,m9,m13; ...
        // 所以平移必须放在 p[3]/p[7]/p[11]，不能放在常规 column-major 内存布局的尾三项。
        private static float[] Trs(float sx, float sy, float sz, float tx, float ty, float tz)
        {
            return new float[16] {
                sx, 0f, 0f, tx,
                0f, sy, 0f, ty,
                0f, 0f, sz, tz,
                0f, 0f, 0f, 1f
            };
        }

        public void Snapshot(GameState state)
        {
            for (int i = 0; i < GameConst.MaxPlayers; i++) {
                _prevPX[i] = Fp.ToFloat(state.Players[i].X);
                _prevPY[i] = Fp.ToFloat(state.Players[i].Y);
                _prevPZ[i] = Fp.ToFloat(state.Players[i].Z);
            }
            for (int i = 0; i < GameConst.MaxEnemies; i++) {
                _prevEX[i] = Fp.ToFloat(state.Enemies[i].X);
                _prevEY[i] = Fp.ToFloat(state.Enemies[i].Y);
                _prevEZ[i] = Fp.ToFloat(state.Enemies[i].Z);
            }
            for (int i = 0; i < GameConst.MaxBullets; i++) {
                _prevBX[i] = Fp.ToFloat(state.Bullets[i].X);
                _prevBY[i] = Fp.ToFloat(state.Bullets[i].Y);
                _prevBZ[i] = Fp.ToFloat(state.Bullets[i].Z);
            }
        }

        private static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        public void RenderFrame(GameState state, float dt, float alpha,
            bool showDebugHud = false, bool showCollisionBoxes = false,
            VirtualControls virtualControls = null)
        {
            CameraPass(state, dt, alpha);

            if (!Bindings.R3dBegin(_r)) return;

            FloorPass();
            ObstaclePass(state);
            EntitiesPass(state, alpha);
            BulletPass(state, alpha);
            if (showCollisionBoxes) CollisionDebugPass(state);
            HudPass(state, dt);
            if (showDebugHud) DebugHudPass(state, dt);
            if (virtualControls != null) virtualControls.Draw(_r, _meshQuad);

            Bindings.R3dEnd(_r);
        }

        // ---- camera_pass：白底清屏 + 俯视 75° 相机，平滑跟随本地玩家 ----
        private void CameraPass(GameState state, float dt, float alpha)
        {
            Bindings.R3dSetClearColor(_r, 255, 255, 255);

            float targetX = 0f, targetY = 0f, targetZ = 0f;
            int follow = state.CameraFollowConv;
            for (int i = 0; follow >= 0 && i < GameConst.MaxPlayers; i++)
            {
                var p = state.Players[i];
                if (p.Alive && p.Conv == follow)
                {
                    targetX = Lerp(_prevPX[i], Fp.ToFloat(p.X), alpha);
                    targetY = Lerp(_prevPY[i], Fp.ToFloat(p.Y), alpha);
                    targetZ = Lerp(_prevPZ[i], Fp.ToFloat(p.Z), alpha);
                    break;
                }
            }

            float smooth = dt * 10f;
            if (smooth > 1f) smooth = 1f;
            if (!_camInit) { _camX = targetX; _camZ = targetZ; _camInit = true; }
            else { _camX += (targetX - _camX) * smooth; _camZ += (targetZ - _camZ) * smooth; }

            float cp = (float)Math.Cos(CamPitch), sp = (float)Math.Sin(CamPitch);
            float eyeX = _camX, eyeY = targetY + sp * CamDist, eyeZ = _camZ - cp * CamDist;
            float aimY = targetY + 1f;
            Bindings.R3dCameraLookat(_r,
                eyeX, eyeY, eyeZ,
                _camX, aimY, _camZ,
                0f, 1f, 0f, 60f);
        }

        // ---- floor_pass：浅灰填充 + 网格线（下沉 0.05 防 z-fighting）----
        private void FloorPass()
        {
            R3dExt.DrawMeshSolidBasis(_r, _meshFloor,
                Trs(1f, 1f, 1f, 0f, -0.05f, 0f), 228, 228, 232, 255);
            R3dExt.DrawWireBasis(_r, _meshFloorWire,
                Trs(1f, 1f, 1f, 0f, -0.05f, 0f), 205, 205, 212, 255);
        }

        // ---- 静态障碍：灰蓝立方体（对齐 static_collider_pass 的盒体）----
        private void ObstaclePass(GameState state)
        {
            for (int i = 0; i < state.ObstacleCount && i < state.Obstacles.Length; i++)
            {
                var o = state.Obstacles[i];
                float cx = Fp.ToFloat(o.X), cz = Fp.ToFloat(o.Z);
                float sx = Fp.ToFloat(o.Hx) * 2f, sz = Fp.ToFloat(o.Hz) * 2f;
                float sy = Fp.ToFloat(o.Sy);
                float[] mat = Trs(sx, sy, sz, cx, sy * 0.5f, cz);
                R3dExt.DrawMeshSolidBasis(_r, _meshCube, mat, 150, 160, 175, 255);
                R3dExt.DrawWireBasis(_r, _meshCubeWire, mat, 90, 100, 120, 255);
            }
        }

        // ---- 玩家/敌人：按角色/敌我着色，血条用细长 quad ----
        private void EntitiesPass(GameState state, float alpha)
        {
            for (int i = 0; i < GameConst.MaxPlayers; i++)
            {
                var p = state.Players[i];
                if (!p.Alive) continue;
                float x = Lerp(_prevPX[i], Fp.ToFloat(p.X), alpha);
                float y = Lerp(_prevPY[i], Fp.ToFloat(p.Y), alpha);
                float z = Lerp(_prevPZ[i], Fp.ToFloat(p.Z), alpha);
                byte r = 60, g = 120, b = 220;
                if (p.CharId == GameConst.CharGunner) { r = 60; g = 180; b = 120; }
                else if (p.CharId == GameConst.CharAssassin) { r = 200; g = 90; b = 120; }
                DrawEntity(x, y, z, PlayerW, PlayerH, PlayerD, r, g, b);
                DrawHealthBar(x, y + 0.8f, z, p.Hp, p.MaxHp);
            }

            for (int i = 0; i < GameConst.MaxEnemies; i++)
            {
                var e = state.Enemies[i];
                if (!e.Alive) continue;
                float x = Lerp(_prevEX[i], Fp.ToFloat(e.X), alpha);
                float y = Lerp(_prevEY[i], Fp.ToFloat(e.Y), alpha);
                float z = Lerp(_prevEZ[i], Fp.ToFloat(e.Z), alpha);
                DrawEntity(x, y, z, EnemyW, EnemyH, EnemyD, 220, 80, 70);
                DrawHealthBar(x, y + 0.7f, z, e.Hp, e.MaxHp);
            }
        }

        private void DrawEntity(float x, float y, float z, float w, float h, float d,
            byte cr, byte cg, byte cb)
        {
            // 完整 cube（6 面实心）按碰撞体尺寸缩放；中心对齐逻辑 (x,y,z)。
            // C++ 原版用 r3d_mesh_cube(w,h,d) + scale(1,1,1)，这里等价（cube 为 1×1×1）。
            float[] mat = Trs(w, h, d, x, y, z);
            R3dExt.DrawMeshSolidBasis(_r, _meshCube, mat, cr, cg, cb, 255);
            R3dExt.DrawWireBasis(_r, _meshCubeWire, mat, 0, 0, 0, 255);
        }

        private void DrawHealthBar(float x, float y, float z, int hp, int maxHp)
        {
            if (maxHp <= 0 || hp <= 0) return;
            float ratio = (float)hp / maxHp;
            if (ratio > 1f) ratio = 1f;
            float fullW = 0.6f;
            // 底（灰）
            R3dExt.DrawMeshSolidBasis(_r, _meshQuad,
                Trs(fullW, 0.12f, 1f, x, y, z), 60, 60, 60, 255);
            // 前景（绿→红按血量）
            byte g = (byte)(80 + 175 * ratio), rr = (byte)(220 * (1f - ratio));
            R3dExt.DrawMeshSolidBasis(_r, _meshQuad,
                Trs(fullW * ratio, 0.12f, 1f, x - fullW * (1f - ratio) * 0.5f, y, z),
                rr, g, 60, 255);
        }

        // ---- 子弹：小球体，敌我异色 ----
        private void BulletPass(GameState state, float alpha)
        {
            for (int i = 0; i < GameConst.MaxBullets; i++)
            {
                var b = state.Bullets[i];
                if (!b.Alive) continue;
                float x = Lerp(_prevBX[i], Fp.ToFloat(b.X), alpha);
                float y = Lerp(_prevBY[i], Fp.ToFloat(b.Y), alpha);
                float z = Lerp(_prevBZ[i], Fp.ToFloat(b.Z), alpha);
                float[] mat = Trs(0.25f, 0.25f, 0.25f, x, y + 0.8f, z);
                if (b.FromEnemy)
                    R3dExt.DrawMeshSolidBasis(_r, _meshSphere, mat, 230, 60, 60, 255);
                else
                    R3dExt.DrawMeshSolidBasis(_r, _meshSphere, mat, 255, 220, 60, 255);
            }
        }

        // ---- HUD：左上角波次/击杀/帧号 ----
        // 文本降频更新：内容每帧变化会让 r3d_font_bake 每帧新建并上传一张纹理。
        private string _hudText = "";
        private float _hudTextTimer;
        private void HudPass(GameState state, float dt)
        {
            Bindings.R3dFontSetColor(_r, 30, 30, 40);
            _hudTextTimer += dt;
            if (_hudTextTimer >= 0.25f || _hudText.Length == 0) {
                _hudTextTimer = 0f;
                _hudText = "Wave " + state.Wave + "  Kills " + state.Kills +
                           "  Frame " + state.Frame;
            }
            Bindings.R3dFontDraw2dColor(_r, _meshQuad, _hudText, 12f, 12f, 20f, _w, _h, 30, 30, 40);
        }

        // ---- 调试 HUD：帧号 / 逻辑 FPS / 敌人数 / 本地玩家状态（描边两遍） ----
        // 同样降频重建文本；绘制阶段复用缓存字符串，命中 r3d 字体 LRU 缓存，不再每帧烘焙。
        private float _fpsTimer, _fpsFrames;
        private int _logicFps;
        private readonly string[] _debugLines = new string[3];
        private float _debugTextTimer;
        private void DebugHudPass(GameState state, float dt)
        {
            _fpsTimer += dt; _fpsFrames++;
            if (_fpsTimer >= 1f) {
                _logicFps = (int)(_fpsFrames / _fpsTimer);
                _fpsTimer = 0f; _fpsFrames = 0f;
            }

            _debugTextTimer += dt;
            if (_debugTextTimer >= 0.1f || _debugLines[0] == null) {
                _debugTextTimer = 0f;
                int enemies = 0;
                for (int i = 0; i < GameConst.MaxEnemies; i++)
                    if (state.Enemies[i].Alive && !state.Enemies[i].Dead) enemies++;

                int local = -1;
                for (int i = 0; i < GameConst.MaxPlayers; i++)
                    if (state.Players[i].Alive) { local = i; break; }

                _debugLines[0] = "LogicFPS " + _logicFps.ToString("D3") + "   Enemies " + enemies;
                _debugLines[1] = null;
                _debugLines[2] = null;
                if (local >= 0) {
                    var p = state.Players[local];
                    _debugLines[1] = "HP " + p.Hp + "/" + p.MaxHp + "   Pos " +
                        Fp.ToFloat(p.X).ToString("F1") + ", " + Fp.ToFloat(p.Z).ToString("F1") +
                        "   Conv " + p.Conv;
                    if (p.Dead) _debugLines[2] = "Respawn " + (p.RespawnTimer / 20f).ToString("F1") + "s";
                    else if (p.SpawnInvuln > 0) _debugLines[2] = "Invuln " + (p.SpawnInvuln / 20f).ToString("F1") + "s";
                }
            }

            float y = 34f;
            for (int i = 0; i < _debugLines.Length; i++) {
                if (string.IsNullOrEmpty(_debugLines[i])) continue;
                DrawDebugLine(_debugLines[i], y); y += 18f;
            }
        }

        private void DrawDebugLine(string text, float y)
        {
            // 白底偏移 1px + 深色正文 = 描边效果（对齐模板 frame_display_pass）
            Bindings.R3dFontDraw2dColor(_r, _meshQuad, text, 13f, y + 1f, 15f, _w, _h, 250, 250, 250);
            Bindings.R3dFontDraw2dColor(_r, _meshQuad, text, 12f, y, 15f, _w, _h, 30, 30, 40);
        }

        // ---- 碰撞盒调试：玩家红 / 敌人橙 / 障碍绿 / 子弹蓝（线框） ----
        private void CollisionDebugPass(GameState state)
        {
            for (int i = 0; i < GameConst.MaxPlayers; i++) {
                var p = state.Players[i];
                if (!p.Alive) continue;
                float w = Fp.ToFloat(p.ColHx) * 2f, h = Fp.ToFloat(p.ColHy) * 2f, d = Fp.ToFloat(p.ColHz) * 2f;
                R3dExt.DrawWireBasis(_r, _meshCubeWire,
                    Trs(w, h, d, Fp.ToFloat(p.X), Fp.ToFloat(p.Y), Fp.ToFloat(p.Z)), 230, 50, 50, 255);
            }
            for (int i = 0; i < GameConst.MaxEnemies; i++) {
                var e = state.Enemies[i];
                if (!e.Alive || e.Dead) continue;
                float w = Fp.ToFloat(e.ColHx) * 2f, h = Fp.ToFloat(e.ColHy) * 2f, d = Fp.ToFloat(e.ColHz) * 2f;
                R3dExt.DrawWireBasis(_r, _meshCubeWire,
                    Trs(w, h, d, Fp.ToFloat(e.X), Fp.ToFloat(e.Y), Fp.ToFloat(e.Z)), 240, 160, 40, 255);
            }
            for (int i = 0; i < state.ObstacleCount; i++) {
                var o = state.Obstacles[i];
                float w = Fp.ToFloat(o.Hx) * 2f, h = Fp.ToFloat(o.Sy), d = Fp.ToFloat(o.Hz) * 2f;
                R3dExt.DrawWireBasis(_r, _meshCubeWire,
                    Trs(w, h, d, Fp.ToFloat(o.X), Fp.ToFloat(o.Y), Fp.ToFloat(o.Z)), 50, 200, 95, 255);
            }
            const float r = 0.15f;
            for (int i = 0; i < GameConst.MaxBullets; i++) {
                var b = state.Bullets[i];
                if (!b.Alive) continue;
                R3dExt.DrawWireBasis(_r, _meshCubeWire,
                    Trs(r * 2f, r * 2f, r * 2f, Fp.ToFloat(b.X), Fp.ToFloat(b.Y), Fp.ToFloat(b.Z)),
                    60, 190, 255, 255);
            }
        }
    }
}
