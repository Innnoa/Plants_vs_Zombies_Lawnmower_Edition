package com.lawnmower;

import com.badlogic.gdx.Game;
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.scenes.scene2d.ui.Skin;
import com.google.protobuf.ByteString;
import com.google.protobuf.MessageLite;
import com.google.protobuf.UnknownFieldSet;

import com.lawnmower.network.TcpClient;
import com.lawnmower.network.UdpClient;
import com.lawnmower.screens.*;
import com.lawnmower.ui.PvzSkin;

import lawnmower.Message;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.net.SocketTimeoutException;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

public class Main extends Game {
    private static final Logger log = LoggerFactory.getLogger(Main.class);
    private static final int LOGIN_RESULT_SESSION_TOKEN_FIELD_NUMBER = 4;
    // 瀹㈡埛绔瀯寤烘爣璇嗭紝鐢ㄤ簬纭鐗堟湰
    private static final String CLIENT_BUILD_VERSION = "2026-01-24-rot-log";
    private static final long RECONNECT_GRACE_MS = 15_000L;
    private static final long RECONNECT_RETRY_INTERVAL_MS = 1000L;
    private enum ReconnectState {
        IDLE,
        RECONNECTING,
        AWAITING_SNAPSHOT
    }
    private Skin skin;
    private TcpClient tcpClient;
    private UdpClient udpClient;
    private String playerName = "Player";
    private int playerId = -1; // 鏈櫥褰曟椂涓?-1
    private String sessionToken = "";
    private long lastSocketWaitLogMs = 0L;
    private long lastUdpSyncTick = -1L;
    private long lastUdpServerTimeMs = -1L;
    private final AtomicBoolean roomReturnRequested = new AtomicBoolean(false);
    private volatile Message.S2C_RoomUpdate pendingRoomUpdate;
    private int currentRoomId = 0;
    private long lastServerTick = -1L;
    private int lastConfirmedInputSeq = 0;
    private volatile ReconnectState reconnectState = ReconnectState.IDLE;
    private long reconnectStartMs = 0L;
    private int reconnectAttempts = 0;
    private ScheduledExecutorService reconnectExecutor;
    private boolean allowReconnect = true;

    private final AtomicBoolean networkRunning = new AtomicBoolean(false);
    private final AtomicBoolean disposed = new AtomicBoolean(false);
    private Thread networkThread;

    @Override
    public void create() {
        Gdx.app.log("ClientVersion", "瀹㈡埛绔増鏈? " + CLIENT_BUILD_VERSION);
        //浣跨敤鑷畾涔?PVZ 椋庢牸鐨偆
        skin = PvzSkin.create();
        
        // 鍒濆鍖?TCP 瀹㈡埛绔紙杩炴帴鏈湴鏈嶅姟鍣級
        try {
            tcpClient = new TcpClient();
            tcpClient.connect(Config.SERVER_HOST, Config.SERVER_PORT);
            allowReconnect = true;
            reconnectState = ReconnectState.IDLE;
            log.info("Connected to server {}:{}", Config.SERVER_HOST, Config.SERVER_PORT);
            startNetworkThread();
        } catch (IOException e) {
            log.error("Failed to connect to server", e);
            setScreen(new MainMenuScreen(this, skin));
        }
        setScreen(new MainMenuScreen(Main.this,skin));
    }

