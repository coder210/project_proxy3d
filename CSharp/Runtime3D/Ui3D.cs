using System;
using Joy.Api;

namespace JoyGame
{
    /// <summary>
    /// UI 场景 2D 绘制工具（复刻各 UI 场景公共的相机/背景板/文字/按钮/矩形/粒子）。
    /// 逻辑分辨率固定 480x320；用正交式相机 eye(0,0,5) 看原点，单位 quad 铺满。
    /// </summary>
    internal sealed class Ui3D
    {
        public const float W = 480f, H = 320f;
        private readonly Jr3d _r;
        private readonly RenderMesh _quad;

        public Jr3d R => _r;
        public Ui3D(Jr3d r, RenderMesh quad) { _r = r; _quad = quad; Bindings.R3dFontInit(r, "fonts/simhei.ttf", 24); }

        public void BeginFrame()
        {
            Bindings.R3dSetClearColor(_r, 255, 255, 255);
            Bindings.R3dCameraLookat(_r, 0, 0, 5, 0, 0, 0, 0, 1, 0, 60f);
            Bindings.R3dBegin(_r);
        }
        public void EndFrame() { Bindings.R3dEnd(_r); }

        public void Text(string s, float x, float y, float size, byte cr, byte cg, byte cb)
        {
            Bindings.R3dFontDraw2dColor(_r, _quad, s, x, y, size, W, H, cr, cg, cb);
        }

        public void Rect(float x, float y, float w, float h, byte cr, byte cg, byte cb)
        {
            // r3d_draw_rect_2d(x,y,w,h,color,winW,winH)，左上角原点
            Bindings.R3dDrawRect2d(_r, _quad, x, y, w, h, cr, cg, cb, W, H);
        }

        public void Border(float x, float y, float w, float h, byte cr, byte cg, byte cb, float t = 2f)
        {
            Rect(x, y, w, t, cr, cg, cb);
            Rect(x, y + h - t, w, t, cr, cg, cb);
            Rect(x, y, t, h, cr, cg, cb);
            Rect(x + w - t, y, t, h, cr, cg, cb);
        }

        public void DrawButton(Jr3dButton btn) { Bindings.R3dButtonDraw(_r, _quad, btn, W, H); }
        public void DrawButtonCircle(Jr3dButton btn) { Bindings.R3dButtonDrawCircle(_r, _quad, btn, W, H); }
        public void DrawInput(Jr3dInput inp) { Bindings.R3dInputDraw(_r, _quad, inp, W, H); }
        public void DrawCombobox(Jr3dComboboxInput ci)
        {
            Bindings.R3dComboboxInputDraw(_r, _quad, ci, W, H);
            Bindings.R3dComboboxInputDrawDropdown(_r, _quad, ci, W, H);
        }
        public void DrawSlider(Jr3dSlider s) { Bindings.R3dSliderDraw(_r, _quad, s, W, H); }
        public void DrawCheckbox(Jr3dCheckbox cb) { Bindings.R3dCheckboxDraw(_r, _quad, cb, W, H); }

        public Jr3dButton Button(float x, float y, float w, float h, string text)
            => R3dExtPlus.ButtonCreate(x, y, w, h, text);
        public Jr3dInput Input(float x, float y, float w, float h, float fs, byte cr, byte cg, byte cb, string text)
            => R3dExtPlus.InputCreate(x, y, w, h, fs, cr, cg, cb, text);
        public Jr3dComboboxInput Combobox(float x, float y, float w, float h, float fs, byte cr, byte cg, byte cb, string text)
            => R3dExtPlus.ComboboxInputCreate(x, y, w, h, fs, cr, cg, cb, text);
        public Jr3dSlider Slider(float x, float y, float w, float h, float min, float max, float init)
            => R3dExtPlus.SliderCreate(x, y, w, h, min, max, init);
        public Jr3dCheckbox Checkbox(float x, float y, float size, string label)
            => R3dExtPlus.CheckboxCreate(x, y, size, label);

        // ---- 轮询式按钮点击判定（替代 SDL_Event 直达 UI）----
        // 返回 true 表示本帧发生完整点击（按下+松开都在按钮内）。
        // 记忆每个按钮上帧 mouse-down 状态，松开时若 hovered 且上帧按在内部则触发。
        private readonly System.Collections.Generic.Dictionary<int, bool> _btnDown =
            new System.Collections.Generic.Dictionary<int, bool>();

        public bool Clicked(Jr3dButton btn, float mx, float my, bool mouseDown)
        {
            int key = btn.GetHashCode();
            bool wasDown;
            _btnDown.TryGetValue(key, out wasDown);
            bool hovered = Bindings.R3dButtonIsHovered(_r, btn, mx, my);
            bool clicked = false;
            if (mouseDown) {
                Bindings.R3dButtonIsPressed(_r, btn, true, mx, my);
                if (hovered) _btnDown[key] = true;
            } else {
                if (wasDown && hovered) clicked = true;
                _btnDown[key] = false;
                Bindings.R3dButtonRelease(_r, btn, mx, my);
            }
            return clicked;
        }
    }

    /// <summary>UI 背景纸纹粒子（5 个场景复用，简化为确定性伪随机）。</summary>
    internal sealed class UiParticles
    {
        private struct P { public float X, Y, S; }
        private readonly P[] _ps = new P[30];
        private float _t;
        public UiParticles() {
            uint seed = 12345;
            for (int i = 0; i < _ps.Length; i++) {
                seed = seed * 1664525u + 1013904223u;
                _ps[i].X = (seed % 1000) / 1000f * Ui3D.W;
                seed = seed * 1664525u + 1013904223u;
                _ps[i].Y = (seed % 1000) / 1000f * Ui3D.H;
                seed = seed * 1664525u + 1013904223u;
                _ps[i].S = 1.5f + (seed % 1000) / 1000f * 1.5f;
            }
        }
        public void Update(float dt) { _t += dt; }
        public void Draw(Ui3D ui) {
            for (int i = 0; i < _ps.Length; i++) {
                var p = _ps[i];
                float drift = (_t * (4f + p.S)) % Ui3D.W;
                float x = (p.X + drift) % Ui3D.W;
                ui.Rect(x, p.Y, p.S, p.S, 150, 150, 160);
            }
        }
    }
}
