using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using Joy.Api;

namespace ProxyGame
{
    /// <summary>
    /// 帧同步状态指纹，逐位复刻 shared/systems/state_checksum.h。
    /// 公式（两端必须逐字节一致）：
    ///   1. 收集带 IdComponent + LogicPositionComponent 的实体（本项目 = 玩家 + 敌人）
    ///   2. 按 id 升序排序
    ///   3. 拼接 string.Format(InvariantCulture, "{0}:{1}:{2}:{3},", id, x, y, z)（坐标为 fp 原始 long）
    ///   4. CryptoExt.Md5Hex（委托 joy_core_utils_md5x，与 C++ 同一实现）
    /// 必须在"模拟本帧之前"调用，与服务端 collect_input 时机一致。
    /// </summary>
    public static class Checksum
    {
        private struct Entry
        {
            public int Id;
            public long X, Y, Z;
        }

        public static string ComputeFull(GameState state)
        {
            var entries = new List<Entry>();
            for (int i = 0; i < state.Players.Length; ++i)
            {
                var p = state.Players[i];
                if (!p.Alive) continue;
                entries.Add(new Entry { Id = p.Id, X = p.X, Y = p.Y, Z = p.Z });
            }
            for (int i = 0; i < state.Enemies.Length; ++i)
            {
                var e = state.Enemies[i];
                if (!e.Alive) continue;
                entries.Add(new Entry { Id = e.Id, X = e.X, Y = e.Y, Z = e.Z });
            }
            // 按 id 升序（与 std::sort 一致；id 唯一，无稳定性问题）
            entries.Sort((a, b) => a.Id.CompareTo(b.Id));

            var sb = new StringBuilder();
            foreach (var e in entries)
            {
                sb.Append(e.Id.ToString(CultureInfo.InvariantCulture)).Append(':');
                sb.Append(e.X.ToString(CultureInfo.InvariantCulture)).Append(':');
                sb.Append(e.Y.ToString(CultureInfo.InvariantCulture)).Append(':');
                sb.Append(e.Z.ToString(CultureInfo.InvariantCulture)).Append(',');
            }
            return CryptoExt.Md5Hex(sb.ToString());
        }

        /// <summary>可读实体快照 "id,x,y,z;..."，与 checksum_entries_string 同格式，便于 diff。</summary>
        public static string EntriesString(GameState state)
        {
            var entries = new List<Entry>();
            for (int i = 0; i < state.Players.Length; ++i)
            {
                var p = state.Players[i];
                if (p.Alive) entries.Add(new Entry { Id = p.Id, X = p.X, Y = p.Y, Z = p.Z });
            }
            for (int i = 0; i < state.Enemies.Length; ++i)
            {
                var e = state.Enemies[i];
                if (e.Alive) entries.Add(new Entry { Id = e.Id, X = e.X, Y = e.Y, Z = e.Z });
            }
            entries.Sort((a, b) => a.Id.CompareTo(b.Id));

            var sb = new StringBuilder();
            foreach (var e in entries)
            {
                sb.Append(e.Id.ToString(CultureInfo.InvariantCulture)).Append(',');
                sb.Append(e.X.ToString(CultureInfo.InvariantCulture)).Append(',');
                sb.Append(e.Y.ToString(CultureInfo.InvariantCulture)).Append(',');
                sb.Append(e.Z.ToString(CultureInfo.InvariantCulture)).Append(';');
            }
            return sb.ToString();
        }
    }
}
