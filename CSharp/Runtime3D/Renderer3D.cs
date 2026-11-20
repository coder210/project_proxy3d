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
        // 实体专用分面 mesh：顶面/正面/侧面用不同亮度，避免同色纸片感
        private readonly RenderMesh _playerTop, _playerFront, _playerSide, _meshPlayerWire;
        private readonly RenderMesh _enemyTop, _enemyFront, _enemySide, _meshEnemyWire;
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
        private const float CamDist = 18.0f;
        private const float CamPitch = 55.0f * 3.14159265f / 180.0f;

        public Renderer3D(Jr3d r, int w, int h)
        {
            _r = r;
            _w = w;
            _h = h;

            // 地板 = 64x64 大平面（对齐地图尺寸）；线框稍小避免与填充完全共面
            _meshFloor = Bindings.R3dMeshPlane(r, 64f, 64f, 1);
            _meshFloorWire = Bindings.R3dMeshPlaneWire(r, 64f, 64f, 1);
            // 实体基础几何体（立方体/球；quad 供 HUD 2D 用）
            _meshCube = Bindings.R3dMeshCube(r, 1f, 1f, 1f);
            _meshCubeWire = Bindings.R3dMeshCubeWire(r, 1f, 1f, 1f);
            MakeBoxFaces(r, 3.0f, 4.0f, 3.0f, out _playerTop, out _playerFront, out _playerSide);
            _meshPlayerWire = Bindings.R3dMeshCubeWire(r, 3.0f, 4.0f, 3.0f);
            MakeBoxFaces(r, 2.5f, 3.2f, 2.5f, out _enemyTop, out _enemyFront, out _enemySide);
            _meshEnemyWire = Bindings.R3dMeshCubeWire(r, 2.5f, 3.2f, 2.5f);
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

        private static byte Shade(byte c, float f)
        {
            int v = (int)(c * f);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            return (byte)v;
        }

        private static void MakeBoxFaces(Jr3d r, float w, float h, float d,
            out RenderMesh top, out RenderMesh front, out RenderMesh side)
        {
            float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
            float[] quad = new float[6 * 5];
            int k = 0;
            Action<float, float, float> v = (x, y, z) => {
                quad[k++] = x; quad[k++] = y; quad[k++] = z;
                quad[k++] = 0f; quad[k++] = 0f;
            };

            k = 0; // 顶面 y=+hh
            v(-hw, hh, -hd); v(hw, hh, -hd); v(hw, hh, hd);
            v(-hw, hh, -hd); v(hw, hh, hd); v(-hw, hh, hd);
            top = R3dExt.MeshCustom(r, quad, 6);

            k = 0; // 正面 z=-hd（相机在 -Z 一侧）
            v(-hw, -hh, -hd); v(hw, -hh, -hd); v(hw, hh, -hd);
            v(-hw, -hh, -hd); v(hw, hh, -hd); v(-hw, hh, -hd);
            front = R3dExt.MeshCustom(r, quad, 6);

            k = 0; // 右侧面 x=+hw
            v(hw, -hh, -hd); v(hw, -hh, hd); v(hw, hh, hd);
            v(hw, -hh, -hd); v(hw, hh, hd); v(hw, hh, -hd);
            side = R3dExt.MeshCustom(r, quad, 6);
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

        public void RenderFrame(GameState state, float dt, float alpha)
        {
            CameraPass(state, dt, alpha);

            if (!Bindings.R3dBegin(_r)) return;

            FloorPass();
            ObstaclePass(state);
            EntitiesPass(state, alpha);
            BulletPass(state, alpha);
            HudPass(state);

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
                DrawEntity(x, y, z, _playerTop, _playerFront, _playerSide, _meshPlayerWire, 4.0f, r, g, b);
                DrawHealthBar(x, y + 4.3f, z, p.Hp, p.MaxHp);
            }

            for (int i = 0; i < GameConst.MaxEnemies; i++)
            {
                var e = state.Enemies[i];
                if (!e.Alive) continue;
                float x = Lerp(_prevEX[i], Fp.ToFloat(e.X), alpha);
                float y = Lerp(_prevEY[i], Fp.ToFloat(e.Y), alpha);
                float z = Lerp(_prevEZ[i], Fp.ToFloat(e.Z), alpha);
                DrawEntity(x, y, z, _enemyTop, _enemyFront, _enemySide, _meshEnemyWire, 3.2f, 220, 80, 70);
                DrawHealthBar(x, y + 3.5f, z, e.Hp, e.MaxHp);
            }
        }

        private void DrawEntity(float x, float y, float z, RenderMesh top, RenderMesh front,
            RenderMesh side, RenderMesh wire, float height, byte cr, byte cg, byte cb)
        {
            float[] mat = Trs(1f, 1f, 1f, x, y + height * 0.5f, z);
            R3dExt.DrawMeshSolidBasis(_r, top, mat, Shade(cr, 1.35f), Shade(cg, 1.35f), Shade(cb, 1.35f), 255);
            R3dExt.DrawMeshSolidBasis(_r, front, mat, cr, cg, cb, 255);
            R3dExt.DrawMeshSolidBasis(_r, side, mat, Shade(cr, 0.65f), Shade(cg, 0.65f), Shade(cb, 0.65f), 255);
            R3dExt.DrawWireBasis(_r, wire, mat, 0, 0, 0, 255);
        }

        private void DrawHealthBar(float x, float y, float z, int hp, int maxHp)
        {
            if (maxHp <= 0 || hp <= 0) return;
            float ratio = (float)hp / maxHp;
            if (ratio > 1f) ratio = 1f;
            float fullW = 1.4f;
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
        private void HudPass(GameState state)
        {
            Bindings.R3dFontSetColor(_r, 30, 30, 40);
            string text = "Wave " + state.Wave + "  Kills " + state.Kills +
                          "  Frame " + state.Frame;
            Bindings.R3dFontDraw2dColor(_r, _meshQuad, text, 12f, 12f, 20f, _w, _h, 30, 30, 40);
        }
    }
}
