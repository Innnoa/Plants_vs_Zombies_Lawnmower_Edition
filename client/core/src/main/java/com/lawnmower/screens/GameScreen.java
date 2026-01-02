package com.lawnmower.screens;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.badlogic.gdx.Screen;
import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.GL20;
import com.badlogic.gdx.graphics.OrthographicCamera;
import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;

import com.badlogic.gdx.math.MathUtils;
import com.badlogic.gdx.math.Vector2;
import com.badlogic.gdx.utils.viewport.FitViewport;
import com.lawnmower.Main;
import com.lawnmower.players.PlayerInputCommand;
import com.lawnmower.players.PlayerStateSnapshot;
import com.lawnmower.players.ServerPlayerSnapshot;
import lawnmower.Message;

import java.util.*;

public class GameScreen implements Screen {

    private static final float WORLD_WIDTH = 1280f;
    private static final float WORLD_HEIGHT = 720f;
    private static final float PLAYER_SPEED = 200f;

    private static final float MAX_COMMAND_DURATION = 0.05f;
    private static final float MIN_COMMAND_DURATION = 1f / 120f;
    private static final long SNAPSHOT_RETENTION_MS = 400L;
    private static final long MAX_EXTRAPOLATION_MS = 150L;
    private static final long INTERP_DELAY_MIN_MS = 80L;
    private static final long INTERP_DELAY_MAX_MS = 220L;

    private final Vector2 renderBuffer = new Vector2();
    private final Map<Integer, Message.PlayerState> serverPlayerStates = new HashMap<>();
    private final Map<Integer, Deque<ServerPlayerSnapshot>> remotePlayerServerSnapshots = new HashMap<>();
    private final Map<Integer, PlayerInputCommand> unconfirmedInputs = new LinkedHashMap<>();
    private final Queue<PlayerStateSnapshot> snapshotHistory = new ArrayDeque<>();

    private Main game;
    private OrthographicCamera camera;
    private FitViewport viewport;
    private SpriteBatch batch;
    private Texture playerTexture;
    private TextureRegion playerTextureRegion;
    private Texture backgroundTexture;

    private Vector2 predictedPosition = new Vector2();
    private float predictedRotation = 0f;
    private int inputSequence = 0;
    private boolean hasReceivedInitialState = false;

    private boolean hasPendingInputChunk = false;
    private final Vector2 pendingMoveDir = new Vector2();
    private boolean pendingAttack = false;
    private float pendingInputDuration = 0f;
    private boolean idleAckSent = true;

    private float clockOffsetMs = 0f;
    private float smoothedRttMs = 120f;

    public GameScreen(Main game) {
        this.game = Objects.requireNonNull(game);
    }