    private void startNetworkThread() {
        if (networkRunning.get()) return;

        networkRunning.set(true);
        networkThread = new Thread(() -> {
            while (networkRunning.get() && !Thread.currentThread().isInterrupted()) {
                try {
                    // 闃诲绛夊緟鏈嶅姟鍣ㄦ秷鎭?
                    long beforeRead = System.currentTimeMillis();
                    Message.Packet packet = tcpClient.receivePacket();
                    long afterRead = System.currentTimeMillis();
                    if (packet == null) break; // 杩炴帴鍏抽棴

                    // 瑙ｆ瀽娑堟伅绫诲瀷骞跺垎鍙?
                    Message.MessageType type = packet.getMsgType();
                    Object payload = null;


                    Gdx.app.log("褰撳墠鎺ュ彈",type.name());
                    switch (type) {
                        case MSG_S2C_LOGIN_RESULT:
                            payload = Message.S2C_LoginResult.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_RECONNECT_ACK:
                            payload = Message.S2C_ReconnectAck.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_ROOM_LIST:
                            payload = Message.S2C_RoomList.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_ROOM_UPDATE:
                            payload = Message.S2C_RoomUpdate.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_GAME_START:
                            payload = Message.S2C_GameStart.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_GAME_STATE_SYNC:
                            payload = Message.S2C_GameStateSync.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_GAME_STATE_DELTA_SYNC:
                            payload = Message.S2C_GameStateDeltaSync.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_PLAYER_HURT:
                            payload = Message.S2C_PlayerHurt.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_ENEMY_DIED:
                            payload = Message.S2C_EnemyDied.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_PLAYER_LEVEL_UP:
                            payload = Message.S2C_PlayerLevelUp.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_DROPPED_ITEM:
                            payload = Message.S2C_DroppedItem.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_GAME_OVER:
                            payload = Message.S2C_GameOver.parseFrom(packet.getPayload());
                            break;
                        case MSG_S2C_PROJECTILE_SPAWN:
                            payload = Message.S2C_ProjectileSpawn.parseFrom(packet.getPayload());
                            break;
        case MSG_S2C_PROJECTILE_DESPAWN:
            payload = Message.S2C_ProjectileDespawn.parseFrom(packet.getPayload());
            break;
        case MSG_S2C_ENEMY_ATTACK_STATE_SYNC:
            payload = Message.S2C_EnemyAttackStateSync.parseFrom(packet.getPayload());
            break;
        case MSG_S2C_UPGRADE_REQUEST:
            payload = Message.S2C_UpgradeRequest.parseFrom(packet.getPayload());
            break;
        case MSG_S2C_UPGRADE_OPTIONS:
            payload = Message.S2C_UpgradeOptions.parseFrom(packet.getPayload());
            break;
        case MSG_S2C_UPGRADE_SELECT_ACK:
            payload = Message.S2C_UpgradeSelectAck.parseFrom(packet.getPayload());
            break;
                        // 鍏朵粬鏈潵娑堟伅鍙户缁坊鍔?
                        default:
                            Gdx.app.log("NET", "Unknown message type: " + type);
                            continue;
                    }

                    // 閫氱煡涓荤嚎绋嬪鐞嗭紙UI 鎿嶄綔蹇呴』鍦ㄦ覆鏌撶嚎绋嬶級
                    handleNetworkMessage(type, payload);
                } catch (SocketTimeoutException e) {
                    long now = System.currentTimeMillis();
                    if (now - lastSocketWaitLogMs > 1000L) {
                        lastSocketWaitLogMs = now;
                    }
                    continue;
                } catch (IOException e) {
                    if (networkRunning.get()) {
                        Gdx.app.log("NET", "Network error: " + e.getMessage());
                    }
                    break;
                } catch (Exception e) {
                    Gdx.app.log("NET", "Error parsing packet", e);
                    break;
                }
            }

            // 绾跨▼閫€鍑?
            networkRunning.set(false);
            stopUdpClient();
            handleConnectionClosed();
        }, "NetworkThread");

        networkThread.setDaemon(true); // 闅忎富绾跨▼閫€鍑鸿€岀粓姝?
        networkThread.start();
    }

    // 鈥斺€斺€斺€斺€斺€斺€斺€?鍏叡璁块棶鏂规硶 鈥斺€斺€斺€斺€斺€斺€斺€?

    private void processUdpPacket(Message.Packet packet) {
        if (packet == null) {
            return;
        }
        Message.MessageType type = packet.getMsgType();
        try {
            Object payload = parsePacketPayload(packet, type);
            if (payload == null) {
                return;
            }
            if (type == Message.MessageType.MSG_S2C_GAME_STATE_SYNC) {
                Message.S2C_GameStateSync sync = (Message.S2C_GameStateSync) payload;
                if (shouldDropUdpSync(sync)) {
                    return;
                }
            } else if (type == Message.MessageType.MSG_S2C_GAME_STATE_DELTA_SYNC) {
                Message.S2C_GameStateDeltaSync delta = (Message.S2C_GameStateDeltaSync) payload;
                if (shouldDropUdpDelta(delta)) {
                    return;
                }
            }
            handleNetworkMessage(type, payload);
        } catch (IOException e) {
            Gdx.app.log("UDP", "Failed to parse UDP packet: " + e.getMessage());
        }
    }

    private void handleConnectionClosed() {
        if (!allowReconnect || !shouldAttemptReconnect()) {
            Gdx.app.postRunnable(this::resetToMainMenu);
            return;
        }
        beginReconnectLoop();
    }

    private boolean shouldAttemptReconnect() {
        return playerId > 0 && sessionToken != null && !sessionToken.isBlank();
    }

