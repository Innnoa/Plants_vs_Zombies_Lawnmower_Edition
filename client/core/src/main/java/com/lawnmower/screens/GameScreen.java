package com.lawnmower.screens;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.badlogic.gdx.Screen;
import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.GL20;
import com.badlogic.gdx.graphics.OrthographicCamera;
import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.Animation;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureAtlas;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.badlogic.gdx.math.MathUtils;
import com.badlogic.gdx.math.Vector2;
import com.badlogic.gdx.utils.TimeUtils;
import com.badlogic.gdx.utils.viewport.FitViewport;
import com.lawnmower.Main;
import com.lawnmower.players.PlayerInputCommand;
import com.lawnmower.players.PlayerStateSnapshot;
import com.lawnmower.players.ServerPlayerSnapshot;
import lawnmower.Message;

import java.util.*;

public class GameScreen implements Screen {

    private static final String TAG = "GameScreen";

    private static final float WORLD_WIDTH = 1280f;
    private static final float WORLD_HEIGHT = 720f;
    private static final float PLAYER_SPEED = 200f;

    private static final float MAX_COMMAND_DURATION = 0.05f;
    private static final float MIN_COMMAND_DURATION = 1f / 120f;
    private static final long SNAPSHOT_RETENTION_MS = 400L;
    private static final long MAX_EXTRAPOLATION_MS = 150L;
    private static final long INTERP_DELAY_MIN_MS = 80L;
    private static final long INTERP_DELAY_MAX_MS = 220L;
    private static final int MAX_UNCONFIRMED_INPUTS = 240;
    private static final long MAX_UNCONFIRMED_INPUT_AGE_MS = 1500L;
    private static final long REMOTE_PLAYER_TIMEOUT_MS = 5000L;

    private final Vector2 renderBuffer = new Vector2();
    private final Map<Integer, Message.PlayerState> serverPlayerStates = new HashMap<>();
    private final Map<Integer, Deque<ServerPlayerSnapshot>> remotePlayerServerSnapshots = new HashMap<>();
    private final Map<Integer, Boolean> remoteFacingRight = new HashMap<>();
    private final Map<Integer, Long> remotePlayerLastSeen = new HashMap<>();
    private final Map<Integer, PlayerInputCommand> unconfirmedInputs = new LinkedHashMap<>();
    private final Map<Integer, Long> inputSendTimes = new HashMap<>();
    private final Queue<PlayerStateSnapshot> snapshotHistory = new ArrayDeque<>();

    private Main game;
    private OrthographicCamera camera;
    private FitViewport viewport;
    private SpriteBatch batch;
    private Texture playerTexture;
    private TextureAtlas playerAtlas;
    private Animation<TextureRegion> playerIdleAnimation;
    private TextureRegion playerTextureRegion;
    private Texture backgroundTexture;
    private float playerAnimationTime = 0f;

    private Vector2 predictedPosition = new Vector2();
    private float predictedRotation = 0f;
    private int inputSequence = 0;
    private boolean hasReceivedInitialState = false;
    private boolean facingRight = true;
    private final Vector2 displayPosition = new Vector2();
    private static final float DISPLAY_LERP_RATE = 12f;
    private static final float DISPLAY_SNAP_DISTANCE = 1f;
    private boolean isLocallyMoving = false;

    private static final float DELTA_SPIKE_THRESHOLD = 0.02f;
    private static final long DELTA_LOG_INTERVAL_MS = 400L;
    private static final float POSITION_CORRECTION_LOG_THRESHOLD = 6f;
    private static final long POSITION_LOG_INTERVAL_MS = 700L;
    private static final float DISPLAY_DRIFT_LOG_THRESHOLD = 10f;
    private static final long DISPLAY_LOG_INTERVAL_MS = 700L;
    private static final float SYNC_INTERVAL_SMOOTH_ALPHA = 0.1f;
    private static final float SYNC_INTERVAL_LOG_THRESHOLD_MS = 120f;
    private static final long SYNC_INTERVAL_LOG_INTERVAL_MS = 800L;