    @Override
    public void show() {
        camera = new OrthographicCamera();
        viewport = new FitViewport(WORLD_WIDTH, WORLD_HEIGHT, camera);
        batch = new SpriteBatch();

        try {
            backgroundTexture = new Texture(Gdx.files.internal("background/roomListBackground.png"));
        } catch (Exception e) {
            Pixmap bgPixmap = new Pixmap((int) WORLD_WIDTH, (int) WORLD_HEIGHT, Pixmap.Format.RGBA8888);
            bgPixmap.setColor(0.05f, 0.15f, 0.05f, 1f);
            bgPixmap.fill();
            backgroundTexture = new Texture(bgPixmap);
            bgPixmap.dispose();
        }

        try {
            playerTexture = new Texture(Gdx.files.internal("player/bbb1.png"));
            playerTextureRegion = new TextureRegion(playerTexture);
        } catch (Exception e) {
            Pixmap pixmap = new Pixmap(64, 64, Pixmap.Format.RGBA8888);
            pixmap.setColor(Color.RED);
            pixmap.fillCircle(32, 32, 30);
            playerTexture = new Texture(pixmap);
            playerTextureRegion = new TextureRegion(playerTexture);
            pixmap.dispose();
        }

        predictedPosition.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f);
    }

    @Override
    public void render(float delta) {
        if (!hasReceivedInitialState || playerTextureRegion == null) {
            Gdx.gl.glClearColor(0.1f, 0.1f, 0.1f, 1);
            Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);
            return;
        }

        Vector2 dir = getMovementInput();
        boolean attacking = Gdx.input.isKeyJustPressed(Input.Keys.SPACE);

        simulateLocalStep(dir, delta);
        processInputChunk(dir, attacking, delta);

        Gdx.gl.glClearColor(0, 0, 0, 1);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);

        clampPositionToMap(predictedPosition);
        camera.position.set(predictedPosition.x, predictedPosition.y, 0);
        camera.update();

        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        batch.draw(backgroundTexture, 0, 0, WORLD_WIDTH, WORLD_HEIGHT);

        float width = playerTextureRegion.getRegionWidth();
        float height = playerTextureRegion.getRegionHeight();
        batch.draw(playerTextureRegion,
                predictedPosition.x - width / 2f,
                predictedPosition.y - height / 2f,
                width / 2f,
                height / 2f,
                width,
                height,
                1f,
                1f,
                predictedRotation);

        long estimatedServerTimeMs = estimateServerTimeMs();
        long renderServerTimeMs = estimatedServerTimeMs - computeRenderDelayMs();
        renderRemotePlayers(renderServerTimeMs, width, height);

        batch.end();
    }

    private void simulateLocalStep(Vector2 dir, float delta) {
        if (dir.len2() > 0.1f) {
            predictedPosition.add(dir.x * PLAYER_SPEED * delta, dir.y * PLAYER_SPEED * delta);
            predictedRotation = dir.angleDeg();
            clampPositionToMap(predictedPosition);
        }
    }

    private void processInputChunk(Vector2 dir, boolean attacking, float delta) {
        boolean moving = dir.len2() > 0.0001f;
        if (!moving && !attacking) {
            if (hasPendingInputChunk) {
                flushPendingInput();
            }
            if (!idleAckSent) {
                sendIdleCommand(delta);
            }
            return;
        }

        idleAckSent = false;

        if (!hasPendingInputChunk) {
            startPendingChunk(dir, attacking, delta);
            if (pendingAttack) {
                flushPendingInput();
            }
            return;
        }

        if (pendingMoveDir.epsilonEquals(dir, 0.001f) && pendingAttack == attacking) {
            pendingInputDuration += delta;
        } else {
            flushPendingInput();
            startPendingChunk(dir, attacking, delta);
        }

        if (pendingAttack || pendingInputDuration >= MAX_COMMAND_DURATION) {
            flushPendingInput();
        }
    }

    private void startPendingChunk(Vector2 dir, boolean attacking, float delta) {
        pendingMoveDir.set(dir);
        pendingAttack = attacking;
        pendingInputDuration = Math.max(delta, MIN_COMMAND_DURATION);
        hasPendingInputChunk = true;
    }

    private void flushPendingInput() {
        if (!hasPendingInputChunk) {
            return;
        }
        float duration = Math.max(pendingInputDuration, MIN_COMMAND_DURATION);
        PlayerInputCommand cmd = new PlayerInputCommand(inputSequence++, pendingMoveDir, pendingAttack, duration);
        unconfirmedInputs.put(cmd.seq, cmd);
        sendPlayerInputToServer(cmd);
        hasPendingInputChunk = false;
        pendingInputDuration = 0f;
    }

    private void sendIdleCommand(float delta) {
        PlayerInputCommand idleCmd = new PlayerInputCommand(
                inputSequence++,
                new Vector2(Vector2.Zero),
                false,
                Math.max(delta, MIN_COMMAND_DURATION)
        );
        unconfirmedInputs.put(idleCmd.seq, idleCmd);
        sendPlayerInputToServer(idleCmd);
        idleAckSent = true;
    }

    private void renderRemotePlayers(long renderServerTimeMs, float width, float height) {
        for (Map.Entry<Integer, Deque<ServerPlayerSnapshot>> entry : remotePlayerServerSnapshots.entrySet()) {
            Deque<ServerPlayerSnapshot> snapshots = entry.getValue();
            if (snapshots == null || snapshots.isEmpty()) {
                continue;
            }

            ServerPlayerSnapshot prev = null;
            ServerPlayerSnapshot next = null;
            for (ServerPlayerSnapshot snap : snapshots) {
                if (snap.serverTimestampMs <= renderServerTimeMs) {
                    prev = snap;
                } else {
                    next = snap;
                    break;
                }
            }

            Vector2 drawPos = renderBuffer;
            float drawRot;

            if (prev != null && next != null && next.serverTimestampMs > prev.serverTimestampMs) {
                float t = (renderServerTimeMs - prev.serverTimestampMs) /
                        (float) (next.serverTimestampMs - prev.serverTimestampMs);
                t = MathUtils.clamp(t, 0f, 1f);
                drawPos.set(prev.position).lerp(next.position, t);
                drawRot = prev.rotation + (next.rotation - prev.rotation) * t;
            } else if (prev != null) {
                long ahead = Math.max(0L, renderServerTimeMs - prev.serverTimestampMs);
                long clampedAhead = Math.min(ahead, MAX_EXTRAPOLATION_MS);
                float seconds = clampedAhead / 1000f;
                drawPos.set(prev.position).mulAdd(prev.velocity, seconds);
                drawRot = prev.rotation;
            } else {
                drawPos.set(next.position);
                drawRot = next.rotation;
            }

            clampPositionToMap(drawPos);
            batch.draw(playerTextureRegion,
                    drawPos.x - width / 2f,
                    drawPos.y - height / 2f,
                    width / 2f,
                    height / 2f,
                    width,
                    height,
                    1f,
                    1f,
                    drawRot);
        }
    }

    private long computeRenderDelayMs() {
        float estimate = smoothedRttMs * 0.5f + 50f;
        if (Float.isNaN(estimate) || Float.isInfinite(estimate)) {
            estimate = 120f;
        }
        long delay = Math.round(estimate);
        delay = Math.max(delay, INTERP_DELAY_MIN_MS);
        delay = Math.min(delay, INTERP_DELAY_MAX_MS);
        return delay;
    }

    private long estimateServerTimeMs() {
        return (long) (System.currentTimeMillis() + clockOffsetMs);
    }

    private void clampPositionToMap(Vector2 position) {
        if (playerTextureRegion == null) {
            return;
        }

        float halfWidth = playerTextureRegion.getRegionWidth() / 2.0f;
        float halfHeight = playerTextureRegion.getRegionHeight() / 2.0f;

        position.x = MathUtils.clamp(position.x, halfWidth, WORLD_WIDTH - halfWidth);
        position.y = MathUtils.clamp(position.y, halfHeight, WORLD_HEIGHT - halfHeight);
    }

    private Vector2 getMovementInput() {
        Vector2 input = new Vector2();
        if (Gdx.input.isKeyPressed(Input.Keys.W)) input.y += 1;
        if (Gdx.input.isKeyPressed(Input.Keys.S)) input.y -= 1;
        if (Gdx.input.isKeyPressed(Input.Keys.A)) input.x -= 1;
        if (Gdx.input.isKeyPressed(Input.Keys.D)) input.x += 1;
        return input.len2() > 0 ? input.nor() : input;
    }

    private void applyInputLocally(Vector2 pos, float rot, PlayerInputCommand input, float delta) {
        if (input.moveDir.len2() > 0.1f) {
            pos.add(input.moveDir.x * PLAYER_SPEED * delta, input.moveDir.y * PLAYER_SPEED * delta);
            clampPositionToMap(pos);
        }
    }

    private void sendPlayerInputToServer(PlayerInputCommand cmd) {
        if (game.getTcpClient() == null || game.getPlayerId() <= 0) return;

        Message.Vector2 pbVec = Message.Vector2.newBuilder()
                .setX(cmd.moveDir.x)
                .setY(cmd.moveDir.y)
                .build();

        Message.C2S_PlayerInput inputMsg = Message.C2S_PlayerInput.newBuilder()
                .setPlayerId(game.getPlayerId())
                .setMoveDirection(pbVec)
                .setIsAttacking(cmd.isAttacking)
                .setInputSeq(cmd.seq)
                .setDeltaMs(cmd.getDeltaMs())
                .build();

        try {
            game.getTcpClient().sendPlayerInput(inputMsg);
        } catch (Exception e) {
            Gdx.app.error("GameScreen", "Failed to send input", e);
        }
    }

    public void onGameStateReceived(Message.S2C_GameStateSync sync) {
        long currentServerTimeMs = sync.getServerTimeMs();
        long now = System.currentTimeMillis();
        float offsetSample = currentServerTimeMs - now;
        clockOffsetMs = MathUtils.lerp(clockOffsetMs, offsetSample, 0.1f);

        Set<Integer> updatedPlayerIds = new HashSet<>();
        int myId = game.getPlayerId();
        Message.PlayerState selfStateFromServer = null;

        for (Message.PlayerState player : sync.getPlayersList()) {
            int playerId = (int) player.getPlayerId();
            updatedPlayerIds.add(playerId);
            serverPlayerStates.put(playerId, player);

            if (playerId == myId && player.getIsAlive()) {
                selfStateFromServer = player;
            } else if (playerId != myId) {
                Vector2 position = new Vector2(player.getPosition().getX(), player.getPosition().getY());
                pushRemoteSnapshot(playerId, position, player.getRotation(), currentServerTimeMs);
            }
        }

        serverPlayerStates.entrySet().removeIf(entry -> {
            int id = entry.getKey();
            boolean shouldRemove = id != myId && !updatedPlayerIds.contains(id);
            if (shouldRemove) {
                remotePlayerServerSnapshots.remove(id);
            }
            return shouldRemove;
        });

        if (selfStateFromServer == null) return;

        Vector2 serverPos = new Vector2(
                selfStateFromServer.getPosition().getX(),
                selfStateFromServer.getPosition().getY()
        );
        clampPositionToMap(serverPos);

        int lastProcessedSeq = selfStateFromServer.getLastProcessedInputSeq();
        PlayerStateSnapshot snapshot = new PlayerStateSnapshot(
                serverPos,
                selfStateFromServer.getRotation(),
                lastProcessedSeq
        );
        snapshotHistory.offer(snapshot);
        while (snapshotHistory.size() > 10) {
            snapshotHistory.poll();
        }

        reconcileWithServer(snapshot);
    }

    private void pushRemoteSnapshot(int playerId, Vector2 position, float rotation, long serverTimeMs) {
        Deque<ServerPlayerSnapshot> queue = remotePlayerServerSnapshots
                .computeIfAbsent(playerId, k -> new ArrayDeque<>());

        Vector2 velocity = new Vector2();
        ServerPlayerSnapshot previous = queue.peekLast();
        if (previous != null) {
            long deltaMs = serverTimeMs - previous.serverTimestampMs;
            if (deltaMs > 0) {
                velocity.set(position).sub(previous.position).scl(1000f / deltaMs);
            } else {
                velocity.set(previous.velocity);
            }
            float maxSpeed = PLAYER_SPEED * 1.5f;
            if (velocity.len2() > maxSpeed * maxSpeed) {
                velocity.clamp(0f, maxSpeed);
            }
        }

        ServerPlayerSnapshot snap = new ServerPlayerSnapshot(position, rotation, velocity, serverTimeMs);
        queue.addLast(snap);

        while (!queue.isEmpty() &&
                (serverTimeMs - queue.peekFirst().serverTimestampMs) > SNAPSHOT_RETENTION_MS) {
            queue.removeFirst();
        }
    }

    private void reconcileWithServer(PlayerStateSnapshot serverSnapshot) {
        PlayerInputCommand acknowledged = unconfirmedInputs.get(serverSnapshot.lastProcessedInputSeq);
        if (acknowledged != null) {
            float sample = System.currentTimeMillis() - acknowledged.timestampMs;
            if (sample > 0f) {
                smoothedRttMs = MathUtils.lerp(smoothedRttMs, sample, 0.2f);
            }
        }

        predictedPosition.set(serverSnapshot.position);
        predictedRotation = serverSnapshot.rotation;
        clampPositionToMap(predictedPosition);

        for (PlayerInputCommand input : unconfirmedInputs.values()) {
            if (input.seq > serverSnapshot.lastProcessedInputSeq) {
                applyInputLocally(predictedPosition, predictedRotation, input, input.deltaSeconds);
            }
        }

        unconfirmedInputs.entrySet().removeIf(entry ->
                entry.getKey() <= serverSnapshot.lastProcessedInputSeq
        );

        hasReceivedInitialState = true;
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
    }

    public void onGameEvent(Message.MessageType type, Object message) {
        switch (type) {
            case MSG_S2C_PLAYER_HURT:
                Message.S2C_PlayerHurt hurt = (Message.S2C_PlayerHurt) message;
                Gdx.app.log("GameEvent", "Player " + hurt.getPlayerId() + " hurt, HP: " + hurt.getRemainingHealth());
                break;
            case MSG_S2C_ENEMY_DIED:
                Message.S2C_EnemyDied died = (Message.S2C_EnemyDied) message;
                Gdx.app.log("GameEvent", "Enemy " + died.getEnemyId() + " died at (" + died.getPosition().getX() + ", " +
                        died.getPosition().getY() + ")");
                break;
            case MSG_S2C_PLAYER_LEVEL_UP:
                Message.S2C_PlayerLevelUp levelUp = (Message.S2C_PlayerLevelUp) message;
                Gdx.app.log("GameEvent", "Player " + levelUp.getPlayerId() + " leveled up to " + levelUp.getNewLevel());
                break;
            case MSG_S2C_GAME_OVER:
                Gdx.app.log("GameEvent", "Game Over!");
                break;
            default:
                Gdx.app.log("GameEvent", "Unhandled game event: " + type);
        }
    }

    @Override
    public void resize(int width, int height) {
        viewport.update(width, height, true);
    }

    @Override
    public void pause() { }

    @Override
    public void resume() { }

    @Override
    public void hide() { }

    @Override
    public void dispose() {
        if (batch != null) batch.dispose();
        if (playerTexture != null) playerTexture.dispose();
        if (backgroundTexture != null) backgroundTexture.dispose();
    }
}




