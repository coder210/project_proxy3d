using System;

namespace ProxyGame
{
    /// <summary>协议枚举，逐项复刻 shared/protocol.h。</summary>
    public static class S2CType
    {
        public const int Normal = 0, Player = 1, Enemy = 2, Prop = 3;
    }
    public static class C2SCmd
    {
        public const int Unknown = 0, PlayerJoin = 1, PlayerLeave = 2,
            PlayerInput = 3, PlayerHeart = 4, Loading = 5;
    }
    public static class S2CCmd
    {
        public const int None = 0, Loading = 1, Command = 2, MapReload = 3;
    }

    /// <summary>
    /// 世界快照实体，摊平自 shared/protocol.h 的 S2CEntity。
    /// 坐标/尺寸/血量均为 fp 原始 long；字符串昵称用托管 string（打包时按 UTF-8 字节）。
    /// </summary>
    public struct S2CEntity
    {
        public int Id, Type;
        public long PositionX, PositionY, PositionZ, Width, Height, Hp;
        public int PlayerConv;
        public short YawQuantized;
        public long AiType;
        public int CharId;
        public string Nickname;
        public int AiTimer, AiState;
        public long AiTargetX, AiTargetZ;
        public int PfTargetGx, PfTargetGz, PfPathLen, PfNextIdx, PfRecomputeCd;

        public S2CEntity InitZero()
        {
            Nickname = string.Empty;
            return this;
        }
    }
}
