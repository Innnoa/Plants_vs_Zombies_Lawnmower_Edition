package com.lawnmower.network;

import com.lawnmower.Config;
import lawnmower.Message;

import java.io.EOFException;
import java.io.IOException;
import java.net.SocketTimeoutException;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

public final class ConnectivityCheck {
    private static final String TIMEOUT_PROPERTY = "lawnmower.connectivity.timeoutMs";
    private static final String TIMEOUT_ENV = "LAWNMOWER_CONNECTIVITY_TIMEOUT_MS";
    private static final long DEFAULT_STAGE_TIMEOUT_MS = 15_000L;
    private static final long UDP_SEND_INTERVAL_MS = 100L;
    private static final long UDP_POLL_TIMEOUT_MS = 250L;

    private final long stageTimeoutMs = resolveTimeoutMs();
    private final LinkedBlockingQueue<Message.Packet> udpPackets = new LinkedBlockingQueue<>();
    private final LinkedBlockingQueue<Throwable> udpErrors = new LinkedBlockingQueue<>();

    private TcpClient tcpClient;
    private UdpClient udpClient;
    private int playerId = -1;
    private int roomId = 0;
    private int inputSeq = 1;
    private String sessionToken = "";
    private int tcpFallbackSyncCount = 0;
    private int tcpFallbackDeltaCount = 0;

    public static void main(String[] args) {
        int exitCode = new ConnectivityCheck().run();
        System.exit(exitCode);
    }

    private int run() {
        System.out.println("[INFO] Connectivity target: " + Config.describeNetworkTarget());
        try {
            connectTcp();
            login();
            createRoom();
            startGame();
            verifyUdpSync();
            System.out.println("[PASS] Connectivity check completed.");
            return 0;
        } catch (Exception e) {
            System.err.println("[FAIL] Connectivity check failed: " + e.getMessage());
            return 1;
        } finally {
            closeQuietly();
        }
    }

    private void connectTcp() throws IOException {
        tcpClient = new TcpClient();
        tcpClient.connect(Config.SERVER_HOST, Config.SERVER_PORT);
        pass("TCP connect", "connected to " + Config.SERVER_HOST + ":" + Config.SERVER_PORT);
    }

    private void login() throws IOException {
        Message.C2S_Login request = Message.C2S_Login.newBuilder()
                .setPlayerName("connectivity_" + System.currentTimeMillis())
                .build();
        tcpClient.sendPacket(Message.MessageType.MSG_C2S_LOGIN, request);

        Message.Packet packet = waitForTcpPacket(
                Message.MessageType.MSG_S2C_LOGIN_RESULT,
                "login_result");
        Message.S2C_LoginResult result = Message.S2C_LoginResult.parseFrom(packet.getPayload());
        if (!result.getSuccess()) {
            throw new IOException("login rejected: " + result.getMessageLogin());
        }
        if (result.getPlayerId() <= 0) {
            throw new IOException("server returned invalid player_id");
        }
        if (result.getSessionToken().isBlank()) {
            throw new IOException("server did not provide session_token");
        }

        playerId = result.getPlayerId();
        sessionToken = result.getSessionToken();
        pass("Login", "player_id=" + playerId + ", token_length=" + sessionToken.length());
    }

    private void createRoom() throws IOException {
        Message.C2S_CreateRoom request = Message.C2S_CreateRoom.newBuilder()
                .setRoomName("connectivity_room")
                .setMaxPlayers(1)
                .build();
        tcpClient.sendPacket(Message.MessageType.MSG_C2S_CREATE_ROOM, request);

        Message.Packet packet = waitForTcpPacket(
                Message.MessageType.MSG_S2C_CREATE_ROOM_RESULT,
                "create_room");
        Message.S2C_CreateRoomResult result =
                Message.S2C_CreateRoomResult.parseFrom(packet.getPayload());
        if (!result.getSuccess()) {
            throw new IOException("create room failed: " + result.getMessageCreate());
        }
        if (result.getRoomId() <= 0) {
            throw new IOException("server returned invalid room_id");
        }

        roomId = result.getRoomId();
        pass("Create room", "room_id=" + roomId);
    }

    private void startGame() throws IOException {
        Message.C2S_StartGame request = Message.C2S_StartGame.newBuilder().build();
        tcpClient.sendPacket(Message.MessageType.MSG_C2S_START_GAME, request);

        Message.Packet packet = waitForTcpPacket(
                Message.MessageType.MSG_S2C_GAME_START,
                "game_start");
        Message.S2C_GameStart result = Message.S2C_GameStart.parseFrom(packet.getPayload());
        if (!result.getSuccess()) {
            throw new IOException("game start failed: " + result.getMessageStart());
        }
        if (result.getRoomId() != roomId) {
            throw new IOException(
                    "game_start room_id mismatch: expected " + roomId + ", got " + result.getRoomId());
        }

        pass("Start game", "room_id=" + roomId);
    }

