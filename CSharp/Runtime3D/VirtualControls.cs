using System;
using Joy.Api;
using ProxyGame;

namespace JoyGame
{
    /// <summary>
    /// 手机端虚拟输入（对齐 joy_topdown_template 的 GameplayControlsLayer）：
    /// 左下 8 向摇杆（贴图 joystick_circle_pad_d / joystick_circle_nub_c），
    /// 右下攻击圆钮（button_circle / button_circle_highlight）按住持续攻击。
    /// 摇杆方向位每次先清本层上一帧贡献再重设，避免斜向切换时残留旧方向。
    /// 键盘位另有来源，SyncKeyboardVisuals 只同步显示，不改输入掩码。
    /// 单指针局限：无法真机双指同时"走+攻"，需要真机多点触摸再补 C++ 绑定。
    /// </summary>
    internal sealed class VirtualControls
    {
        // 设计稿 480×320（与模板 syncLayout 一致）
        private const float DesignW = 480f, DesignH = 320f;
        private const float StickRadius = 72f;                       // 模板半径 72*scale
        private const float StickCx = StickRadius + 9.6f;            // 81.6，左下锚定
        private const float StickCy = DesignH - StickRadius - 24f;   // 224
        private const float NubRadius = StickRadius * 0.4f;          // 模板 handle_radius
        private const float StickDead = 0.25f;                       // 模板 mag<=0.25 回中
        private const float AxisThreshold = 0.35f;                   // 模板八向置位阈值
        private const float BtnSize = 64f;
        private const float BtnCx = DesignW - 4f - BtnSize * 0.5f;   // 444，右留边 4
        private const float BtnCy = DesignH - 6f - BtnSize * 0.5f;   // 282，下留边 6

        private const string PadPath = "sprites/ui/joystick_circle_pad_d.png";
        private const string NubPath = "sprites/ui/joystick_circle_nub_c.png";
        private const string BtnPath = "sprites/ui/button_circle.png";
        private const string BtnHlPath = "sprites/ui/button_circle_highlight.png";

        private bool _stickActive;
        private bool _btnActive;
        private bool _kbAttack;
        private float _stickDx, _stickDy;   // 归一化向量（键盘同步或触摸）
        private byte _stickMask;            // 本层上一帧写入 mask 的方向位

        public bool Visible = true;

        /// <summary>每帧采集：把虚拟输入合并进 mask，并返回近战上升沿。</summary>
        public void Collect(ref byte mask, out bool meleeEdge)
        {
            meleeEdge = false;
            if (!Visible) {
                _stickMask = 0;
                _stickActive = false;
                _btnActive = false;
                _stickDx = _stickDy = 0f;
                return;
            }

            // 模板 updateStick：先无条件清掉本层上一帧的方向位（保留键盘位），再按当前向量重设。
            // 不清会在"从右直接拖到上"时留下 RIGHT，变成错误的右上斜向。
            mask &= (byte)~_stickMask;
            _stickMask = 0;

            float mx = Joy.Input.mouse_x(), my = Joy.Input.mouse_y();
            bool down = Joy.Input.mouse_button(1);

            // 攻击键优先：按下即置位并持续到松开（与模板 pressed_btn / INPUT_ATTACK 一致）
            bool btnHit = Dist(mx, my, BtnCx, BtnCy) <= BtnSize * 0.5f;
            if (!_btnActive && down && btnHit && !_stickActive) {
                _btnActive = true;
                meleeEdge = true;
            }
            if (_btnActive) {
                if (down) mask |= GameConst.InputAttack;
                else _btnActive = false;
            }

            // 摇杆：只在半径内按下才激活，松开立即回中
            if (down && !_btnActive && Dist(mx, my, StickCx, StickCy) <= StickRadius)
                _stickActive = true;
            else if (!down)
                _stickActive = false;

            if (_stickActive) {
                float dx = mx - StickCx, dy = my - StickCy;
                float len = (float)Math.Sqrt(dx * dx + dy * dy);
                if (len > StickRadius) {
                    float k = StickRadius / len;
                    dx *= k; dy *= k; len = StickRadius;
                }
                float mag = len / StickRadius;
                _stickDx = mag > 0f ? dx / StickRadius : 0f;
                _stickDy = mag > 0f ? dy / StickRadius : 0f;

                if (mag > StickDead) {
                    // 八向：左右/上下可同时置位 → 斜向移动（与模板 updateStick 同阈值）
                    byte stickBits = 0;
                    if (_stickDy < -AxisThreshold) stickBits |= GameConst.InputUp;
                    if (_stickDy >  AxisThreshold) stickBits |= GameConst.InputDown;
                    if (_stickDx < -AxisThreshold) stickBits |= GameConst.InputLeft;
                    if (_stickDx >  AxisThreshold) stickBits |= GameConst.InputRight;
                    mask |= stickBits;
                    _stickMask = stickBits;
                }
            } else {
                _stickDx = 0f;
                _stickDy = 0f;
            }
        }

        /// <summary>键盘按下时同步显示摇杆/攻击钮（模板 Render 同款），不改逻辑。</summary>
        public void SyncKeyboardVisuals(byte mask, bool attackDown)
        {
            if (_stickActive) return;
            _kbAttack = attackDown;
            float dx = 0f, dy = 0f;
            if ((mask & GameConst.InputRight) != 0) dx += 1f;
            if ((mask & GameConst.InputLeft)  != 0) dx -= 1f;
            if ((mask & GameConst.InputDown)  != 0) dy += 1f;
            if ((mask & GameConst.InputUp)    != 0) dy -= 1f;
            // 模板 joy_r2d_joystick_set_vector 会归一化，斜向不应超出摇杆半径
            float len = (float)Math.Sqrt(dx * dx + dy * dy);
            if (len > 0f) { dx /= len; dy /= len; }
            _stickDx = dx;
            _stickDy = dy;
        }

        public void Draw(Jr3d r, RenderMesh quad)
        {
            if (!Visible) return;

            // 摇杆底盘与摇杆头（模板 joy_r2d_joystick_draw 的 background/handle 矩形）
            Bindings.JoyR3dDrawUiTexture(r, quad, PadPath,
                StickCx - StickRadius, StickCy - StickRadius,
                StickRadius * 2f, StickRadius * 2f, Ui3D.W, Ui3D.H);
            float hx = StickCx + _stickDx * StickRadius;
            float hy = StickCy + _stickDy * StickRadius;
            Bindings.JoyR3dDrawUiTexture(r, quad, NubPath,
                hx - NubRadius, hy - NubRadius,
                NubRadius * 2f, NubRadius * 2f, Ui3D.W, Ui3D.H);

            // 攻击圆钮：触摸按下或键盘 INPUT_ATTACK 都点亮金色辉光贴图
            bool pressed = _btnActive || _kbAttack;
            Bindings.JoyR3dDrawUiTexture(r, quad, pressed ? BtnHlPath : BtnPath,
                BtnCx - BtnSize * 0.5f, BtnCy - BtnSize * 0.5f,
                BtnSize, BtnSize, Ui3D.W, Ui3D.H);
            Bindings.R3dFontDraw2dColor(r, quad, "攻", BtnCx - 11f, BtnCy - 12f,
                20f, Ui3D.W, Ui3D.H, 20, 20, 25);
        }

        private static float Dist(float x, float y, float cx, float cy)
        {
            float dx = x - cx, dy = y - cy;
            return (float)Math.Sqrt(dx * dx + dy * dy);
        }
    }
}
