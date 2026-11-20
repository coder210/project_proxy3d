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
        private readonly RenderMesh _meshSphere;
        private readonly RenderMesh _meshQuad;

        // 相机平滑跟随目标（对齐 C++ camera_pass 的 camera_target）
        private float _camX, _camZ;
        private bool _camInit;

        // 相机常量（复刻 camera_pass.cpp:18-19）
        private const float CamDist = 28.0f;
        private const float CamPitch = 75.0f * 3.14159265f / 180.0f;

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
            _meshSphere = Bindings.R3dMeshSphere(r, 0.5f, 12, 8);
            _meshQuad = Bindings.R3dMeshQuad(r);

            Bindings.R3dFontInit(r, "assets/fonts/simhei.ttf", 24);
        }

        public void RenderFrame(GameState state, float dt)
        {
            CameraPass(state, dt);

            if (!Bindings.R3dBegin(_r)) return;

            FloorPass();
            ObstaclePass(state);
            EntitiesPass(state, dt);
            BulletPass(state);
            HudPass(state);

            Bindings.R3dEnd(_r);
        }

        // ---- camera_pass：白底清屏 + 俯视 75° 相机，平滑跟随本地玩家 ----
        private void CameraPass(GameState state, float dt)
        {
            Bindings.R3dSetClearColor(_r, 255, 255, 255);

            float targetX = 0f, targetY = 0f, targetZ = 0f;
            int follow = state.CameraFollowConv;
            for (int i = 0; follow >= 0 && i < GameConst.MaxPlayers; i++)
            {
                var p = state.Players[i];
                if (p.Alive && p.Conv == follow)
                {
                    targetX = Fp.ToFloat(p.X);
                    targetY = Fp.ToFloat(p.Y);
                    targetZ = Fp.ToFloat(p.Z);
                    break;
                }
            }

            float alpha = dt * 10f;
            if (alpha > 1f) alpha = 1f;
            if (!_camInit) { _camX = targetX; _camZ = targetZ; _camInit = true; }
            else { _camX += (targetX - _camX) * alpha; _camZ += (targetZ - _camZ) * alpha; }

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
                R3dExt.MakeTrs(1f, 1f, 1f, 0f, -0.05f, 0f), 228, 228, 232, 255);
            R3dExt.DrawWireBasis(_r, _meshFloorWire,
                R3dExt.MakeTrs(1f, 1f, 1f, 0f, -0.05f, 0f), 205, 205, 212, 255);
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
                float[] mat = R3dExt.MakeTrs(sx, sy, sz, cx, sy * 0.5f, cz);
                R3dExt.DrawMeshSolidBasis(_r, _meshCube, mat, 150, 160, 175, 255);
                R3dExt.DrawWireBasis(_r, _meshCube, mat, 90, 100, 120, 255);
            }
        }

        // ---- 玩家/敌人：按角色/敌我着色，血条用细长 quad ----
        private void EntitiesPass(GameState state, float dt)
        {
            for (int i = 0; i < GameConst.MaxPlayers; i++)
            {
                var p = state.Players[i];
                if (!p.Alive) continue;
                byte r = 60, g = 120, b = 220;
                if (p.CharId == GameConst.CharGunner) { r = 60; g = 180; b = 120; }
                else if (p.CharId == GameConst.CharAssassin) { r = 200; g = 90; b = 120; }
                DrawEntity(p.X, p.Y, p.Z, Fp.ToFloat(p.ColHx) * 2f,
                    Fp.ToFloat(p.ColHx) * 2f, r, g, b);
                DrawHealthBar(p.X, p.Y, p.Z, p.Hp, p.MaxHp);
            }

            for (int i = 0; i < GameConst.MaxEnemies; i++)
            {
                var e = state.Enemies[i];
                if (!e.Alive) continue;
                DrawEntity(e.X, e.Y, e.Z, Fp.ToFloat(e.ColHx) * 2f,
                    Fp.ToFloat(e.ColHx) * 2f, 220, 80, 70);
                DrawHealthBar(e.X, e.Y, e.Z, e.Hp, e.MaxHp);
            }
        }

        private void DrawEntity(long fx, long fy, long fz, float sx, float sz,
            byte cr, byte cg, byte cb)
        {
            float x = Fp.ToFloat(fx), y = Fp.ToFloat(fy), z = Fp.ToFloat(fz);
            if (sx <= 0f) sx = 1f;
            if (sz <= 0f) sz = 1f;
            float h = 1.6f;
            float[] mat = R3dExt.MakeTrs(sx, h, sz, x, y + h * 0.5f, z);
            R3dExt.DrawMeshSolidBasis(_r, _meshCube, mat, cr, cg, cb, 255);
            R3dExt.DrawWireBasis(_r, _meshCube, mat, 30, 30, 40, 255);
        }

        private void DrawHealthBar(long fx, long fy, long fz, int hp, int maxHp)
        {
            if (maxHp <= 0 || hp <= 0) return;
            float x = Fp.ToFloat(fx), y = Fp.ToFloat(fy) + 1.9f, z = Fp.ToFloat(fz);
            float ratio = (float)hp / maxHp;
            if (ratio > 1f) ratio = 1f;
            float fullW = 1.0f;
            // 底（灰）
            R3dExt.DrawMeshSolidBasis(_r, _meshQuad,
                R3dExt.MakeTrs(fullW, 0.12f, 1f, x, y, z), 60, 60, 60, 255);
            // 前景（绿→红按血量）
            byte g = (byte)(80 + 175 * ratio), rr = (byte)(220 * (1f - ratio));
            R3dExt.DrawMeshSolidBasis(_r, _meshQuad,
                R3dExt.MakeTrs(fullW * ratio, 0.12f, 1f, x - fullW * (1f - ratio) * 0.5f, y, z),
                rr, g, 60, 255);
        }

        // ---- 子弹：小球体，敌我异色 ----
        private void BulletPass(GameState state)
        {
            for (int i = 0; i < GameConst.MaxBullets; i++)
            {
                var b = state.Bullets[i];
                if (!b.Alive) continue;
                float x = Fp.ToFloat(b.X), y = Fp.ToFloat(b.Y), z = Fp.ToFloat(b.Z);
                float[] mat = R3dExt.MakeTrs(0.25f, 0.25f, 0.25f, x, y + 0.8f, z);
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
