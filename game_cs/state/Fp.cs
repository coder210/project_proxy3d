using System;

namespace ProxyGame
{
    /// <summary>
    /// 确定性定点数（Q32，底层 long）——委托 joy 的 joy_math_fp_* C API，
    /// 与 C++ 侧 fp_t（jmath.h）逐位一致，不做任何 C# 自实现运算。
    /// 这是帧同步双端 checksum 一致的基础。
    ///
    /// 基础运算用 joy 原有的 JmathFp* 绑定；mul/div/sqrt/atan/sin/cos 等
    /// 由本轮在 joy IDL 新开放的 joy_math_fp_* 提供（C# 方法名 JoyMathFp*）。
    /// 全部运算都委托给同一套 jmath.h 实现，双端逐位一致。
    /// </summary>
    public static class Fp
    {
        public static long Zero => Joy.Api.Bindings.JmathFpZero();
        public static long One() => Joy.Api.Bindings.JmathFpOne();
        public static long Pi() => Joy.Api.Bindings.JmathFpPi();

        public static long FromInt(int v) => Joy.Api.Bindings.JmathFpFromInt(v);
        public static long FromUint(uint v) => Joy.Api.Bindings.JmathFpFromUint(v);
        public static long FromFloat(float v) => Joy.Api.Bindings.JmathFpFromFloat(v);
        public static long FromFraction(uint packed) => Joy.Api.Bindings.JmathFpFromFraction(packed);
        public static float ToFloat(long v) => Joy.Api.Bindings.JmathFpToFloat(v);

        public static long Add(long a, long b) => Joy.Api.Bindings.JmathFpAdd(a, b);
        public static long Sub(long a, long b) => Joy.Api.Bindings.JmathFpSub(a, b);
        public static long Half() => Joy.Api.Bindings.JmathFpHalf();
        public static long Lerp(long a, long b, long t) => Joy.Api.Bindings.JmathFpLerp(a, b, t);
        public static long Pow2(long a) => Joy.Api.Bindings.JmathFpPow2(a);
        public static long Min(long a, long b) => Joy.Api.Bindings.JmathFpMin(a, b);
        public static long Max(long a, long b) => Joy.Api.Bindings.JmathFpMax(a, b);
        public static long Clamp(long a, long lo, long hi) => Joy.Api.Bindings.JmathFpClamp(a, lo, hi);
        public static int Sign(long a) => Joy.Api.Bindings.JmathFpSign(a);
        public static long MaxValue() => Joy.Api.Bindings.JmathFpMaxValue();
        public static long MinValue() => Joy.Api.Bindings.JmathFpMinValue();

        // ---- 新版绑定（本轮在 joy IDL 开放的 joy_math_fp_*，方法名 JoyMathFp*）----
        public static long Mul(long a, long b) => Joy.Api.Bindings.JoyMathFpMul(a, b);
        public static long Div(long a, long b) => Joy.Api.Bindings.JoyMathFpDiv(a, b);
        public static long Sqrt(long a) => Joy.Api.Bindings.JoyMathFpSqrt(a);
        public static long Atan(long z) => Joy.Api.Bindings.JoyMathFpAtan(z);
        public static long Atan2(long y, long x) => Joy.Api.Bindings.JoyMathFpAtan2(y, x);
        public static long Sin(long a) => Joy.Api.Bindings.JoyMathFpSin(a);
        public static long Cos(long a) => Joy.Api.Bindings.JoyMathFpCos(a);
        public static long Abs(long a) => Joy.Api.Bindings.JoyMathFpAbs(a);
        public static long Round(long a) => Joy.Api.Bindings.JoyMathFpRound(a);
        public static long Exp(long a) => Joy.Api.Bindings.JoyMathFpExp(a);
        public static long Ln(long a) => Joy.Api.Bindings.JoyMathFpLn(a);
        public static long Pow(long b, long exp) => Joy.Api.Bindings.JoyMathFpPow(b, exp);
    }
}