    private Object parsePacketPayload(Message.Packet packet, Message.MessageType type) throws IOException {
        switch (type) {
            case MSG_S2C_LOGIN_RESULT:
                return Message.S2C_LoginResult.parseFrom(packet.getPayload());
            case MSG_S2C_RECONNECT_ACK:
                return Message.S2C_ReconnectAck.parseFrom(packet.getPayload());
            case MSG_S2C_ROOM_LIST:
                return Message.S2C_RoomList.parseFrom(packet.getPayload());
            case MSG_S2C_CREATE_ROOM_RESULT:
                return Message.S2C_CreateRoomResult.parseFrom(packet.getPayload());
            case MSG_S2C_ROOM_UPDATE:
                return Message.S2C_RoomUpdate.parseFrom(packet.getPayload());
            case MSG_S2C_GAME_START:
                return Message.S2C_GameStart.parseFrom(packet.getPayload());
            case MSG_S2C_GAME_STATE_SYNC:
                return Message.S2C_GameStateSync.parseFrom(packet.getPayload());
            case MSG_S2C_GAME_STATE_DELTA_SYNC:
                return Message.S2C_GameStateDeltaSync.parseFrom(packet.getPayload());
            case MSG_S2C_PLAYER_HURT:
                return Message.S2C_PlayerHurt.parseFrom(packet.getPayload());
            case MSG_S2C_ENEMY_DIED:
                return Message.S2C_EnemyDied.parseFrom(packet.getPayload());
            case MSG_S2C_PLAYER_LEVEL_UP:
                return Message.S2C_PlayerLevelUp.parseFrom(packet.getPayload());
            case MSG_S2C_DROPPED_ITEM:
                return Message.S2C_DroppedItem.parseFrom(packet.getPayload());
            case MSG_S2C_GAME_OVER:
                return Message.S2C_GameOver.parseFrom(packet.getPayload());
            case MSG_S2C_SET_READY_RESULT:
                return Message.S2C_SetReadyResult.parseFrom(packet.getPayload());
            case MSG_S2C_PROJECTILE_SPAWN:
                return Message.S2C_ProjectileSpawn.parseFrom(packet.getPayload());
        case MSG_S2C_PROJECTILE_DESPAWN:
            return Message.S2C_ProjectileDespawn.parseFrom(packet.getPayload());
        case MSG_S2C_ENEMY_ATTACK_STATE_SYNC:
            return Message.S2C_EnemyAttackStateSync.parseFrom(packet.getPayload());
        case MSG_S2C_UPGRADE_REQUEST:
            return Message.S2C_UpgradeRequest.parseFrom(packet.getPayload());
        case MSG_S2C_UPGRADE_OPTIONS:
            return Message.S2C_UpgradeOptions.parseFrom(packet.getPayload());
        case MSG_S2C_UPGRADE_SELECT_ACK:
            return Message.S2C_UpgradeSelectAck.parseFrom(packet.getPayload());
        default:
            Gdx.app.log("NET", "Unknown message type: " + type);
            return null;
        }
    }

    private boolean shouldDropUdpSync(Message.S2C_GameStateSync sync) {
        Message.Timestamp syncTime = sync.hasSyncTime() ? sync.getSyncTime() : null;
        long tick = extractSyncTick(syncTime);
        long serverTimeMs = extractServerTime(syncTime);
        return shouldDropUdpState(tick, serverTimeMs);
    }

    private boolean shouldDropUdpDelta(Message.S2C_GameStateDeltaSync delta) {
        Message.Timestamp syncTime = delta.hasSyncTime() ? delta.getSyncTime() : null;
        long tick = extractSyncTick(syncTime);
        long serverTimeMs = extractServerTime(syncTime);
        return shouldDropUdpState(tick, serverTimeMs);
    }

    private boolean shouldDropUdpState(long tick, long serverTimeMs) {
        if (tick >= 0) {
            if (lastUdpSyncTick >= 0 && Long.compareUnsigned(tick, lastUdpSyncTick) <= 0) {
                return true;
            }
            lastUdpSyncTick = tick;
            if (serverTimeMs > lastUdpServerTimeMs) {
                lastUdpServerTimeMs = serverTimeMs;
            }
            return false;
        }

        long effectiveServerTime = serverTimeMs > 0L ? serverTimeMs : System.currentTimeMillis();
        if (lastUdpServerTimeMs > 0L && effectiveServerTime <= lastUdpServerTimeMs) {
            return true;
        }
        lastUdpServerTimeMs = effectiveServerTime;
        return false;
    }

    private long extractSyncTick(Message.Timestamp syncTime) {
        return syncTime != null ? Integer.toUnsignedLong(syncTime.getTick()) : -1L;
    }

