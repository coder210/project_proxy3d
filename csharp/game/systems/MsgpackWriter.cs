using System;
using System.Collections.Generic;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 最小 msgpack 打包器（纯托管），遵循 msgpack-c 的整数最小编码规则，
    /// 用于复刻 shared/protocol.h 的 pack_entity/pack_world/pack_s2c。
    /// 与 C++ parity harness 里同构的内置 packer 输出逐字节对照验证。
    /// </summary>
    public sealed class MsgpackWriter
    {
        private readonly List<byte> _buf = new List<byte>(512);

        public byte[] Bytes => _buf.ToArray();
        public string Hex() => BitConverter.ToString(Bytes).Replace("-", "");

        private void U8(int v) => _buf.Add((byte)(v & 0xFF));
        private void U16(int v) { U8(v >> 8); U8(v); }
        private void U32(uint v) { U8((int)(v >> 24)); U8((int)(v >> 16)); U8((int)(v >> 8)); U8((int)v); }
        private void U64(ulong v) { U32((uint)(v >> 32)); U32((uint)v); }

        public void PackArray(int n)
        {
            if (n < 16) U8(0x90 | n);
            else if (n <= 0xFFFF) { U8(0xDC); U16(n); }
            else { U8(0xDD); U32((uint)n); }
        }

        public void PackNil() => U8(0xC0);

        /// <summary>msgpack_pack_int：按 int 最小编码（正数 fixint/uint8/16/32，负数 negfixint/int8/16/32）。</summary>
        public void PackInt(int d)
        {
            if (d >= 0) PackUint((uint)d);
            else {
                if (d >= -32) U8(0xE0 | (d & 0x1F));
                else if (d >= -128) { U8(0xD0); U8(d); }
                else if (d >= -32768) { U8(0xD1); U16(d); }
                else { U8(0xD2); U32((uint)d); }
            }
        }

        public void PackUint(uint d)
        {
            if (d <= 0x7F) U8((int)d);
            else if (d <= 0xFF) { U8(0xCC); U8((int)d); }
            else if (d <= 0xFFFF) { U8(0xCD); U16((int)d); }
            else { U8(0xCE); U32(d); }
        }

        /// <summary>msgpack_pack_int64：按 int64 最小编码。</summary>
        public void PackInt64(long d)
        {
            if (d >= 0) PackUint64((ulong)d);
            else {
                if (d >= -32) U8(0xE0 | (int)(d & 0x1F));
                else if (d >= -128) { U8(0xD0); U8((int)d); }
                else if (d >= -32768) { U8(0xD1); U16((int)d); }
                else if (d >= -2147483648L) { U8(0xD2); U32((uint)(int)d); }
                else { U8(0xD3); U64(unchecked((ulong)d)); }
            }
        }

        public void PackUint64(ulong d)
        {
            if (d <= 0x7F) U8((int)d);
            else if (d <= 0xFF) { U8(0xCC); U8((int)d); }
            else if (d <= 0xFFFF) { U8(0xCD); U16((int)d); }
            else if (d <= 0xFFFFFFFFUL) { U8(0xCE); U32((uint)d); }
            else { U8(0xCF); U64(d); }
        }

        public void PackStr(string s)
        {
            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(s ?? string.Empty);
            int n = bytes.Length;
            if (n < 32) U8(0xA0 | n);
            else if (n <= 0xFF) { U8(0xD9); U8(n); }
            else if (n <= 0xFFFF) { U8(0xDA); U16(n); }
            else { U8(0xDB); U32((uint)n); }
            for (int i = 0; i < n; ++i) _buf.Add(bytes[i]);
        }
    }
}