    private void verifyUdpSync() throws IOException, InterruptedException {
        udpClient = new UdpClient();
        udpClient.setErrorConsumer(error -> {
            if (error != null) {
                udpErrors.offer(error);
            }
        });
        udpClient.start(Config.SERVER_HOST, Config.SERVER_UDP_PORT, packet -> {
            if (packet != null) {
                udpPackets.offer(packet);
            }
        });
        pass("UDP start", "listening for sync packets on udp port " + Config.SERVER_UDP_PORT);

        long deadline = System.currentTimeMillis() + stageTimeoutMs;
        long nextSendAt = 0L;
        while (System.currentTimeMillis() < deadline) {
            Throwable udpError = udpErrors.poll();
            if (udpError != null) {
                throw new IOException("udp receive error: " + udpError.getMessage(), udpError);
            }

            long now = System.currentTimeMillis();
            if (now >= nextSendAt) {
                sendUdpHello();
                nextSendAt = now + UDP_SEND_INTERVAL_MS;
            }

            drainTcpFallbackPackets();

            long waitMs = Math.max(1L, Math.min(UDP_POLL_TIMEOUT_MS, deadline - now));
            Message.Packet udpPacket = udpPackets.poll(waitMs, TimeUnit.MILLISECONDS);
            if (udpPacket == null) {
                continue;
            }

            Message.MessageType type = udpPacket.getMsgType();
            if (type != Message.MessageType.MSG_S2C_GAME_STATE_SYNC
                    && type != Message.MessageType.MSG_S2C_GAME_STATE_DELTA_SYNC) {
                continue;
            }

            int packetRoomId = extractRoomId(udpPacket);
            if (packetRoomId != roomId) {
                throw new IOException(
                        "udp packet room_id mismatch: expected " + roomId + ", got " + packetRoomId);
            }

            pass("UDP sync", "received " + type.name() + " for room " + roomId);
            return;
        }

        StringBuilder message = new StringBuilder("timed out waiting for UDP state sync");
        if (tcpFallbackSyncCount > 0 || tcpFallbackDeltaCount > 0) {
            message.append(" (observed TCP fallback syncs=")
                    .append(tcpFallbackSyncCount)
                    .append(", deltas=")
                    .append(tcpFallbackDeltaCount)
                    .append(")");
        }
        throw new IOException(message.toString());
    }

    private void sendUdpHello() throws IOException {
        Message.C2S_PlayerInput input = Message.C2S_PlayerInput.newBuilder()
                .setPlayerId(playerId)
                .setIsAttacking(true)
                .setInputSeq(inputSeq++)
                .setDeltaMs(50)
                .setSessionToken(sessionToken)
                .build();
        if (!udpClient.sendPlayerInput(input)) {
            throw new IOException("failed to send udp player input");
        }
    }

    private Message.Packet waitForTcpPacket(Message.MessageType expectedType,
                                            String stageName) throws IOException {
        long deadline = System.currentTimeMillis() + stageTimeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                Message.Packet packet = tcpClient.receivePacket();
                if (packet == null) {
                    throw new EOFException("tcp connection closed while waiting for " + expectedType.name());
                }
                if (packet.getMsgType() == expectedType) {
                    return packet;
                }
            } catch (SocketTimeoutException ignored) {
                // 继续等待同一阶段的目标包
            }
        }
        throw new IOException("timed out waiting for " + expectedType.name() + " during " + stageName);
    }

    private void drainTcpFallbackPackets() throws IOException {
        int drained = 0;
        while (tcpClient != null && tcpClient.availableBytes() > 0 && drained < 8) {
            Message.Packet packet = tcpClient.receivePacket();
            if (packet == null) {
                throw new EOFException("tcp connection closed while draining fallback packets");
            }

            Message.MessageType type = packet.getMsgType();
            if (type == Message.MessageType.MSG_S2C_GAME_STATE_SYNC) {
                tcpFallbackSyncCount++;
            } else if (type == Message.MessageType.MSG_S2C_GAME_STATE_DELTA_SYNC) {
                tcpFallbackDeltaCount++;
            } else if (type == Message.MessageType.MSG_S2C_GAME_OVER) {
                throw new IOException("server ended the game before UDP sync was observed");
            }
            drained++;
        }
    }

    private int extractRoomId(Message.Packet packet) throws IOException {
        Message.MessageType type = packet.getMsgType();
        if (type == Message.MessageType.MSG_S2C_GAME_STATE_SYNC) {
            return Message.S2C_GameStateSync.parseFrom(packet.getPayload()).getRoomId();
        }
        if (type == Message.MessageType.MSG_S2C_GAME_STATE_DELTA_SYNC) {
            return Message.S2C_GameStateDeltaSync.parseFrom(packet.getPayload()).getRoomId();
        }
        throw new IOException("unsupported packet type: " + type.name());
    }

    private void closeQuietly() {
        if (udpClient != null) {
            udpClient.stop();
            udpClient = null;
        }
        if (tcpClient != null) {
            try {
                tcpClient.close();
            } catch (IOException ignored) {
            }
            tcpClient = null;
        }
    }

    private void pass(String stage, String detail) {
        System.out.println("[PASS] " + stage + ": " + detail);
    }

    private long resolveTimeoutMs() {
        String rawValue = System.getProperty(TIMEOUT_PROPERTY);
        if (rawValue == null || rawValue.isBlank()) {
            rawValue = System.getenv(TIMEOUT_ENV);
        }
        if (rawValue == null || rawValue.isBlank()) {
            return DEFAULT_STAGE_TIMEOUT_MS;
        }

        try {
            long timeoutMs = Long.parseLong(rawValue.trim());
            if (timeoutMs > 0L) {
                return timeoutMs;
            }
        } catch (NumberFormatException ignored) {
        }
        return DEFAULT_STAGE_TIMEOUT_MS;
    }
}