    private long extractServerTime(Message.Timestamp syncTime) {
        if (syncTime == null) {
            return -1L;
        }
        long serverTime = syncTime.getServerTime();
        return serverTime > 0L ? serverTime : -1L;
    }


    private synchronized void prepareUdpClientForMatch() {
        if (playerId <= 0) {
            return;
        }
        try {
            startUdpClientIfNeeded();
            lastUdpSyncTick = -1L;
            lastUdpServerTimeMs = -1L;
            sendInitialUdpHello();
        } catch (IOException e) {
            log.error("Failed to initialize UDP client", e);
        }
    }

    private synchronized void startUdpClientIfNeeded() throws IOException {
        if (udpClient == null) {
            udpClient = new UdpClient();
            udpClient.setErrorConsumer(err -> {
                if (err != null) {
                    Gdx.app.log("UDP", "UDP error: " + err.getMessage());
                }
            });
        }
        if (!udpClient.isRunning()) {
            udpClient.start(Config.SERVER_HOST, Config.SERVER_UDP_PORT, this::processUdpPacket);
        }
    }

    private synchronized void stopUdpClient() {
        if (udpClient == null) {
            return;
        }
        udpClient.stop();
        udpClient = null;
    }

    private void beginReconnectLoop() {
        if (reconnectState != ReconnectState.IDLE) {
            return;
        }
        reconnectState = ReconnectState.RECONNECTING;
        reconnectStartMs = System.currentTimeMillis();
        reconnectAttempts = 0;
        notifyGameScreenReconnectStart();
        scheduleReconnectAttempt(0L);
    }

    private void scheduleReconnectAttempt(long delayMs) {
        if (reconnectExecutor == null || reconnectExecutor.isShutdown()) {
            reconnectExecutor = Executors.newSingleThreadScheduledExecutor();
        }
        reconnectExecutor.schedule(this::attemptReconnectOnce, delayMs, TimeUnit.MILLISECONDS);
    }

    private void attemptReconnectOnce() {
        if (reconnectState != ReconnectState.RECONNECTING) {
            return;
        }
        long elapsed = System.currentTimeMillis() - reconnectStartMs;
        if (elapsed >= RECONNECT_GRACE_MS) {
            handleReconnectFailure("timeout");
            return;
        }
        reconnectAttempts++;
        try {
            reconnectTcpAndSendRequest();
        } catch (IOException e) {
            log.warn("Reconnect attempt {} failed: {}", reconnectAttempts, e.getMessage());
            scheduleReconnectAttempt(RECONNECT_RETRY_INTERVAL_MS);
        }
    }

    private void reconnectTcpAndSendRequest() throws IOException {
        if (tcpClient != null) {
            try {
                tcpClient.close();
            } catch (IOException ignore) {
            }
        }
        tcpClient = new TcpClient();
        tcpClient.connect(Config.SERVER_HOST, Config.SERVER_PORT);
        allowReconnect = true;
        startNetworkThread();
        sendReconnectRequest();
    }

    private void sendReconnectRequest() throws IOException {
        if (tcpClient == null) {
            throw new IOException("TCP client not ready");
        }
        Message.C2S_ReconnectRequest request = Message.C2S_ReconnectRequest.newBuilder()
                .setPlayerId(Math.max(0, playerId))
                .setRoomId(Math.max(0, currentRoomId))
                .setSessionToken(sessionToken == null ? "" : sessionToken)
                .setLastInputSeq(Math.max(0, lastConfirmedInputSeq))
                .setLastServerTick((int) Math.max(0, lastServerTick))
                .build();
        tcpClient.sendReconnectRequest(request);
    }

    private void handleReconnectFailure(String reason) {
        log.warn("Reconnect failed: {}", reason);
        stopReconnectExecutor();
        reconnectState = ReconnectState.IDLE;
        Gdx.app.postRunnable(() -> {
            notifyGameScreenReconnectFinish();
            resetToMainMenu();
        });
    }

    private void stopReconnectExecutor() {
        if (reconnectExecutor != null) {
            reconnectExecutor.shutdownNow();
            reconnectExecutor = null;
        }
    }

    private void notifyGameScreenReconnectStart() {
        Gdx.app.postRunnable(() -> {
            if (getScreen() instanceof GameScreen gameScreen) {
                gameScreen.enterReconnectHold();
            }
        });
    }

    private void notifyGameScreenReconnectFinish() {
        if (Gdx.app == null) {
            return;
        }
        Gdx.app.postRunnable(() -> {
            if (getScreen() instanceof GameScreen gameScreen) {
                gameScreen.exitReconnectHold();
            }
        });
    }

