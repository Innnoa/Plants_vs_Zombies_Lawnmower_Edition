package com.lawnmower;

import com.badlogic.gdx.Game;
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.scenes.scene2d.ui.Skin;

import com.lawnmower.network.TcpClient;
import com.lawnmower.network.UdpClient;
import com.lawnmower.screens.GameRoomScreen;
import com.lawnmower.screens.GameScreen;
import com.lawnmower.screens.MainMenuScreen;
import com.lawnmower.screens.RoomListScreen;
import com.lawnmower.ui.PvzSkin;
import lawnmower.Message;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.net.SocketTimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;

public class Main extends Game {
    private static final Logger log = LoggerFactory.getLogger(Main.class);
    private Skin skin;
    private TcpClient tcpClient;
    private UdpClient udpClient;
    private String playerName = "Player";
    private int playerId = -1; // 未登录时为 -1
    private long lastSocketWaitLogMs = 0L;
    private long lastUdpSyncTick = -1L;
    private long lastUdpServerTimeMs = -1L;

    private final AtomicBoolean networkRunning = new AtomicBoolean(false);
    private Thread networkThread;

    @Override
    public void create() {
        //使用自定义 PVZ 风格皮肤
        skin = PvzSkin.create();

        // 初始化 TCP 客户端（连接本地服务器）
        try {
            tcpClient = new TcpClient();
            tcpClient.connect(Config.SERVER_HOST, Config.SERVER_PORT);
            log.info("Connected to server {}:{}", Config.SERVER_HOST, Config.SERVER_PORT);
            startNetworkThread();
        } catch (IOException e) {
            log.error("Failed to connect to server", e);
            setScreen(new MainMenuScreen(this, skin));
        }
        setScreen(new MainMenuScreen(Main.this, skin));
    }