    private boolean hasPendingInputChunk = false;
    private final Vector2 pendingMoveDir = new Vector2();
    private boolean pendingAttack = false;
    private float pendingInputDuration = 0f;
    private boolean idleAckSent = true;

    private float clockOffsetMs = 0f;
    private float smoothedRttMs = 120f;

    private static final float MAX_FRAME_DELTA = 1f / 30f;
    private static final float DELTA_SMOOTH_ALPHA = 0.15f;
    private float smoothedFrameDelta = 1f / 60f;

    private double logicalClockRemainderMs = 0.0;
    private long logicalTimeMs = 0L;

    private static final float RENDER_DELAY_LERP = 0.25f;
    private static final float MAX_RENDER_DELAY_STEP_MS = 15f;
    private float renderDelayMs = 150f;

    private long lastDeltaSpikeLogMs = 0L;
    private long lastCorrectionLogMs = 0L;
    private long lastDisplayDriftLogMs = 0L;
    private long lastSyncArrivalMs = 0L;
    private long lastSyncLogMs = 0L;
    private float smoothedSyncIntervalMs = 50f;

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
            playerAtlas = new TextureAtlas(Gdx.files.internal("Plants/PeaShooter/Standby/standby.atlas"));
            playerIdleAnimation = new Animation<>(0.1f, playerAtlas.getRegions(), Animation.PlayMode.LOOP);
            playerTextureRegion = playerIdleAnimation.getKeyFrame(0f);
        } catch (Exception e) {
            Pixmap pixmap = new Pixmap(64, 64, Pixmap.Format.RGBA8888);
            pixmap.setColor(Color.RED);
            pixmap.fillCircle(32, 32, 30);
            playerTexture = new Texture(pixmap);
            playerTextureRegion = new TextureRegion(playerTexture);
            playerIdleAnimation = null;
            pixmap.dispose();
        }

        predictedPosition.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f);
        displayPosition.set(predictedPosition);
    }

    @Override
    public void render(float delta) {
        advanceLogicalClock(delta);

        if (!hasReceivedInitialState || playerTextureRegion == null) {
            Gdx.gl.glClearColor(0.1f, 0.1f, 0.1f, 1);
            Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);
            return;
        }

        float renderDelta = getStableDelta(delta);
        Vector2 dir = getMovementInput();
        isLocallyMoving = dir.len2() > 0.0001f;
        boolean attacking = Gdx.input.isKeyJustPressed(Input.Keys.SPACE);

        simulateLocalStep(dir, renderDelta);
        processInputChunk(dir, attacking, renderDelta);

        Gdx.gl.glClearColor(0, 0, 0, 1);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);

        clampPositionToMap(predictedPosition);
        updateDisplayPosition(renderDelta);
        clampPositionToMap(displayPosition);
        camera.position.set(displayPosition.x, displayPosition.y, 0);
        camera.update();

        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        batch.draw(backgroundTexture, 0, 0, WORLD_WIDTH, WORLD_HEIGHT);

        playerAnimationTime += renderDelta;
        TextureRegion currentFrame = playerIdleAnimation != null
                ? playerIdleAnimation.getKeyFrame(playerAnimationTime, true)
                : playerTextureRegion;
        if (currentFrame == null) {
            batch.end();
            return;
        }
        playerTextureRegion = currentFrame;

        long estimatedServerTimeMs = estimateServerTimeMs();
        long renderServerTimeMs = estimatedServerTimeMs - computeRenderDelayMs();
        renderRemotePlayers(renderServerTimeMs, currentFrame);
        drawCharacterFrame(currentFrame, displayPosition.x, displayPosition.y, facingRight);

        batch.end();
    }

    private void advanceLogicalClock(float delta) {
        double total = delta * 1000.0 + logicalClockRemainderMs;
        long advance = (long) total;
        logicalClockRemainderMs = total - advance;
        logicalTimeMs += advance;
    }

    private float getStableDelta(float rawDelta) {
        float clamped = Math.min(rawDelta, MAX_FRAME_DELTA);
        smoothedFrameDelta += (clamped - smoothedFrameDelta) * DELTA_SMOOTH_ALPHA;
        logFrameDeltaSpike(rawDelta, smoothedFrameDelta);
        return smoothedFrameDelta;
    }

    private void simulateLocalStep(Vector2 dir, float delta) {
        if (dir.len2() > 0.1f) {
            predictedPosition.add(dir.x * PLAYER_SPEED * delta, dir.y * PLAYER_SPEED * delta);
            predictedRotation = dir.angleDeg();
            if (Math.abs(dir.x) > 0.01f) {
                facingRight = dir.x >= 0f;
            }
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
        inputSendTimes.put(cmd.seq, logicalTimeMs);
        pruneUnconfirmedInputs();
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
        inputSendTimes.put(idleCmd.seq, logicalTimeMs);
        pruneUnconfirmedInputs();
        sendPlayerInputToServer(idleCmd);
        idleAckSent = true;
    }

    private void pruneUnconfirmedInputs() {
        if (unconfirmedInputs.isEmpty()) {
            return;
        }

        long now = logicalTimeMs;
        Iterator<Map.Entry<Integer, PlayerInputCommand>> iterator = unconfirmedInputs.entrySet().iterator();
        boolean removed = false;
        while (iterator.hasNext()) {
            Map.Entry<Integer, PlayerInputCommand> entry = iterator.next();
            int seq = entry.getKey();
            long sentAt = inputSendTimes.getOrDefault(seq, entry.getValue().timestampMs);
            boolean tooOld = (now - sentAt) > MAX_UNCONFIRMED_INPUT_AGE_MS;
            boolean overflow = unconfirmedInputs.size() > MAX_UNCONFIRMED_INPUTS;
            if (tooOld || overflow) {
                iterator.remove();
                inputSendTimes.remove(seq);
                removed = true;
                continue;
            }
            if (!tooOld && !overflow) {
                break;
            }
        }

        if (removed) {
            Gdx.app.log(TAG, "Pruned stale inputs, remaining=" + unconfirmedInputs.size());
        }
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
    }

    private void renderRemotePlayers(long renderServerTimeMs, TextureRegion frame) {
        if (frame == null) {
            return;
        }
        for (Map.Entry<Integer, Deque<ServerPlayerSnapshot>> entry : remotePlayerServerSnapshots.entrySet()) {
            int playerId = entry.getKey();
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
            if (prev != null && next != null && next.serverTimestampMs > prev.serverTimestampMs) {
                float t = (renderServerTimeMs - prev.serverTimestampMs) /
                        (float) (next.serverTimestampMs - prev.serverTimestampMs);
                t = MathUtils.clamp(t, 0f, 1f);
                drawPos.set(prev.position).lerp(next.position, t);
            } else if (prev != null) {
                long ahead = Math.max(0L, renderServerTimeMs - prev.serverTimestampMs);
                long clampedAhead = Math.min(ahead, MAX_EXTRAPOLATION_MS);
                float seconds = clampedAhead / 1000f;
                drawPos.set(prev.position).mulAdd(prev.velocity, seconds);
            } else {
                drawPos.set(next.position);
            }
            clampPositionToMap(drawPos);
            boolean remoteFacing = remoteFacingRight.getOrDefault(playerId, true);
            drawCharacterFrame(frame, drawPos.x, drawPos.y, remoteFacing);
        }
    }

    private void drawCharacterFrame(TextureRegion frame, float centerX, float centerY, boolean faceRight) {
        if (frame == null) {
            return;
        }
        float width = frame.getRegionWidth();
        float height = frame.getRegionHeight();
        float drawX = centerX - width / 2f;
        float drawY = centerY - height / 2f;
        float originX = width / 2f;
        float originY = height / 2f;
        float scaleX = faceRight ? 1f : -1f;
        batch.draw(frame, drawX, drawY, originX, originY, width, height, scaleX, 1f, 0f);
    }

    private long computeRenderDelayMs() {
        float target = smoothedRttMs * 0.5f + 50f;
        if (Float.isNaN(target) || Float.isInfinite(target)) {
            target = 120f;
        }
        float jitterReserve = MathUtils.clamp(smoothedSyncIntervalMs * 1.2f + 30f,
                INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS + 30f);
        target = Math.max(target, jitterReserve);
        target = MathUtils.clamp(target, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        float delta = target - renderDelayMs;
        delta = MathUtils.clamp(delta, -MAX_RENDER_DELAY_STEP_MS, MAX_RENDER_DELAY_STEP_MS);
        renderDelayMs = MathUtils.clamp(renderDelayMs + delta * RENDER_DELAY_LERP,
                INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        return Math.round(renderDelayMs);
    }
    private void logSyncIntervalSpike(float intervalMs, float smoothInterval) {
        if (intervalMs < SYNC_INTERVAL_LOG_THRESHOLD_MS) {
            return;
            }
        long nowMs = TimeUtils.millis();
        if (nowMs - lastSyncLogMs < SYNC_INTERVAL_LOG_INTERVAL_MS) {
            return;
            }
        Gdx.app.log(TAG, "Sync interval spike=" + intervalMs + "ms smooth=" + smoothInterval
                + "ms renderDelay=" + renderDelayMs);
        lastSyncLogMs = nowMs;
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
            Gdx.app.error(TAG, "Failed to send input", e);
        }
    }

    public void onGameStateReceived(Message.S2C_GameStateSync sync) {
        long arrivalMs = TimeUtils.millis();
        if (lastSyncArrivalMs != 0L) {
            float interval = arrivalMs - lastSyncArrivalMs;
            smoothedSyncIntervalMs += (interval - smoothedSyncIntervalMs) * SYNC_INTERVAL_SMOOTH_ALPHA;
            logSyncIntervalSpike(interval, smoothedSyncIntervalMs);
        }
        lastSyncArrivalMs = arrivalMs;
        long currentServerTimeMs = sync.getServerTimeMs();
        long now = System.currentTimeMillis();
        float offsetSample = currentServerTimeMs - now;
        clockOffsetMs = MathUtils.lerp(clockOffsetMs, offsetSample, 0.1f);

        int myId = game.getPlayerId();
        Message.PlayerState selfStateFromServer = null;

        for (Message.PlayerState player : sync.getPlayersList()) {
            int playerId = (int) player.getPlayerId();
            serverPlayerStates.put(playerId, player);

            if (playerId == myId) {
                if (player.getIsAlive()) {
                    selfStateFromServer = player;
                }
                continue;
            }

            Vector2 position = new Vector2(player.getPosition().getX(), player.getPosition().getY());
            pushRemoteSnapshot(playerId, position, player.getRotation(), currentServerTimeMs);
            remotePlayerLastSeen.put(playerId, currentServerTimeMs);
        }

        purgeStaleRemotePlayers(currentServerTimeMs);

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
        clampPositionToMap(position);
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

        updateRemoteFacing(playerId, velocity, rotation);

        ServerPlayerSnapshot snap = new ServerPlayerSnapshot(position, rotation, velocity, serverTimeMs);
        queue.addLast(snap);

        while (queue.size() > 1 &&
                (serverTimeMs - queue.peekFirst().serverTimestampMs) > SNAPSHOT_RETENTION_MS) {
            queue.removeFirst();
        }
    }

    private void updateRemoteFacing(int playerId, Vector2 velocity, float rotation) {
        boolean faceRight = remoteFacingRight.getOrDefault(playerId, true);
        if (Math.abs(velocity.x) > 0.001f) {
            faceRight = velocity.x >= 0f;
        } else {
            faceRight = inferFacingFromRotation(rotation);
        }
        remoteFacingRight.put(playerId, faceRight);
    }

    private void purgeStaleRemotePlayers(long currentServerTimeMs) {
        Iterator<Map.Entry<Integer, Long>> iterator = remotePlayerLastSeen.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<Integer, Long> entry = iterator.next();
            long lastSeen = entry.getValue();
            if ((currentServerTimeMs - lastSeen) > REMOTE_PLAYER_TIMEOUT_MS) {
                int playerId = entry.getKey();
                iterator.remove();
                remotePlayerServerSnapshots.remove(playerId);
                remoteFacingRight.remove(playerId);
                serverPlayerStates.remove(playerId);
            }
        }
    }

    private boolean inferFacingFromRotation(float rotation) {
        float normalized = rotation % 360f;
        if (normalized < 0f) {
            normalized += 360f;
        }
        return !(normalized > 90f && normalized < 270f);
    }

    private void reconcileWithServer(PlayerStateSnapshot serverSnapshot) {
        PlayerInputCommand acknowledged = unconfirmedInputs.get(serverSnapshot.lastProcessedInputSeq);
        if (acknowledged != null) {
            Long sentLogical = inputSendTimes.remove(acknowledged.seq);
            float sample;
            if (sentLogical != null) {
                sample = logicalTimeMs - sentLogical;
            } else {
                sample = System.currentTimeMillis() - acknowledged.timestampMs;
            }
            if (sample > 0f) {
                smoothedRttMs = MathUtils.lerp(smoothedRttMs, sample, 0.2f);
            }
        }

        float correctionDist = predictedPosition.dst(serverSnapshot.position);
        boolean wasInitialized = hasReceivedInitialState;
        predictedPosition.set(serverSnapshot.position);
        predictedRotation = serverSnapshot.rotation;
        facingRight = inferFacingFromRotation(predictedRotation);
        clampPositionToMap(predictedPosition);
        logServerCorrection(correctionDist, serverSnapshot.lastProcessedInputSeq);
        if (!wasInitialized) {
            displayPosition.set(predictedPosition);
        }

        for (PlayerInputCommand input : unconfirmedInputs.values()) {
            if (input.seq > serverSnapshot.lastProcessedInputSeq) {
                applyInputLocally(predictedPosition, predictedRotation, input, input.deltaSeconds);
            }
        }

        unconfirmedInputs.entrySet().removeIf(entry -> {
            boolean applied = entry.getKey() <= serverSnapshot.lastProcessedInputSeq;
            if (applied) {
                inputSendTimes.remove(entry.getKey());
            }
            return applied;
        });
        pruneUnconfirmedInputs();

        hasReceivedInitialState = true;
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }

        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= (DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE * 4f)) {
            displayPosition.set(predictedPosition);
        } else {
            displayPosition.lerp(predictedPosition, 0.35f);
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
                Gdx.app.log("GameEvent", "Player " + levelUp.getPlayerId() + " leveled up to " +
                        levelUp.getNewLevel());
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
        if (playerAtlas != null) playerAtlas.dispose();
        if (backgroundTexture != null) backgroundTexture.dispose();
    }

    private void updateDisplayPosition(float delta) {
        if (!hasReceivedInitialState) {
            return;
        }
        if (!isLocallyMoving) {
            displayPosition.set(predictedPosition);
            return;
        }
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE) {
            displayPosition.set(predictedPosition);
            return;
        }
        float distance = (float) Math.sqrt(distSq);
        if (distance > DISPLAY_DRIFT_LOG_THRESHOLD) {
            logDisplayDrift(distance);
        }
        float alpha = MathUtils.clamp(delta * DISPLAY_LERP_RATE, 0f, 1f);
        displayPosition.lerp(predictedPosition, alpha);
    }

    private void logFrameDeltaSpike(float rawDelta, float stableDelta) {
        if (Math.abs(rawDelta - stableDelta) < DELTA_SPIKE_THRESHOLD) {
            return;
        }
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDeltaSpikeLogMs < DELTA_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Frame delta spike raw=" + rawDelta + " stable=" + stableDelta
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDeltaSpikeLogMs = nowMs;
    }

    private void logServerCorrection(float correctionDist, int lastProcessedSeq) {
        if (correctionDist < POSITION_CORRECTION_LOG_THRESHOLD) {
            return;
        }
        long nowMs = TimeUtils.millis();
        if (nowMs - lastCorrectionLogMs < POSITION_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Server correction dist=" + correctionDist
                + " lastSeq=" + lastProcessedSeq
                + " pendingInputs=" + unconfirmedInputs.size());
        lastCorrectionLogMs = nowMs;
    }

    private void logDisplayDrift(float drift) {
        if (drift < DISPLAY_DRIFT_LOG_THRESHOLD) {
            return;
        }
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDisplayDriftLogMs < DISPLAY_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Display drift=" + drift + " facingRight=" + facingRight
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDisplayDriftLogMs = nowMs;
    }
}