    private void handleReconnectAck(Message.S2C_ReconnectAck ack) {
        if (ack == null) {
            handleReconnectFailure("empty_ack");
            return;
        }
        if (!ack.getSuccess()) {
            handleReconnectFailure(ack.getMessage());
            return;
        }
        stopReconnectExecutor();
        allowReconnect = true;
        lastServerTick = Integer.toUnsignedLong(ack.getServerTick());
        boolean isPaused = ack.getIsPaused();
        if (!ack.getSessionToken().isBlank()) {
            setSessionToken(ack.getSessionToken());
        }
        updateActiveRoomId((int) ack.getRoomId());
        setPlayerId((int) ack.getPlayerId());
        if (ack.getIsPlaying()) {
            reconnectState = ReconnectState.AWAITING_SNAPSHOT;
            Gdx.app.postRunnable(() -> {
                if (!(getScreen() instanceof GameScreen)) {
                    setScreen(new GameScreen(Main.this));
                }
                if (getScreen() instanceof GameScreen gameScreen) {
                    gameScreen.resetWorldStateForFullSync("reconnect_ack");
                    gameScreen.setServerPaused(isPaused);
                }
            });
            prepareUdpClientForMatch();
        } else {
            reconnectState = ReconnectState.IDLE;
            notifyGameScreenReconnectFinish();
            stopUdpClient();
            Gdx.app.postRunnable(() -> {
                if (getScreen() instanceof GameScreen gameScreen) {
                    gameScreen.setServerPaused(false);
                }
                setScreen(new GameRoomScreen(Main.this, skin));
            });
        }
    }

    public synchronized void onReconnectSnapshotApplied() {
        if (reconnectState != ReconnectState.AWAITING_SNAPSHOT) {
            return;
        }
        reconnectState = ReconnectState.IDLE;
        notifyGameScreenReconnectFinish();
    }

    /*
    灏嗚緭鍏ュ彂閫佺粰鏈嶅姟绔?
     */
    public boolean trySendPlayerInput(Message.C2S_PlayerInput input) {
        if (input == null) {
            return false;
        }

        Message.C2S_PlayerInput withToken = attachSessionToken(input, sessionToken);
        boolean hasToken = sessionToken != null && !sessionToken.isBlank();

        if (udpClient != null && udpClient.isRunning() && hasToken) {
            if (udpClient.sendPlayerInput(withToken)) {
                return true;
            }
        }
        if (tcpClient != null) {
            try {
                tcpClient.sendPlayerInput(withToken);
                return true;
            } catch (IOException e) {
                log.warn("Failed to send input via TCP fallback", e);
            }
        }
        return false;
    }

    public void requestFullGameStateSync() {
        requestFullGameStateSync(null);
    }

    /**
     * 璇锋眰鍏ㄩ噺鍚屾
     * @param reason
     */
    public void requestFullGameStateSync(String reason) {
        if (tcpClient == null) {
            return;
        }
        try {
            Message.C2S_Heartbeat heartbeat = Message.C2S_Heartbeat.newBuilder()
                    .setTimestamp(System.currentTimeMillis())
                    .build();
            tcpClient.sendPacket(Message.MessageType.MSG_C2S_HEARTBEAT, heartbeat);
            String tag = reason == null ? "unknown" : reason;
            Gdx.app.log("NET", "Requested full game state sync (" + tag + ")");
        } catch (IOException e) {
            log.warn("Failed to request game state sync", e);
        }
    }

    public boolean sendUpgradeRequestAck(int roomId) {
        if (!isUpgradeRoomValid(roomId)) {
            return false;
        }
        Message.C2S_UpgradeRequestAck ack = Message.C2S_UpgradeRequestAck.newBuilder()
                .setRoomId(roomId)
                .setPlayerId(playerId)
                .build();
        return sendUpgradePacket(Message.MessageType.MSG_C2S_UPGRADE_REQUEST_ACK, ack);
    }

    public boolean sendUpgradeOptionsAck(int roomId) {
        if (!isUpgradeRoomValid(roomId)) {
            return false;
        }
        Message.C2S_UpgradeOptionsAck ack = Message.C2S_UpgradeOptionsAck.newBuilder()
                .setRoomId(roomId)
                .setPlayerId(playerId)
                .build();
        return sendUpgradePacket(Message.MessageType.MSG_C2S_UPGRADE_OPTIONS_ACK, ack);
    }