    private void startNetworkThread() {
        if (networkRunning.get()) return;

        networkRunning.set(true);
        networkThread = new Thread(() -> {
            while (networkRunning.get() && !Thread.currentThread().isInterrupted()) {
                try {
                    // 阻塞等待服务器消息
                    long beforeRead = System.currentTimeMillis();
                    Message.Packet packet = tcpClient.receivePacket();
                    long afterRead = System.currentTimeMillis();
                    if (packet == null) break; // 连接关闭

                    // 解析消息类型并分发
                    Message.MessageType type = packet.getMsgType();
                    Object payload = null;


                    Gdx.app.log("当前接受",type.name());
                    switch (type) {
                        case MSG_S2C_LOGIN_RESULT:
                            payload = Message.S2C_LoginResult.parseFrom(packet.getPayload());
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
                        // 其他未来消息可继续添加
                        default:
                            Gdx.app.log("NET", "Unknown message type: " + type);
                            continue;
                    }

                    // 通知主线程处理（UI 操作必须在渲染线程）
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

            // 线程退出
            networkRunning.set(false);
            stopUdpClient();
            Gdx.app.postRunnable(() -> {
                // 可选：提示连接断开，返回主菜单等
                if (!(getScreen() instanceof MainMenuScreen)) {
                    setScreen(new MainMenuScreen(Main.this, skin));
                }
            });
        }, "NetworkThread");

        networkThread.setDaemon(true); // 随主线程退出而终止
        networkThread.start();
    }

    // ———————— 公共访问方法 ————————

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
            }
            handleNetworkMessage(type, payload);
        } catch (IOException e) {
            Gdx.app.log("UDP", "Failed to parse UDP packet: " + e.getMessage());
        }
    }

    private Object parsePacketPayload(Message.Packet packet, Message.MessageType type) throws IOException {
        switch (type) {
            case MSG_S2C_LOGIN_RESULT:
                return Message.S2C_LoginResult.parseFrom(packet.getPayload());
            case MSG_S2C_ROOM_LIST:
                return Message.S2C_RoomList.parseFrom(packet.getPayload());
            case MSG_S2C_ROOM_UPDATE:
                return Message.S2C_RoomUpdate.parseFrom(packet.getPayload());
            case MSG_S2C_GAME_START:
                return Message.S2C_GameStart.parseFrom(packet.getPayload());
            case MSG_S2C_GAME_STATE_SYNC:
                return Message.S2C_GameStateSync.parseFrom(packet.getPayload());
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
            default:
                Gdx.app.log("NET", "Unknown message type: " + type);
                return null;
        }
    }

    private boolean shouldDropUdpSync(Message.S2C_GameStateSync sync) {
        long tick = sync.hasSyncTime()
                ? Integer.toUnsignedLong(sync.getSyncTime().getTick())
                : -1L;
        if (tick >= 0) {
            if (tick <= lastUdpSyncTick) {
                return true;
            }
            lastUdpSyncTick = tick;
            lastUdpServerTimeMs = sync.getServerTimeMs();
            return false;
        }

        long serverTime = sync.getServerTimeMs();
        if (serverTime <= lastUdpServerTimeMs) {
            return true;
        }
        lastUdpServerTimeMs = serverTime;
        return false;
    }

    private synchronized void prepareUdpClientForMatch() {
        if (playerId <= 0) {
            return;
        }
        try {
            startUdpClientIfNeeded();
            lastUdpSyncTick = -1L;
            lastUdpServerTimeMs = -1L;
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

    public boolean trySendPlayerInput(Message.C2S_PlayerInput input) {
        if (input == null) {
            return false;
        }
        if (udpClient != null && udpClient.isRunning()) {
            if (udpClient.sendPlayerInput(input)) {
                return true;
            }
        }
        if (tcpClient != null) {
            try {
                tcpClient.sendPlayerInput(input);
                return true;
            } catch (IOException e) {
                log.warn("Failed to send input via TCP fallback", e);
            }
        }
        return false;
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

    // ———————— 网络消息处理入口（由网络线程调用） ————————

    public void handleNetworkMessage(Message.MessageType type, Object message) {
        Gdx.app.postRunnable(() -> {
            switch (type) {
                case MSG_S2C_LOGIN_RESULT:
                    Message.S2C_LoginResult result = (Message.S2C_LoginResult) message;
                    if (result.getSuccess()) {
                        setPlayerId(result.getPlayerId());
                        setPlayerName(result.getMessageLogin());
                        // 登录成功，跳转到房间列表
                        setScreen(new RoomListScreen(Main.this, skin));
                    } else {
                        // 登录失败：返回主菜单并提示
                        if (getScreen() instanceof MainMenuScreen mainMenu) {
                            mainMenu.showError("登录失败: " + result.getMessageLogin());
                        } else {
                            setScreen(new MainMenuScreen(Main.this, skin));
                            ((MainMenuScreen) getScreen()).showError("登录失败: " + result.getMessageLogin());
                        }
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
                    if (!(getScreen() instanceof GameRoomScreen)) {
                        // 自动进入游戏房间界面
                        setScreen(new GameRoomScreen(Main.this, skin));
                    }
                    if (getScreen() instanceof GameRoomScreen gameRoom) {
                        gameRoom.onRoomUpdate(update.getRoomId(), update.getPlayersList());
                    }
                    break;
                case MSG_S2C_GAME_START:
                    prepareUdpClientForMatch();
                    if (getScreen() instanceof GameRoomScreen) {
                        // 切换到游戏场景
                        setScreen(new GameScreen(Main.this));
                    }
                    break;

                case MSG_S2C_GAME_STATE_SYNC:
                    // 将同步数据转发给 GameScreen（如果当前是游戏界面）
                    if (getScreen() instanceof GameScreen gameScreen) {
                        Message.S2C_GameStateSync sync = (Message.S2C_GameStateSync) message;
                        gameScreen.onGameStateReceived(sync);
                    }
                    break;

                case MSG_S2C_PLAYER_HURT:
                case MSG_S2C_ENEMY_DIED:
                case MSG_S2C_PLAYER_LEVEL_UP:
                case MSG_S2C_DROPPED_ITEM:
                case MSG_S2C_GAME_OVER:
                    // 暂时只打日志，后续由 GameScreen 处理
                    Gdx.app.log("GAME_EVENT", "Received game event: " + type);
                    if (getScreen() instanceof GameScreen gameScreen) {
                        gameScreen.onGameEvent(type, message);
                    }
                    break;
                default:
                    Gdx.app.log("NET", "Unhandled message type: " + type);
            }
        });
    }

    @Override
    public void dispose() {
        networkRunning.set(false);

        if (tcpClient != null) {
            try {
                tcpClient.close();
            } catch (IOException e) {
                log.warn("Error closing TCP client", e);
            }
        }

        stopUdpClient();

        if (networkThread != null) {
            networkThread.interrupt();
            networkThread = null;
        }

        if (skin != null) skin.dispose();
        super.dispose();
    }
}
