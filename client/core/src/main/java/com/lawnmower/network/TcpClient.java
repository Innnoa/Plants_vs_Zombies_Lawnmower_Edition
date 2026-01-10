package com.lawnmower.network;

import com.google.protobuf.MessageLite;

import com.lawnmower.Config;

import lawnmower.Message;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.*;
import java.net.*;
import java.io.IOException;

public class TcpClient {
    private static final Logger log = LoggerFactory.getLogger(TcpClient.class);
    private static final int SOCKET_TIMEOUT_MS = 500;
    private Socket socket;
    private DataInputStream dataIn;
    private DataOutputStream dataOut;

    public void connect(String host, int port) throws IOException {
//        socket = new Socket(host, port);
        socket = new Socket();
        socket.setTcpNoDelay(true);
        socket.setKeepAlive(true);
        socket.setReceiveBufferSize(128 * 1024);
        socket.setSoTimeout(SOCKET_TIMEOUT_MS);
        socket.connect(new InetSocketAddress(host, port), 3000);
        dataOut = new DataOutputStream(socket.getOutputStream());
        dataIn = new DataInputStream(socket.getInputStream());
        System.out.println("已连接到 " + host + ":" + port);
    }

    public void sendCreateRoom(String roomName, int maxPlayers) throws IOException {
        var msg = Message.C2S_CreateRoom.newBuilder()
                .setRoomName(roomName)
                .setMaxPlayers(maxPlayers)
                .build();
        sendPacket(Message.MessageType.MSG_C2S_CREATE_ROOM, msg);
    }

    public int getSocketTimeoutMs() {
        return SOCKET_TIMEOUT_MS;
    }

    public void sendGetRoomList() throws IOException {
        var msg = Message.C2S_GetRoomList.newBuilder().build();
        sendPacket(Message.MessageType.MSG_C2S_GET_ROOM_LIST, msg);
    }

    public void sendJoinRoom(int roomId) throws IOException {
        var msg = Message.C2S_JoinRoom.newBuilder()
                .setRoomId(roomId)
                .build();
        sendPacket(Message.MessageType.MSG_C2S_JOIN_ROOM, msg);
    }

    public void sendLeaveRoom() throws IOException {
        var msg = Message.C2S_LeaveRoom.newBuilder().build();
        sendPacket(Message.MessageType.MSG_C2S_LEAVE_ROOM, msg);
    }

    public void sendSetReady(boolean ready) throws IOException {
        var msg = Message.C2S_SetReady.newBuilder()
                .setIsReady(ready)
                .build();
        sendPacket(Message.MessageType.MSG_C2S_SET_READY, msg);
    }

    public void sendStartGame() throws IOException {
        var msg = Message.C2S_StartGame.newBuilder().build();
        sendPacket(Message.MessageType.MSG_C2S_START_GAME, msg);
    }


    private void writePacket(Message.Packet packet) throws IOException {
        byte[] data = packet.toByteArray();
        synchronized (dataOut) {
            dataOut.writeInt(data.length);
            dataOut.write(data);
            dataOut.flush();
        }
    }

    public void sendPacket(Message.Packet packet) throws IOException {
        writePacket(packet);
    }

    public void sendPacket(Message.MessageType type, MessageLite payload) throws IOException {
        Message.Packet packet = Message.Packet.newBuilder()
                .setMsgType(type)
                .setPayload(payload.toByteString())
                .build();
        writePacket(packet);
    }
    // ====== 新增方法：发送玩家输入 ======
    public void sendPlayerInput(Message.C2S_PlayerInput input) throws IOException {
        // 注意：input 已包含 player_id、方向、攻击状态等
        sendPacket(Message.MessageType.MSG_C2S_PLAYER_INPUT, input);
    }

    public Message.Packet receivePacket() throws IOException {
        try {
            int len = dataIn.readInt();
            byte[] data = new byte[len];
            dataIn.readFully(data);
            return Message.Packet.parseFrom(data);
        } catch (SocketTimeoutException timeout) {
            throw timeout;
        } catch (EOFException | SocketException e) {
            return null;
        }
    }
    public int availableBytes() {
        if (dataIn == null) return -1;
        try {
            return dataIn.available();
        } catch (IOException e) {
            return -1;
        }
    }


    public void close() throws IOException {
        if (socket == null) return;

        Message.Packet packet = Message.Packet.newBuilder()
                .setMsgType(Message.MessageType.MSG_C2S_REQUEST_QUIT)
                .setPayload(Config.byteString)
                .build();
        writePacket(packet);

        if (dataIn != null) {
            dataIn.close();
            dataIn = null;
        }
        if (dataOut != null) {
            dataOut.close();
            dataOut = null;
        }
        socket.close();
        socket = null;
    }

}