    public boolean sendUpgradeSelect(int roomId, int optionIndex) {
        if (!isUpgradeRoomValid(roomId)) {
            return false;
        }
        Message.C2S_UpgradeSelect select = Message.C2S_UpgradeSelect.newBuilder()
                .setRoomId(roomId)
                .setPlayerId(playerId)
                .setOptionIndex(optionIndex)
                .build();
        return sendUpgradePacket(Message.MessageType.MSG_C2S_UPGRADE_SELECT, select);
    }

    public boolean sendUpgradeRefreshRequest(int roomId) {
        if (!isUpgradeRoomValid(roomId)) {
            return false;
        }
        Message.C2S_UpgradeRefreshRequest request = Message.C2S_UpgradeRefreshRequest.newBuilder()
                .setRoomId(roomId)
                .setPlayerId(playerId)
                .build();
        return sendUpgradePacket(Message.MessageType.MSG_C2S_UPGRADE_REFRESH_REQUEST, request);
    }

    private boolean isUpgradeRoomValid(int roomId) {
        return tcpClient != null && playerId > 0 && roomId > 0;
    }

    private boolean sendUpgradePacket(Message.MessageType type, MessageLite payload) {
        if (tcpClient == null || payload == null) {
            return false;
        }
        try {
            tcpClient.sendPacket(type, payload);
            return true;
        } catch (IOException e) {
            log.warn("Failed to send {}: {}", type, e.getMessage());
            return false;
        }
    }

    public void requestReturnToRoomFromGameOver() {
        roomReturnRequested.set(true);
        Gdx.app.postRunnable(this::tryProcessCachedRoomUpdate);
    }

    private void tryProcessCachedRoomUpdate() {
        if (!roomReturnRequested.get()) {
            return;
        }
        Message.S2C_RoomUpdate cached = pendingRoomUpdate;
        if (cached == null) {
            return;
        }
        deliverRoomUpdate(cached);
    }

    public Skin getSkin() {
        return skin;
    }

    public TcpClient getTcpClient() {
        return tcpClient;
    }

    public String getPlayerName() {
        return playerName;
    }

    public void setPlayerName(String name) {
        this.playerName = name;
    }

    public int getPlayerId() {
        return playerId;
    }

    public void setPlayerId(int id) {
        this.playerId = id;
    }

    public String getSessionToken() {
        return sessionToken;
    }

    private void setSessionToken(String token) {
        this.sessionToken = token == null ? "" : token;
    }

    private static String extractSessionToken(Message.S2C_LoginResult result) {
        if (result == null) {
            return "";
        }
        String directToken = result.getSessionToken();
        if (directToken != null && !directToken.isBlank()) {
            return directToken;
        }
        UnknownFieldSet unknownFields = result.getUnknownFields();
        if (unknownFields == null) {
            return "";
        }
        UnknownFieldSet.Field tokenField =
                unknownFields.getField(LOGIN_RESULT_SESSION_TOKEN_FIELD_NUMBER);
        if (tokenField == null || tokenField.getLengthDelimitedList().isEmpty()) {
            return "";
        }
        ByteString bytes = tokenField.getLengthDelimitedList().get(0);
        return bytes == null ? "" : bytes.toStringUtf8();
    }

    private static Message.C2S_PlayerInput attachSessionToken(Message.C2S_PlayerInput input,
                                                             String token) {
        if (input == null) {
            return null;
        }
        if (token == null || token.isBlank()) {
            return input;
        }

        return input.toBuilder()
                .setSessionToken(token)
                .build();
    }

    public synchronized void updateConfirmedInputSeq(int seq) {
        if (seq < 0) {
            return;
        }
        if (Integer.compareUnsigned(seq, lastConfirmedInputSeq) > 0) {
            lastConfirmedInputSeq = seq;
        }
    }

    public synchronized void updateServerTick(long tick) {
        if (tick < 0L) {
            return;
        }
        if (Long.compareUnsigned(tick, lastServerTick) > 0) {
            lastServerTick = tick;
        }
    }

    public synchronized boolean isAwaitingReconnectSnapshot() {
        return reconnectState == ReconnectState.AWAITING_SNAPSHOT;
    }

    public synchronized void updateActiveRoomId(int roomId) {
        if (roomId <= 0) {
            return;
        }
        currentRoomId = roomId;
    }

    // 鈥斺€斺€斺€斺€斺€斺€斺€?缃戠粶娑堟伅澶勭悊鍏ュ彛锛堢敱缃戠粶绾跨▼璋冪敤锛?鈥斺€斺€斺€斺€斺€斺€斺€?

