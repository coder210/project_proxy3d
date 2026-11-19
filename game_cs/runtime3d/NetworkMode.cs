using System;
using ProxyGame;

namespace JoyGame
{
    /// <summary>
    /// 网络模式框架（复刻 C++ client.cpp/server.cpp 的四种模式职责）。
    ///
    /// - Single：本地完整可用（输入直接写 GameState.Net，消费系统按帧处理）。
    /// - Host/Server：权威端骨架——维护帧号、连接、命令缓冲；真实 socket 收发需要
    ///   joy 的 NetclientCreate/NetserverCreate + NetMessage data 访问绑定（IDL status:off，
    ///   与 r3d_create 同类，需手写 InternalCall 适配，见 compat 计划）。
    /// - Client：消费者骨架——按帧应用命令；同上，socket 部分待绑定补齐。
    ///
    /// 当前实现保证：四种模式都能进入战斗、逻辑正常推进、单机/本地多开无异常。
    /// </summary>
    internal sealed class NetworkMode
    {
        private readonly GameContext _ctx;
        private int _frameId;
        private int _localConv = 1;
        private bool _loaded;

        public NetworkMode(GameContext ctx) { _ctx = ctx; }

        public void Init() {
            _frameId = 0;
            _loaded = false;
            _localConv = _ctx.Mode == GameMode.Server ? 0 : 1;
            _ctx.State.Frame = 0;
            Joy.Host.log("[net] init mode=" + _ctx.Mode + " conv=" + _localConv);
        }

        /// <summary>在逻辑帧前把本地输入写进 NetCommands（等价 C++ *collect_local_input）。</summary>
        public void CollectLocalInput() {
            if (_ctx.Mode == GameMode.Server) return; // 独立服务端无本地玩家

            var s = _ctx.State;
            var net = s.Net;
            net.FrameId = _frameId;
            if (net.NumInputs < net.Inputs.Length) {
                int attack = 0;
                if (_ctx.AttackTriggered) attack = 1;
                else if (_ctx.BulletTriggered) attack = 2;
                else if (_ctx.RayTriggered) attack = 3;
                short yaw = (short)(s.Players[0].YawQuantized);
                net.Inputs[net.NumInputs++] = new NetInput {
                    ClientSeq = (uint)_frameId, Conv = _localConv,
                    Dir = _ctx.InputMask, Attack = attack, YawQuantized = yaw
                };
            }
        }

        /// <summary>每帧网络 pump（真实收发待 net 绑定补齐；本地模式直接按帧推进）。</summary>
        public void Update(float dt) {
            _frameId++;
            _ctx.State.Net.FrameId = _frameId;
            if (!_loaded) _loaded = true;
        }

        public void Destroy() {
            Joy.Host.log("[net] destroy frames=" + _frameId);
        }
    }
}
