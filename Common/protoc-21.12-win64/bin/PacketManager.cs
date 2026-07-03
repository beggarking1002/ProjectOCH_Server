using Google.Protobuf;
using ServerCore;
using System;
using System.Collections.Generic;
using Protocol;

public enum MsgId : ushort
{
    C_LOGIN = 1000,
    S_LOGIN = 1001,
    C_ENTER_GAME = 1002,
    S_ENTER_GAME = 1003,
    C_LEAVE_GAME = 1004,
    S_LEAVE_GAME = 1005,
    S_SPAWN = 1006,
    S_DESPAWN = 1007,
    C_MOVE = 1008,
    S_MOVE = 1009,
    C_CHAT = 1010,
    S_CHAT = 1011,
    C_ENTER_BATTLE = 1012,
    S_ENTER_BATTLE = 1013,
    C_BATTLE_MOVE = 1014,
    S_BATTLE_MOVE = 1015,
    C_BATTLE_SKILL = 1016,
    S_BATTLE_SKILL = 1017,
    C_BATTLE_END_TURN = 1018,
    S_BATTLE_END_TURN = 1019,
}

class PacketManager
{
    #region Singleton
    static PacketManager _instance = new PacketManager();
    public static PacketManager Instance { get { return _instance; } }
    #endregion

    PacketManager()
    {
        Register();
    }

    Dictionary<ushort, Action<PacketSession, ArraySegment<byte>, ushort>> _onRecv
        = new Dictionary<ushort, Action<PacketSession, ArraySegment<byte>, ushort>>();
    Dictionary<ushort, Action<PacketSession, IMessage>> _handler
        = new Dictionary<ushort, Action<PacketSession, IMessage>>();

    public Action<PacketSession, IMessage, ushort> CustomHandler { get; set; }

    public void Register()
    {
        _onRecv.Add((ushort)MsgId.S_LOGIN, MakePacket<S_LOGIN>);
        _handler.Add((ushort)MsgId.S_LOGIN, PacketHandler.S_LOGINHandler);
        _onRecv.Add((ushort)MsgId.S_ENTER_GAME, MakePacket<S_ENTER_GAME>);
        _handler.Add((ushort)MsgId.S_ENTER_GAME, PacketHandler.S_ENTER_GAMEHandler);
        _onRecv.Add((ushort)MsgId.S_LEAVE_GAME, MakePacket<S_LEAVE_GAME>);
        _handler.Add((ushort)MsgId.S_LEAVE_GAME, PacketHandler.S_LEAVE_GAMEHandler);
        _onRecv.Add((ushort)MsgId.S_SPAWN, MakePacket<S_SPAWN>);
        _handler.Add((ushort)MsgId.S_SPAWN, PacketHandler.S_SPAWNHandler);
        _onRecv.Add((ushort)MsgId.S_DESPAWN, MakePacket<S_DESPAWN>);
        _handler.Add((ushort)MsgId.S_DESPAWN, PacketHandler.S_DESPAWNHandler);
        _onRecv.Add((ushort)MsgId.S_MOVE, MakePacket<S_MOVE>);
        _handler.Add((ushort)MsgId.S_MOVE, PacketHandler.S_MOVEHandler);
        _onRecv.Add((ushort)MsgId.S_CHAT, MakePacket<S_CHAT>);
        _handler.Add((ushort)MsgId.S_CHAT, PacketHandler.S_CHATHandler);
        _onRecv.Add((ushort)MsgId.S_ENTER_BATTLE, MakePacket<S_ENTER_BATTLE>);
        _handler.Add((ushort)MsgId.S_ENTER_BATTLE, PacketHandler.S_ENTER_BATTLEHandler);
        _onRecv.Add((ushort)MsgId.S_BATTLE_MOVE, MakePacket<S_BATTLE_MOVE>);
        _handler.Add((ushort)MsgId.S_BATTLE_MOVE, PacketHandler.S_BATTLE_MOVEHandler);
        _onRecv.Add((ushort)MsgId.S_BATTLE_SKILL, MakePacket<S_BATTLE_SKILL>);
        _handler.Add((ushort)MsgId.S_BATTLE_SKILL, PacketHandler.S_BATTLE_SKILLHandler);
        _onRecv.Add((ushort)MsgId.S_BATTLE_END_TURN, MakePacket<S_BATTLE_END_TURN>);
        _handler.Add((ushort)MsgId.S_BATTLE_END_TURN, PacketHandler.S_BATTLE_END_TURNHandler);
    }

    public void OnRecvPacket(PacketSession session, ArraySegment<byte> buffer)
    {
        ushort count = 0;
        ushort size = BitConverter.ToUInt16(buffer.Array, buffer.Offset);
        count += 2;
        ushort id = BitConverter.ToUInt16(buffer.Array, buffer.Offset + count);
        count += 2;

        Action<PacketSession, ArraySegment<byte>, ushort> action = null;
        if (_onRecv.TryGetValue(id, out action))
            action.Invoke(session, buffer, id);
    }

    void MakePacket<T>(PacketSession session, ArraySegment<byte> buffer, ushort id) where T : IMessage, new()
    {
        T pkt = new T();
        pkt.MergeFrom(buffer.Array, buffer.Offset + 4, buffer.Count - 4);

        if (CustomHandler != null)
        {
            CustomHandler.Invoke(session, pkt, id);
        }
        else
        {
            Action<PacketSession, IMessage> action = null;
            if (_handler.TryGetValue(id, out action))
                action.Invoke(session, pkt);
        }

    }
    public Action<PacketSession, IMessage> GetPacketHandler(ushort id)
    {
        Action<PacketSession, IMessage> action = null;
        if (_handler.TryGetValue(id, out action))
            return action;
        return null;
    }
}