    public void handleNetworkMessage(Message.MessageType type, Object message) {
        Gdx.app.postRunnable(() -> {
            switch (type) {
                case MSG_S2C_LOGIN_RESULT:
                    Message.S2C_LoginResult result = (Message.S2C_LoginResult) message;
                    if (result.getSuccess()) {
                        setPlayerId(result.getPlayerId());
                        setSessionToken(extractSessionToken(result));
                        if (sessionToken.isBlank()) {
                            log.warn("Login succeeded but server did not provide session_token; UDP input may be rejected");
                        } else {
                            log.debug("Received session_token (length={})", sessionToken.length());
                        }
                        // 鐧诲綍鎴愬姛锛岃烦杞埌鎴块棿鍒楄〃
                        setScreen(new RoomListScreen(Main.this, skin));
                    } else {
                        setPlayerId(-1);
                        setSessionToken("");
                        // 鐧诲綍澶辫触锛氳繑鍥炰富鑿滃崟骞舵彁绀?
                        if (getScreen() instanceof MainMenuScreen mainMenu) {
                            mainMenu.showError("鐧诲綍澶辫触: " + result.getMessageLogin());
                        } else {
                            setScreen(new MainMenuScreen(Main.this, skin));
                            ((MainMenuScreen) getScreen()).showError("鐧诲綍澶辫触: " + result.getMessageLogin());
                        }
                    }
                    break;

                case MSG_S2C_RECONNECT_ACK:
                    handleReconnectAck((Message.S2C_ReconnectAck) message);
                    break;

                case MSG_S2C_CREATE_ROOM_RESULT:
                    Message.S2C_CreateRoomResult createRoomResult = (Message.S2C_CreateRoomResult) message;
                    if (getScreen() instanceof RoomListScreen roomListScreen) {
                        roomListScreen.onCreateRoomResult(createRoomResult);
                    } else if (!createRoomResult.getSuccess()) {
                        log.warn("Create room failed (roomId={}): {}", createRoomResult.getRoomId(), createRoomResult.getMessageCreate());
                    }
                    break;

                case MSG_S2C_ROOM_LIST:
                    if (getScreen() instanceof RoomListScreen roomList) {
                        Message.S2C_RoomList list = (Message.S2C_RoomList) message;
                        roomList.onRoomListReceived(list.getRoomsList());
                    }
                    break;

                case MSG_S2C_ROOM_UPDATE:
                    Message.S2C_RoomUpdate update = (Message.S2C_RoomUpdate) message;
                    pendingRoomUpdate = update;
                    if (getScreen() instanceof GameScreen) {
                        log.debug("Ignore ROOM_UPDATE while in game view (roomId={})",
                                update.getRoomId());
                        break;
                    }
                    if (getScreen() instanceof GameOverScreen && !roomReturnRequested.get()) {
                        log.debug("Hold ROOM_UPDATE while in game over view (roomId={})",
                                update.getRoomId());
                        break;
                    }
                    deliverRoomUpdate(update);
                    break;
                case MSG_S2C_GAME_START:
                    prepareUdpClientForMatch();
                    boolean createdGameScreen = false;
                    if (getScreen() instanceof GameRoomScreen) {
                        // 閸掑洦宕查崚鐗堢埗閹村繐婧€閺?
                        setScreen(new GameScreen(Main.this));
                        createdGameScreen = true;
                    }
                    if (getScreen() instanceof GameScreen gameScreenStart) {
                        if (createdGameScreen) {
                            gameScreenStart.expectFullGameStateSync("game_start");
                        } else {
                            gameScreenStart.resetWorldStateForFullSync("game_start");
                        }
                    }
                    break;
                case MSG_S2C_GAME_STATE_SYNC:
                    // 灏嗗悓姝ユ暟鎹浆鍙戠粰 GameScreen锛堝鏋滃綋鍓嶆槸娓告垙鐣岄潰锛?
                    if (getScreen() instanceof GameScreen gameScreen) {
                        Message.S2C_GameStateSync sync = (Message.S2C_GameStateSync) message;
                        gameScreen.onGameStateReceived(sync);
                    }
                    break;

                case MSG_S2C_GAME_STATE_DELTA_SYNC:
                    if (getScreen() instanceof GameScreen gameScreenDelta) {
                        Message.S2C_GameStateDeltaSync delta = (Message.S2C_GameStateDeltaSync) message;
                        gameScreenDelta.onGameStateDeltaReceived(delta);
                    }
                    break;

                case MSG_S2C_SET_READY_RESULT:
                    Message.S2C_SetReadyResult readyResult = (Message.S2C_SetReadyResult) message;
                    if (getScreen() instanceof GameRoomScreen gameRoomScreen) {
                        gameRoomScreen.onSetReadyResult(readyResult);
                    } else if (!readyResult.getSuccess()) {
                        log.warn("Set ready failed outside GameRoomScreen: {}", readyResult.getMessageReady());
                    }
                    break;

                case MSG_S2C_PLAYER_HURT:
                case MSG_S2C_ENEMY_DIED:
                case MSG_S2C_PLAYER_LEVEL_UP:
                case MSG_S2C_DROPPED_ITEM:
                case MSG_S2C_GAME_OVER:
                case MSG_S2C_PROJECTILE_SPAWN:
                case MSG_S2C_PROJECTILE_DESPAWN:
        case MSG_S2C_ENEMY_ATTACK_STATE_SYNC:
            // 鏆傛椂鍙墦鏃ュ織锛屽悗缁敱 GameScreen 澶勭悊
            Gdx.app.log("GAME_EVENT", "Received game event: " + type);
            if (getScreen() instanceof GameScreen gameScreen) {
                gameScreen.onGameEvent(type, message);
            }
            break;
        case MSG_S2C_UPGRADE_REQUEST:
            if (getScreen() instanceof GameScreen requestScreen) {
                requestScreen.onUpgradeRequest((Message.S2C_UpgradeRequest) message);
            }
            break;
        case MSG_S2C_UPGRADE_OPTIONS:
            if (getScreen() instanceof GameScreen optionsScreen) {
                optionsScreen.onUpgradeOptions((Message.S2C_UpgradeOptions) message);
            }
            break;
        case MSG_S2C_UPGRADE_SELECT_ACK:
            if (getScreen() instanceof GameScreen ackScreen) {
                ackScreen.onUpgradeSelectAck((Message.S2C_UpgradeSelectAck) message);
            }
            break;
        default:
            Gdx.app.log("NET", "Unhandled message type: " + type);
        }
    });
}

    private void deliverRoomUpdate(Message.S2C_RoomUpdate update) {
        if (update == null) {
            return;
        }
        if (!(getScreen() instanceof GameRoomScreen)) {
            setScreen(new GameRoomScreen(Main.this, skin));
        }
        if (getScreen() instanceof GameRoomScreen gameRoom) {
            gameRoom.onRoomUpdate(update.getRoomId(), update.getPlayersList());
            pendingRoomUpdate = null;
            roomReturnRequested.set(false);
        }
    }

    @Override
    public void dispose() {
        if (!disposed.compareAndSet(false, true)) {
            return;
        }

        shutdownNetworking();

        if (skin != null) skin.dispose();
        super.dispose();
    }

    public void requestExit() {
        shutdownNetworking();
        if (Gdx.app != null) {
            Gdx.app.postRunnable(() -> Gdx.app.exit());
        }
    }

    private void shutdownNetworking() {
        networkRunning.set(false);
        allowReconnect = false;

        if (tcpClient != null) {
            try {
                tcpClient.close();
            } catch (IOException e) {
                log.warn("Error closing TCP client", e);
            } finally {
                tcpClient = null;
            }
        }

        stopUdpClient();

        if (networkThread != null) {
            networkThread.interrupt();
            networkThread = null;
        }
    }

    private void sendInitialUdpHello() {
        if (playerId <= 0) {
            return;
        }
        if (sessionToken == null || sessionToken.isBlank()) {
            log.debug("Skipping UDP hello because session token is missing");
            return;
        }
        UdpClient client = this.udpClient;
        if (client == null || !client.isRunning()) {
            return;
        }
        Message.C2S_PlayerInput hello = Message.C2S_PlayerInput.newBuilder()
                .setPlayerId(playerId)
                .setDeltaMs(0)
                .build();
        Message.C2S_PlayerInput withToken = attachSessionToken(hello, sessionToken);
        if (client.sendPlayerInput(withToken)) {
            log.debug("Sent UDP hello to register endpoint (playerId={})", playerId);
        } else {
            log.warn("Failed to send UDP hello; UDP sync may be delayed until player input occurs");
        }
    }

    private void resetToMainMenu() {
        stopReconnectExecutor();
        reconnectState = ReconnectState.IDLE;
        currentRoomId = 0;
        lastServerTick = -1L;
        lastConfirmedInputSeq = 0;
        setPlayerId(-1);
        setSessionToken("");
        if (!(getScreen() instanceof MainMenuScreen)) {
            setScreen(new MainMenuScreen(Main.this, skin));
        }
    }
}



