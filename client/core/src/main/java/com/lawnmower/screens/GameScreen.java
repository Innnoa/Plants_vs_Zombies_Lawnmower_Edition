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
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureAtlas;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.badlogic.gdx.math.MathUtils;
import com.badlogic.gdx.math.Vector2;
import com.badlogic.gdx.utils.Array;
import com.badlogic.gdx.utils.TimeUtils;
import com.badlogic.gdx.utils.viewport.FitViewport;
import com.lawnmower.enemies.EnemyDefinitions;
import com.lawnmower.enemies.EnemyView;
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
    // 与服务器 GameManager::SceneConfig.move_speed (200f) 对齐，避免预测/权威速度不一致
    private static final float PLAYER_SPEED = 200f;

    // 输入分段更细（20~33ms），降低服务端堆积
    private static final float MAX_COMMAND_DURATION = 0.025f;
    private static final float MIN_COMMAND_DURATION = 1f / 120f;
    private static final long SNAPSHOT_RETENTION_MS = 400L;
    private static final long MAX_EXTRAPOLATION_MS = 150L;
    private static final int PLACEHOLDER_ENEMY_ID = -1;
    private static final long ENEMY_TIMEOUT_MS = 5000L;
    private static final int DEFAULT_ENEMY_TYPE_ID = EnemyDefinitions.getDefaultTypeId();
    private static final long INTERP_DELAY_MIN_MS = 60L;
    private static final long INTERP_DELAY_MAX_MS = 180L;
    private static final int AUTO_ATTACK_TOGGLE_KEY = Input.Keys.C;

    private static final int MAX_UNCONFIRMED_INPUTS = 240;
    private static final long MAX_UNCONFIRMED_INPUT_AGE_MS = 1500L;
    private static final long REMOTE_PLAYER_TIMEOUT_MS = 5000L;
    private static final long MIN_INPUT_SEND_INTERVAL_MS = 10L; // ~100Hz

    /*
     * 增量同步,标识服务端传入的变化值,采用位掩码,更方便能看出来需要传入的值是否发生了变化,比如位置信息
     * 而且这个方式是将Message中的字段缓存成变量,解耦代码
     */
    private static final int PLAYER_DELTA_POSITION_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_POSITION_VALUE;
    private static final int PLAYER_DELTA_ROTATION_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_ROTATION_VALUE;
    private static final int PLAYER_DELTA_IS_ALIVE_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_IS_ALIVE_VALUE;
    private static final int PLAYER_DELTA_LAST_INPUT_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_LAST_PROCESSED_INPUT_SEQ_VALUE;

    private static final int ENEMY_DELTA_POSITION_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_POSITION_VALUE;
    private static final int ENEMY_DELTA_HEALTH_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_HEALTH_VALUE;
    private static final int ENEMY_DELTA_IS_ALIVE_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_IS_ALIVE_VALUE;

    private final Vector2 renderBuffer = new Vector2();
    private final Vector2 projectileTempVector = new Vector2();
    private final Map<Integer, Message.PlayerState> serverPlayerStates = new HashMap<>();
    private final Map<Integer, Message.EnemyState> enemyStateCache = new HashMap<>();
    private final Map<Integer, EnemyView> enemyViews = new HashMap<>();
    private final Map<Integer, Long> enemyLastSeen = new HashMap<>();
    private final Map<Integer, Deque<ServerPlayerSnapshot>> remotePlayerServerSnapshots = new HashMap<>();
    private final Map<Integer, Boolean> remoteFacingRight = new HashMap<>();
    private final Map<Integer, Long> remotePlayerLastSeen = new HashMap<>();
    private final Map<Integer, Vector2> remoteDisplayPositions = new HashMap<>();
    private final Map<Integer, PlayerInputCommand> unconfirmedInputs = new LinkedHashMap<>();
    private final Map<Integer, Long> inputSendTimes = new HashMap<>();
    private final Queue<PlayerStateSnapshot> snapshotHistory = new ArrayDeque<>();
    private final Map<Long, ProjectileView> projectileViews = new HashMap<>();
    private final Array<ProjectileImpact> projectileImpacts = new Array<>();

    private Main game;
    private OrthographicCamera camera;
    private FitViewport viewport;
    private SpriteBatch batch;
    private Texture playerTexture;
    private TextureAtlas playerAtlas;
    private Animation<TextureRegion> playerIdleAnimation;
    private TextureRegion playerTextureRegion;
    private Texture backgroundTexture;
    private final Map<Integer, Animation<TextureRegion>> enemyAnimations = new HashMap<>();
    private final Map<Integer, Animation<TextureRegion>> enemyAttackAnimations = new HashMap<>();
    private final Map<String, TextureAtlas> enemyAtlasCache = new HashMap<>();
    private Texture enemyFallbackTexture;
    private TextureRegion enemyFallbackRegion;
    private TextureAtlas projectileAtlas;
    private Animation<TextureRegion> projectileAnimation;
    private Texture projectileFallbackTexture;
    private TextureRegion projectileFallbackRegion;
    private TextureAtlas projectileImpactAtlas;
    private Animation<TextureRegion> projectileImpactAnimation;
    private float playerAnimationTime = 0f;
    private BitmapFont loadingFont;
    private GlyphLayout loadingLayout;

    private Vector2 predictedPosition = new Vector2();
    private float predictedRotation = 0f;
    private int inputSequence = 0;
    private boolean hasReceivedInitialState = false;
    private boolean facingRight = true;
    private final Vector2 displayPosition = new Vector2();
    private static final float DISPLAY_LERP_RATE = 12f;
    private static final float DISPLAY_SNAP_DISTANCE = 1f;
    private boolean isLocallyMoving = false;
    private long initialStateStartMs = 0L;
    private long lastInitialStateRequestMs = 0L;
    private boolean initialStateWarningLogged = false;
    private boolean initialStateCriticalLogged = false;
    private boolean initialStateFailureLogged = false;
    private int initialStateRequestCount = 0;
    private long lastDeltaResyncRequestMs = 0L;

    private static final float DELTA_SPIKE_THRESHOLD = 0.02f;
    private static final long DELTA_LOG_INTERVAL_MS = 400L;
    private static final float POSITION_CORRECTION_LOG_THRESHOLD = 6f;
    private static final long POSITION_LOG_INTERVAL_MS = 700L;
    private static final float DISPLAY_DRIFT_LOG_THRESHOLD = 10f;
    private static final long DISPLAY_LOG_INTERVAL_MS = 700L;
    private static final float SYNC_INTERVAL_SMOOTH_ALPHA = 0.1f;
    private static final float SYNC_INTERVAL_LOG_THRESHOLD_MS = 120f;
    private static final long SYNC_INTERVAL_LOG_INTERVAL_MS = 800L;
    private static final float SYNC_DEVIATION_SMOOTH_ALPHA = 0.12f;
    private static final float REMOTE_DISPLAY_LERP_RATE = 14f;
    private static final float REMOTE_DISPLAY_SNAP_DISTANCE = 8f;
    private static final long DROPPED_SYNC_LOG_INTERVAL_MS = 900L;
    private static final long INITIAL_STATE_REQUEST_INTERVAL_MS = 1500L;
    private static final long INITIAL_STATE_WARNING_MS = 4000L;
    private static final long INITIAL_STATE_CRITICAL_MS = 10000L;
    private static final long INITIAL_STATE_FAILURE_HINT_MS = 16000L;
    private static final long DELTA_RESYNC_COOLDOWN_MS = 1200L;

    private boolean hasPendingInputChunk = false;
    private final Vector2 pendingMoveDir = new Vector2();
    private boolean pendingAttack = false;
    private float pendingInputDuration = 0f;
    private long pendingInputStartMs = 0L;
    private boolean idleAckSent = true;
    private boolean autoAttackToggle = false;

    private float clockOffsetMs = 0f;
    private float smoothedRttMs = 120f;

    private static final float MAX_FRAME_DELTA = 1f / 30f;
    private static final float DELTA_SMOOTH_ALPHA = 0.15f;
    private float smoothedFrameDelta = 1f / 60f;

    private double logicalClockRemainderMs = 0.0;
    private long logicalTimeMs = 0L;

    private static final float RENDER_DELAY_LERP = 0.35f;
    private static final float MAX_RENDER_DELAY_STEP_MS = 20f;
    private float renderDelayMs = 100f;

    private long lastDeltaSpikeLogMs = 0L;
    private long lastCorrectionLogMs = 0L;
    private long lastDisplayDriftLogMs = 0L;
    private long lastSyncArrivalMs = 0L;
    private long lastSyncLogMs = 0L;
    private long lastDroppedSyncLogMs = 0L;
    private long lastAppliedSyncTick = -1L;
    private long lastAppliedServerTimeMs = -1L;
    // 30Hz 目标同步间隔约 33ms，预置一个靠近目标的初值便于平滑
    private float smoothedSyncIntervalMs = 35f;
    private float smoothedSyncDeviationMs = 30f;
    private Message.C2S_PlayerInput pendingRateLimitedInput;
    private long lastInputSendMs = 0L;
    private String statusToastMessage = "";
    private float statusToastTimer = 0f;
    private static final float STATUS_TOAST_DURATION = 2.75f;

    public GameScreen(Main game) {
        this.game = Objects.requireNonNull(game);
    }

    @Override
    public void show() {
        camera = new OrthographicCamera();
        viewport = new FitViewport(WORLD_WIDTH, WORLD_HEIGHT, camera);
        batch = new SpriteBatch();//绘制人物用的
        loadingFont = new BitmapFont();//绘制字体
        loadingFont.getData().setScale(1.3f);//字体放大1.3倍
        loadingLayout = new GlyphLayout();//布局文本,是一种比较高级的布局

        //背景
        try {
            backgroundTexture = new Texture(Gdx.files.internal("background/roomListBackground.png"));
        } catch (Exception e) {
            Pixmap bgPixmap = new Pixmap((int) WORLD_WIDTH, (int) WORLD_HEIGHT, Pixmap.Format.RGBA8888);//设置纯色图
            bgPixmap.setColor(0.05f, 0.15f, 0.05f, 1f);
            bgPixmap.fill();
            backgroundTexture = new Texture(bgPixmap);
            bgPixmap.dispose();
        }

        //人物
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
        //加载资源
        loadEnemyAssets();
        loadProjectileAssets();
        enemyViews.clear();
        enemyLastSeen.clear();
        projectileViews.clear();
        projectileImpacts.clear();
        spawnPlaceholderEnemy();

        //设置人物初始位置为地图中央
        predictedPosition.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f);
        displayPosition.set(predictedPosition);
        resetInitialStateTracking();
    }

    @Override
    /**
    游戏主循环
     */
    public void render(float delta) {
        advanceLogicalClock(delta);//更新一个稳定的时间跳变
        pumpPendingNetworkInput();

        /*
        hasReceivedInitialState:游戏初始状态
        playerTextureRegion:角色纹理
         */
        if (!hasReceivedInitialState || playerTextureRegion == null) {
            maybeRequestInitialStateResync();
            renderLoadingOverlay();
            return;
        }
        /*
        采集本地输入
         */
        float renderDelta = getStableDelta(delta);
        Vector2 dir = getMovementInput();
        isLocallyMoving = dir.len2() > 0.0001f;//判断是否在移动
        if (Gdx.input.isKeyJustPressed(AUTO_ATTACK_TOGGLE_KEY)) {
            autoAttackToggle = !autoAttackToggle;
            showStatusToast(autoAttackToggle ? "自动攻击已开启" : "自动攻击已关闭");
        }
        boolean attacking = Gdx.input.isKeyPressed(Input.Keys.SPACE) || autoAttackToggle;//支持长按或自动攻击

        /*
        预测操作
         */
        simulateLocalStep(dir, renderDelta);
        processInputChunk(dir, attacking, renderDelta);

        /*
        清屏加相机跟随
         */
        Gdx.gl.glClearColor(0, 0, 0, 1);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);

        clampPositionToMap(predictedPosition);
        updateDisplayPosition(renderDelta);
        clampPositionToMap(displayPosition);
        camera.position.set(displayPosition.x, displayPosition.y, 0);
        camera.update();

        /*
        开始渲染
         */
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        batch.draw(backgroundTexture, 0, 0, WORLD_WIDTH, WORLD_HEIGHT);
        /*
        播放玩家空闲动画
         */
        playerAnimationTime += renderDelta;
        TextureRegion currentFrame = playerIdleAnimation != null
                ? playerIdleAnimation.getKeyFrame(playerAnimationTime, true)//循环播放
                : playerTextureRegion;
        if (currentFrame == null) {
            batch.end();
            return;
        }
        playerTextureRegion = currentFrame;
        /*
        估算服务器时间,计算渲染延迟
         */
        long estimatedServerTimeMs = estimateServerTimeMs();
        long renderServerTimeMs = estimatedServerTimeMs - computeRenderDelayMs();
        updateProjectiles(renderDelta, renderServerTimeMs);
        /*
        渲染敌人和玩家
         */
        renderEnemies(renderDelta, renderServerTimeMs);
        renderRemotePlayers(renderServerTimeMs, currentFrame, renderDelta);
        renderProjectiles();
        renderProjectileImpacts(renderDelta);
        /*
        渲染本地玩家角色
         */
        drawCharacterFrame(currentFrame, displayPosition.x, displayPosition.y, facingRight);
        renderStatusToast(renderDelta);

        batch.end();
    }

    /**
     * 更新一个稳定的时间跳变
     * @param delta
     */
    private void advanceLogicalClock(float delta) {
        double total = delta * 1000.0 + logicalClockRemainderMs;
        long advance = (long) total;
        logicalClockRemainderMs = total - advance;
        logicalTimeMs += advance;
    }

    /**
     * 平滑操作
     * @param rawDelta
     * @return
     */
    private float getStableDelta(float rawDelta) {
        float clamped = Math.min(rawDelta, MAX_FRAME_DELTA);//设置最大帧间隔防止极大帧
        smoothedFrameDelta += (clamped - smoothedFrameDelta) * DELTA_SMOOTH_ALPHA;//指数平滑操作,利用IIR低通滤波器
        logFrameDeltaSpike(rawDelta, smoothedFrameDelta);//日志输出
        return smoothedFrameDelta;
    }

    /**
     * 预测操作,本地直接用预测进行移动
     * @param dir
     * @param delta
     */
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

    /**
     * 打包发送玩家输入
     * @param dir
     * @param attacking
     * @param delta
     */
    private void processInputChunk(Vector2 dir, boolean attacking, float delta) {
        boolean moving = dir.len2() > 0.0001f;//判断是否在移动
        //未移动未攻击
        if (!moving && !attacking) {
            if (hasPendingInputChunk) {
                flushPendingInput();
            }
            if (!idleAckSent) {
                sendIdleCommand(delta);
            }
            return;
        }
        //重置空闲标志
        idleAckSent = false;
        //首次操作
        if (!hasPendingInputChunk) {
            startPendingChunk(dir, attacking, delta);
            if (pendingAttack) {
                flushPendingInput();
            }
            return;
        }
        //判断是否可合并
        if (pendingMoveDir.epsilonEquals(dir, 0.001f) && pendingAttack == attacking) {
            pendingInputDuration += delta;//可以,延长持续时间
        } else {
            flushPendingInput();//不可以,结束旧块
            startPendingChunk(dir, attacking, delta);//开始新块
        }
        //攻击和按键持续时间超过上限
        if (pendingAttack || pendingInputDuration >= MAX_COMMAND_DURATION) {
            flushPendingInput();
        }
    }

    /**
     * 初始化所需状态
     * @param dir
     * @param attacking
     * @param delta
     */
    private void startPendingChunk(Vector2 dir, boolean attacking, float delta) {
        pendingMoveDir.set(dir);
        pendingAttack = attacking;
        pendingInputDuration = Math.max(delta, MIN_COMMAND_DURATION);
        pendingInputStartMs = logicalTimeMs;
        hasPendingInputChunk = true;
    }

    /**
     * 将客户端的输入打包发给服务器
     */
    private void flushPendingInput() {
        //检查是否有待发送的输入
        if (!hasPendingInputChunk) {
            return;
        }
        float duration = resolvePendingDurationSeconds();
        PlayerInputCommand cmd = new PlayerInputCommand(inputSequence++, pendingMoveDir, pendingAttack, duration);
        unconfirmedInputs.put(cmd.seq, cmd);//存入未确认输入的缓存
        inputSendTimes.put(cmd.seq, logicalTimeMs);
        pruneUnconfirmedInputs();
        sendPlayerInputToServer(cmd);
        resetPendingInputAccumulator();
    }

    /**
     * 在无输入时向服务器发送心跳
     * @param delta
     */
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

    /**
     * 清理未确认队列
     */
    private void pruneUnconfirmedInputs() {
        if (unconfirmedInputs.isEmpty()) {
            return;
        }

        /*
        安全迭代删除过期条目
         */
        long now = logicalTimeMs;
        Iterator<Map.Entry<Integer, PlayerInputCommand>> iterator = unconfirmedInputs.entrySet().iterator();
        boolean removed = false;
        while (iterator.hasNext()) {
            Map.Entry<Integer, PlayerInputCommand> entry = iterator.next();
            int seq = entry.getKey();
            long sentAt = inputSendTimes.getOrDefault(seq, entry.getValue().timestampMs);
            /*
            判断是否真的需要删除
             */
            boolean tooOld = (now - sentAt) > MAX_UNCONFIRMED_INPUT_AGE_MS;
            boolean overflow = unconfirmedInputs.size() > MAX_UNCONFIRMED_INPUTS;
            if (tooOld || overflow) {
                iterator.remove();
                inputSendTimes.remove(seq);
                removed = true;
                continue;
            }
            //提前终止遍历
            if (!tooOld && !overflow) {
                break;
            }
        }

        /*
        日志与空闲状态处理
         */
        if (removed) {
            Gdx.app.log(TAG, "Pruned stale inputs, remaining=" + unconfirmedInputs.size());
        }
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
    }

    /**
     * 渲染其他玩家
     * @param renderServerTimeMs
     * @param frame
     * @param delta
     */
    private void renderRemotePlayers(long renderServerTimeMs, TextureRegion frame, float delta) {
        //空检查
        if (frame == null) {
            return;
        }
        //遍历每个玩家的快照
        for (Map.Entry<Integer, Deque<ServerPlayerSnapshot>> entry : remotePlayerServerSnapshots.entrySet()) {
            int playerId = entry.getKey();
            Deque<ServerPlayerSnapshot> snapshots = entry.getValue();
            if (snapshots == null || snapshots.isEmpty()) {
                continue;
            }
            //插值区间
            ServerPlayerSnapshot prev = null;
            ServerPlayerSnapshot next = null;
            for (ServerPlayerSnapshot snap : snapshots) {
                if (snap.serverTimestampMs <= renderServerTimeMs) {
                    prev = snap;//最新的旧快照
                } else {
                    next = snap;//下一个快照
                    break;
                }
            }

            Vector2 targetPos = renderBuffer;
            /*
             插值计算策略
             */
            //有上一个和下一个就平滑插中值
            if (prev != null && next != null && next.serverTimestampMs > prev.serverTimestampMs) {
                float t = (renderServerTimeMs - prev.serverTimestampMs) /
                        (float) (next.serverTimestampMs - prev.serverTimestampMs);
                t = MathUtils.clamp(t, 0f, 1f);
                targetPos.set(prev.position).lerp(next.position, t);
            }//只有prev就预测一个新值
            else if (prev != null) {
                long ahead = Math.max(0L, renderServerTimeMs - prev.serverTimestampMs);
                long clampedAhead = Math.min(ahead, MAX_EXTRAPOLATION_MS);
                float seconds = clampedAhead / 1000f;
                targetPos.set(prev.position).mulAdd(prev.velocity, seconds);
            }//只有新值就直接用
            else {
                targetPos.set(next.position);
            }
            clampPositionToMap(targetPos);
            boolean remoteFacing = remoteFacingRight.getOrDefault(playerId, true);
            //获取初始化位置缓存
            Vector2 displayPos = remoteDisplayPositions
                    .computeIfAbsent(playerId, id -> new Vector2(targetPos));
            //平滑过度,属于是保险的保险,防止因包跳跃造成的突变
            float distSq = displayPos.dst2(targetPos);
            if (distSq > REMOTE_DISPLAY_SNAP_DISTANCE * REMOTE_DISPLAY_SNAP_DISTANCE) {
                displayPos.set(targetPos);
            } else {
                float lerpAlpha = MathUtils.clamp(delta * REMOTE_DISPLAY_LERP_RATE, 0f, 1f);
                displayPos.lerp(targetPos, lerpAlpha);
            }
            drawCharacterFrame(frame, displayPos.x, displayPos.y, remoteFacing);
        }
    }

    /**
     * 渲染敌人
     * @param delta
     * @param renderServerTimeMs
     */
    private void renderEnemies(float delta, long renderServerTimeMs) {
        if (batch == null || enemyViews.isEmpty()) {
            return;
        }
        for (EnemyView view : enemyViews.values()) {
            view.render(batch, delta, renderServerTimeMs);
        }
    }

    private void updateProjectiles(float delta, long serverTimeMs) {
        if (projectileViews.isEmpty()) {
            return;
        }
        Iterator<Map.Entry<Long, ProjectileView>> iterator = projectileViews.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<Long, ProjectileView> entry = iterator.next();
            ProjectileView view = entry.getValue();
            view.animationTime += delta;
            view.position.mulAdd(view.velocity, delta);
            boolean expired = view.expireServerTimeMs > 0 && serverTimeMs >= view.expireServerTimeMs;
            if (expired || isProjectileOutOfBounds(view.position)) {
                iterator.remove();
            }
        }
    }

    /**
     * 批量渲染投射物
     */
    private void renderProjectiles() {
        //前置条件
        if (projectileViews.isEmpty() || batch == null) {
            return;
        }
        //遍历投射物
        for (ProjectileView view : projectileViews.values()) {
            TextureRegion frame = resolveProjectileFrame(view);
            //动态解析贴图帧
            if (frame == null) {
                continue;
            }
            //获取尺寸绘制位置
            float width = frame.getRegionWidth();
            float height = frame.getRegionHeight();
            float drawX = view.position.x - width / 2f;
            float drawY = view.position.y - height / 2f;
            float originX = width / 2f;
            float originY = height / 2f;
            batch.draw(frame, drawX, drawY, originX, originY, width, height,
                    1f, 1f, view.rotationDeg);
        }
    }

    /**
     * 渲染投射物命中目标时的瞬间特效
     * @param delta
     */
    private void renderProjectileImpacts(float delta) {
        //安全检查
        if (projectileImpacts.isEmpty() || batch == null) {
            return;
        }
        if (projectileImpactAnimation == null) {
            projectileImpacts.clear();
            return;
        }
        //反向遍历更新生命周期
        for (int i = projectileImpacts.size - 1; i >= 0; i--) {
            ProjectileImpact impact = projectileImpacts.get(i);
            impact.elapsed += delta;
            //获取当前动画帧
            TextureRegion frame = projectileImpactAnimation.getKeyFrame(impact.elapsed, false);
            if (frame == null || projectileImpactAnimation.isAnimationFinished(impact.elapsed)) {
                projectileImpacts.removeIndex(i);
                continue;
            }
            //渲染
            float width = frame.getRegionWidth();
            float height = frame.getRegionHeight();
            float drawX = impact.position.x - width / 2f;
            float drawY = impact.position.y - height / 2f;
            float originX = width / 2f;
            float originY = height / 2f;
            batch.draw(frame, drawX, drawY, originX, originY, width, height, 1f, 1f, 0f);
        }
    }
    /**
     * 投射物动画帧解析器
     */
    private TextureRegion resolveProjectileFrame(ProjectileView view) {
        if (projectileAnimation != null) {
            return projectileAnimation.getKeyFrame(view.animationTime, true);
        }
        return projectileFallbackRegion;
    }

    /**
     * 判断投射物是否飞出边界
     * @param position
     * @return
     */
    private boolean isProjectileOutOfBounds(Vector2 position) {
        float margin = 32f;
        return position.x < -margin || position.x > WORLD_WIDTH + margin
                || position.y < -margin || position.y > WORLD_HEIGHT + margin;
    }

    /**
     * 指定坐标触发命中特效
     * @param position
     */
    private void spawnImpactEffect(Vector2 position) {
        if (projectileImpactAnimation == null || position == null) {
            return;
        }
        ProjectileImpact impact = new ProjectileImpact();
        impact.position.set(position);
        projectileImpacts.add(impact);
    }

    /**
     * ui资源反馈组件
     * @param delta
     */
    private void renderStatusToast(float delta) {
        //更新倒计时
        if (statusToastTimer > 0f) {
            statusToastTimer -= delta;
        }
        //多重退出条件
        if (statusToastTimer <= 0f || loadingFont == null || statusToastMessage == null
                || statusToastMessage.isBlank() || batch == null) {
            return;
        }
        //文字颜色
        loadingFont.setColor(Color.WHITE);
        //屏幕左上角的世界坐标
        float padding = 20f;
        float drawX = camera.position.x - WORLD_WIDTH / 2f + padding;
        float drawY = camera.position.y + WORLD_HEIGHT / 2f - padding;
        loadingFont.draw(batch, statusToastMessage, drawX, drawY);
    }

    /**
     * 状态提示方法,比如连接成功,技能已经解锁
     * @param message
     */
    private void showStatusToast(String message) {
        if (message == null || message.isBlank()) {
            return;
        }
        statusToastMessage = message;
        statusToastTimer = STATUS_TOAST_DURATION;
    }

    /**
     * 投射物在客户端轻量的视图
     */
    private static final class ProjectileView {
        final long projectileId;
        final Vector2 position = new Vector2();
        final Vector2 velocity = new Vector2();
        float rotationDeg;
        float animationTime;
        long spawnServerTimeMs;
        long expireServerTimeMs;

        ProjectileView(long projectileId) {
            this.projectileId = projectileId;
        }
    }

    /**
     * 投射物命中的瞬间特效
     */
    private static final class ProjectileImpact {
        final Vector2 position = new Vector2();
        float elapsed;
    }

    /**
     * 创建一个占位敌人
     */
    private void spawnPlaceholderEnemy() {
        EnemyView placeholder = new EnemyView(PLACEHOLDER_ENEMY_ID, WORLD_WIDTH, WORLD_HEIGHT);
        placeholder.setVisual(DEFAULT_ENEMY_TYPE_ID,
                resolveEnemyAnimation(DEFAULT_ENEMY_TYPE_ID),
                resolveEnemyAttackAnimation(DEFAULT_ENEMY_TYPE_ID),
                getEnemyFallbackRegion());
        renderBuffer.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f);
        placeholder.snapTo(renderBuffer, TimeUtils.millis());
        enemyViews.put(PLACEHOLDER_ENEMY_ID, placeholder);
    }

    /**
     * 移除占位敌人
     */
    private void removePlaceholderEnemy() {
        enemyViews.remove(PLACEHOLDER_ENEMY_ID);
        enemyLastSeen.remove(PLACEHOLDER_ENEMY_ID);
    }

    /**
     * 移除死亡敌人
     * @param enemyId
     */
    private void removeEnemy(int enemyId) {
        enemyViews.remove(enemyId);
        enemyStateCache.remove(enemyId);
        enemyLastSeen.remove(enemyId);
    }

    /**
     * 清理长时间未更新的敌人
     * @param serverTimeMs
     */
    private void purgeStaleEnemies(long serverTimeMs) {
        Iterator<Map.Entry<Integer, Long>> iterator = enemyLastSeen.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<Integer, Long> entry = iterator.next();
            if ((serverTimeMs - entry.getValue()) > ENEMY_TIMEOUT_MS) {
                int enemyId = entry.getKey();
                iterator.remove();
                enemyViews.remove(enemyId);
                enemyStateCache.remove(enemyId);
            }
        }
    }

    /**
     * 封装一个创建EnemyView对象的方法
     * @param enemyState
     * @return
     */
    private EnemyView ensureEnemyView(Message.EnemyState enemyState) {
        int enemyId = (int) enemyState.getEnemyId();
        EnemyView view = enemyViews.get(enemyId);
        if (view == null) {
            view = new EnemyView(enemyId, WORLD_WIDTH, WORLD_HEIGHT);
            enemyViews.put(enemyId, view);
        }
        return view;
    }

    /**
     * 加载所有动画资源
     */
    private void loadEnemyAssets() {
        disposeEnemyAtlases();
        enemyAnimations.clear();
        enemyAttackAnimations.clear();
        enemyAttackAnimations.clear();
        for (EnemyDefinitions.Definition definition : EnemyDefinitions.all()) {
            Animation<TextureRegion> walkAnimation = createEnemyAnimation(
                    definition.getAtlasPath(),
                    definition.getRegionPrefix(),
                    definition.getFrameDuration());
            if (walkAnimation != null) {
                enemyAnimations.put(definition.getTypeId(), walkAnimation);
            }

            Animation<TextureRegion> attackAnimation = createEnemyAnimation(
                    definition.getAttackAtlasPath(),
                    definition.getAttackRegionPrefix(),
                    definition.getAttackFrameDuration());
            if (attackAnimation != null) {
                enemyAttackAnimations.put(definition.getTypeId(), attackAnimation);
            } else if (walkAnimation != null) {
                enemyAttackAnimations.put(definition.getTypeId(), walkAnimation);
            }
        }
    }

    /**
     * 基于动态配置加载资源
     * @param definition
     * @return
     */
    private Animation<TextureRegion> createEnemyAnimation(String atlasPath,
                                                          String regionPrefix,
                                                          float frameDuration) {
        if (atlasPath == null || regionPrefix == null) {
            return null;
        }
        try {
            TextureAtlas atlas = enemyAtlasCache.computeIfAbsent(
                    atlasPath, path -> new TextureAtlas(Gdx.files.internal(path)));
            Array<TextureAtlas.AtlasRegion> regions = atlas.findRegions(regionPrefix);
            if (regions == null || regions.size == 0) {
                Gdx.app.log(TAG, "Enemy atlas missing region '" + regionPrefix + "' for " + atlasPath);
                return null;
            }
            return new Animation<>(frameDuration, regions, Animation.PlayMode.LOOP);
        } catch (Exception e) {
            Gdx.app.log(TAG, "Failed to load enemy animation: " + atlasPath, e);
            return null;
        }
    }

    /**
     * 查找动画,能找到就用,找不到就降级为默认动画
     * @param typeId
     * @return
     */
    private Animation<TextureRegion> resolveEnemyAnimation(int typeId) {
        Animation<TextureRegion> animation = enemyAnimations.get(typeId);
        if (animation == null) {
            animation = enemyAnimations.get(DEFAULT_ENEMY_TYPE_ID);
        }
        return animation;
    }

    private Animation<TextureRegion> resolveEnemyAttackAnimation(int typeId) {
        Animation<TextureRegion> animation = enemyAttackAnimations.get(typeId);
        if (animation == null) {
            animation = enemyAttackAnimations.get(DEFAULT_ENEMY_TYPE_ID);
        }
        return animation;
    }

    /**
     * 懒加载一个敌人材质的占位防止资源丢失
     * @return
     */
    private TextureRegion getEnemyFallbackRegion() {
        if (enemyFallbackRegion == null) {
            Pixmap pixmap = new Pixmap(72, 72, Pixmap.Format.RGBA8888);
            pixmap.setColor(0.3f, 0.7f, 0.2f, 1f);
            pixmap.fillCircle(36, 36, 34);
            enemyFallbackTexture = new Texture(pixmap);
            enemyFallbackRegion = new TextureRegion(enemyFallbackTexture);
            pixmap.dispose();
        }
        return enemyFallbackRegion;
    }
    /**
     * 清理敌人信息
     */
    private void disposeEnemyAtlases() {
        for (TextureAtlas atlas : enemyAtlasCache.values()) {
            atlas.dispose();
        }
        enemyAtlasCache.clear();
    }

    /**
     * 加载攻击资源
     */
    private void loadProjectileAssets() {
        disposeProjectileAssets();
        try {
            projectileAtlas = new TextureAtlas(Gdx.files.internal("Plants/PeaShooter/Pea/pea.atlas"));
            Array<TextureAtlas.AtlasRegion> regions = projectileAtlas.getRegions();
            if (regions != null && regions.size > 0) {
                projectileAnimation = new Animation<>(0.04f, regions, Animation.PlayMode.LOOP);
            } else {
                projectileAnimation = null;
            }
        } catch (Exception e) {
            projectileAtlas = null;
            projectileAnimation = null;
            Gdx.app.log(TAG, "Failed to load projectile atlas", e);
        }

        if (projectileAnimation == null && projectileFallbackRegion == null) {
            createProjectileFallbackTexture();
        }

        try {
            projectileImpactAtlas = new TextureAtlas(Gdx.files.internal("Zombie/NormalZombie/Attack/attack.atlas"));
            Array<TextureAtlas.AtlasRegion> regions = projectileImpactAtlas.getRegions();
            if (regions != null && regions.size > 0) {
                projectileImpactAnimation = new Animation<>(0.05f, regions, Animation.PlayMode.NORMAL);
            } else {
                projectileImpactAnimation = null;
            }
        } catch (Exception e) {
            projectileImpactAtlas = null;
            projectileImpactAnimation = null;
            Gdx.app.log(TAG, "Failed to load projectile impact atlas", e);
        }
    }

    private void createProjectileFallbackTexture() {
        Pixmap pixmap = new Pixmap(16, 16, Pixmap.Format.RGBA8888);
        pixmap.setColor(Color.CHARTREUSE);
        pixmap.fillCircle(8, 8, 7);
        projectileFallbackTexture = new Texture(pixmap);
        projectileFallbackRegion = new TextureRegion(projectileFallbackTexture);
        pixmap.dispose();
    }

    private void disposeProjectileAssets() {
        if (projectileAtlas != null) {
            projectileAtlas.dispose();
            projectileAtlas = null;
        }
        if (projectileImpactAtlas != null) {
            projectileImpactAtlas.dispose();
            projectileImpactAtlas = null;
        }
        if (projectileFallbackTexture != null) {
            projectileFallbackTexture.dispose();
            projectileFallbackTexture = null;
            projectileFallbackRegion = null;
        }
        projectileAnimation = null;
        projectileImpactAnimation = null;
    }

    /**
     * 绘制角色
     * @param frame
     * @param centerX
     * @param centerY
     * @param faceRight
     */
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

    /**
     * 平衡网络抖动和延迟,使插值不过度滞后
     * @return
     */
    private long computeRenderDelayMs() {
        //计算RTT延迟
        float latencyComponent = smoothedRttMs * 0.25f + 12f;
        if (Float.isNaN(latencyComponent) || Float.isInfinite(latencyComponent)) {
            latencyComponent = 90f;
        }
        latencyComponent = MathUtils.clamp(latencyComponent, 45f, 150f);
        //同步抖动的缓冲储备
        float jitterReserve = Math.abs(smoothedSyncIntervalMs - 33f) * 0.5f
                + (smoothedSyncDeviationMs * 1.3f) + 18f;
        jitterReserve = MathUtils.clamp(jitterReserve, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        //确定延迟
        float target = Math.max(latencyComponent, jitterReserve);
        target = MathUtils.clamp(target, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        //平滑过度到目标值
        float delta = target - renderDelayMs;
        delta = MathUtils.clamp(delta, -MAX_RENDER_DELAY_STEP_MS, MAX_RENDER_DELAY_STEP_MS);
        renderDelayMs = MathUtils.clamp(renderDelayMs + delta * RENDER_DELAY_LERP,
                INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        return Math.round(renderDelayMs);
    }

    /**
     * 抖动日志
     * @param intervalMs
     * @param smoothInterval
     */
    private void logSyncIntervalSpike(float intervalMs, float smoothInterval) {
        //合理的抖动不处理
        if (intervalMs < SYNC_INTERVAL_LOG_THRESHOLD_MS) {
            return;
        }
        long nowMs = TimeUtils.millis();
        if (nowMs - lastSyncLogMs < SYNC_INTERVAL_LOG_INTERVAL_MS) {
            return;
        }
        //只处理异常抖动
        Gdx.app.log(TAG, "Sync interval spike=" + intervalMs + "ms smooth=" + smoothInterval
                + "ms renderDelay=" + renderDelayMs);
        lastSyncLogMs = nowMs;
    }

    /**
     * 防止处理过期或者乱序包
     * @param syncTime
     * @param serverTimeMs
     * @param arrivalMs
     * @return
     */
    private boolean shouldAcceptStatePacket(Message.Timestamp syncTime, long serverTimeMs, long arrivalMs) {
        //标准化tick
        long incomingTick = syncTime != null
                ? Integer.toUnsignedLong(syncTime.getTick())
                : -1L;
        //tick校验
        if (incomingTick >= 0) {
            if (lastAppliedSyncTick >= 0L && Long.compareUnsigned(incomingTick, lastAppliedSyncTick) <= 0) {
                logDroppedSync("tick", incomingTick, serverTimeMs, arrivalMs);
                return false;
            }
            lastAppliedSyncTick = incomingTick;
            if (serverTimeMs > lastAppliedServerTimeMs) {
                lastAppliedServerTimeMs = serverTimeMs;
            }
            return true;
        }
        //tick无效的回退
        if (lastAppliedServerTimeMs >= 0L && serverTimeMs <= lastAppliedServerTimeMs) {
            logDroppedSync("serverTime", serverTimeMs, serverTimeMs, arrivalMs);
            return false;
        }

        lastAppliedServerTimeMs = serverTimeMs;
        return true;
    }

    /**
     * 只处理指定延迟的日志
     * @param reason
     * @param value
     * @param serverTimeMs
     * @param arrivalMs
     */
    private void logDroppedSync(String reason, long value, long serverTimeMs, long arrivalMs) {
        long nowMs = TimeUtils.millis();
        if ((nowMs - lastDroppedSyncLogMs) < DROPPED_SYNC_LOG_INTERVAL_MS) {
            return;
        }
        long sinceLastAccepted = lastSyncArrivalMs == 0L ? -1L : (arrivalMs - lastSyncArrivalMs);
        Gdx.app.log(TAG, "Dropped GameStateSync by " + reason
                + " val=" + value
                + " serverTime=" + serverTimeMs
                + " lastTick=" + lastAppliedSyncTick
                + " lastServerTime=" + lastAppliedServerTimeMs
                + " arrivalDelta=" + sinceLastAccepted);
        lastDroppedSyncLogMs = nowMs;
    }

    /**
     * 进行网络诊断,并提供平滑处理
     * @param arrivalMs
     */
    private void updateSyncArrivalStats(long arrivalMs) {
        //首次不进行处理
        if (lastSyncArrivalMs != 0L) {
            //当前包间隔约等于服务器的发送频率
            float interval = arrivalMs - lastSyncArrivalMs;
            //指数平滑平均间隔---低通滤波
            smoothedSyncIntervalMs += (interval - smoothedSyncIntervalMs) * SYNC_INTERVAL_SMOOTH_ALPHA;
            //计算并平滑抖动
            float deviation = Math.abs(interval - smoothedSyncIntervalMs);
            smoothedSyncDeviationMs += (deviation - smoothedSyncDeviationMs) * SYNC_DEVIATION_SMOOTH_ALPHA;
            //安全处理
            smoothedSyncDeviationMs = MathUtils.clamp(smoothedSyncDeviationMs, 0f, 220f);
            //日志处理
            logSyncIntervalSpike(interval, smoothedSyncIntervalMs);
        }
        //更新到达时间
        lastSyncArrivalMs = arrivalMs;
    }

    /**
     * 正确的估算远程玩家传到本地服务器的时间,进而估算出当前服务器时间
     * @param serverTimeMs
     */
    //TODO:用不用补偿RTT
    private void sampleClockOffset(long serverTimeMs) {
        long now = System.currentTimeMillis();
        float offsetSample = serverTimeMs - now;
        //平滑噪声
        clockOffsetMs = MathUtils.lerp(clockOffsetMs, offsetSample, 0.1f);
    }
    /**
     * 本地时间
     */
    private long estimateServerTimeMs() {
        return (long) (System.currentTimeMillis() + clockOffsetMs);
    }

    /**
     * 确保角色不会走出地图
     * @param position
     */
    private void clampPositionToMap(Vector2 position) {
        if (playerTextureRegion == null) {
            return;
        }

        float halfWidth = playerTextureRegion.getRegionWidth() / 2.0f;
        float halfHeight = playerTextureRegion.getRegionHeight() / 2.0f;

        position.x = MathUtils.clamp(position.x, halfWidth, WORLD_WIDTH - halfWidth);
        position.y = MathUtils.clamp(position.y, halfHeight, WORLD_HEIGHT - halfHeight);
    }

    private static void setVectorFromProto(Message.Vector2 source, Vector2 target) {
        if (source == null || target == null) {
            return;
        }
        target.set(source.getX(), source.getY());
    }

    /**
     * 获取wasd输入
     * @return
     */
    private Vector2 getMovementInput() {
        Vector2 input = new Vector2();
        if (Gdx.input.isKeyPressed(Input.Keys.W)) input.y += 1;
        if (Gdx.input.isKeyPressed(Input.Keys.S)) input.y -= 1;
        if (Gdx.input.isKeyPressed(Input.Keys.A)) input.x -= 1;
        if (Gdx.input.isKeyPressed(Input.Keys.D)) input.x += 1;
        return input.len2() > 0 ? input.nor() : input;
    }

    /**
     * 本地模拟玩家移动
     * @param pos
     * @param rot
     * @param input
     * @param delta
     */
    private void applyInputLocally(Vector2 pos, float rot, PlayerInputCommand input, float delta) {
        if (input.moveDir.len2() > 0.1f) {
            pos.add(input.moveDir.x * PLAYER_SPEED * delta, input.moveDir.y * PLAYER_SPEED * delta);
            clampPositionToMap(pos);
        }
    }

    /**
     * 打包发送
     * @param cmd
     */
    private void sendPlayerInputToServer(PlayerInputCommand cmd) {
        if (game.getPlayerId() <= 0) return;

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

        enqueueInputForSend(inputMsg);
    }

    /**
     * 客户端发送速率限制
     * @param inputMsg
     */
    private void enqueueInputForSend(Message.C2S_PlayerInput inputMsg) {
        long now = TimeUtils.millis();
        if ((now - lastInputSendMs) < MIN_INPUT_SEND_INTERVAL_MS) {
            pendingRateLimitedInput = inputMsg;
            return;
        }
        sendInputImmediately(inputMsg, now);
    }

    /**
    发送代码
     */
    private void sendInputImmediately(Message.C2S_PlayerInput msg, long timestampMs) {
        if (game.trySendPlayerInput(msg)) {
            lastInputSendMs = timestampMs;
            pendingRateLimitedInput = null;
        } else {
            pendingRateLimitedInput = msg;
        }
    }

    /**
    如果本地输入达到了最小发送间隔就打包发出去
     */
    private void pumpPendingNetworkInput() {
        if (pendingRateLimitedInput == null) {
            return;
        }
        long now = TimeUtils.millis();
        if ((now - lastInputSendMs) >= MIN_INPUT_SEND_INTERVAL_MS) {
            sendInputImmediately(pendingRateLimitedInput, now);
        }
    }

    /**
     * 客户端处理服务器全量游戏同步包
     * @param sync
     */
    public void onGameStateReceived(Message.S2C_GameStateSync sync) {
        long arrivalMs = TimeUtils.millis();//包到达的时间
        //安全机制
        Message.Timestamp syncTime = sync.hasSyncTime() ? sync.getSyncTime() : null;
        if (!shouldAcceptStatePacket(syncTime, sync.getServerTimeMs(), arrivalMs)) {
            return;
        }
        updateSyncArrivalStats(arrivalMs);
        sampleClockOffset(sync.getServerTimeMs());
        //处理敌人状态
        if (!sync.getEnemiesList().isEmpty()) {
            for (Message.EnemyState enemy : sync.getEnemiesList()) {
                enemyStateCache.put((int) enemy.getEnemyId(), enemy);
            }
            syncEnemyViews(sync.getEnemiesList(), sync.getServerTimeMs());
        }
        handlePlayersFromServer(sync.getPlayersList(), sync.getServerTimeMs());
    }

    /**
     * 客户端处理增量同步的入口函数
     * @param delta
     */
    public void onGameStateDeltaReceived(Message.S2C_GameStateDeltaSync delta) {
        //接收包获取同步时间
        long arrivalMs = TimeUtils.millis();
        Message.Timestamp syncTime = delta.hasSyncTime() ? delta.getSyncTime() : null;
        //有效性检验
        if (!shouldAcceptStatePacket(syncTime, delta.getServerTimeMs(), arrivalMs)) {
            return;
        }
        //合并玩家增量到完整状态
        List<Message.PlayerState> mergedPlayers = new ArrayList<>(delta.getPlayersCount());
        for (Message.PlayerStateDelta playerDelta : delta.getPlayersList()) {
            Message.PlayerState merged = mergePlayerDelta(playerDelta);
            if (merged != null) {
                mergedPlayers.add(merged);
            }
        }
        //合并敌人增量到完整状态
        List<Message.EnemyState> updatedEnemies = new ArrayList<>(delta.getEnemiesCount());
        for (Message.EnemyStateDelta enemyDelta : delta.getEnemiesList()) {
            Message.EnemyState mergedEnemy = mergeEnemyDelta(enemyDelta);
            if (mergedEnemy != null) {
                updatedEnemies.add(mergedEnemy);
            }
        }
        //网络质量+时钟校准
        updateSyncArrivalStats(arrivalMs);//更新偏移指标
        sampleClockOffset(delta.getServerTimeMs());//估算客户端到服务器的偏移量
        //分发处理状态
        handlePlayersFromServer(mergedPlayers, delta.getServerTimeMs());
        if (!updatedEnemies.isEmpty()) {
            syncEnemyViews(updatedEnemies, delta.getServerTimeMs());
        }
    }

    /**
     * 服务器处理本地玩家和远程玩家状态的核心逻辑
     * @param players
     * @param serverTimeMs
     */
    private void handlePlayersFromServer(Collection<Message.PlayerState> players, long serverTimeMs) {
        //获取本地玩家id
        int myId = game.getPlayerId();
        Message.PlayerState selfStateFromServer = null;
        //遍历所有玩家状态
        if (players != null) {
            for (Message.PlayerState player : players) {
                int playerId = (int) player.getPlayerId();
                serverPlayerStates.put(playerId, player);
                //本地玩家,活着才处理
                if (playerId == myId) {
                    if (player.getIsAlive()) {
                        selfStateFromServer = player;
                    }
                    continue;
                }
                //远程玩家--记录快照
                if (player.hasPosition()) {
                    Vector2 position = new Vector2(player.getPosition().getX(), player.getPosition().getY());
                    pushRemoteSnapshot(playerId, position, player.getRotation(), serverTimeMs);
                    remotePlayerLastSeen.put(playerId, serverTimeMs);
                }
            }
        }
        //清理过期玩家
        purgeStaleRemotePlayers(serverTimeMs);
        //应用服务器对自身的矫正
        if (selfStateFromServer != null) {
            applySelfStateFromServer(selfStateFromServer);
        }
    }

    /**
     * 客户端远程同步敌人信息
     * @param enemies
     * @param serverTimeMs
     */
    private void syncEnemyViews(Collection<Message.EnemyState> enemies, long serverTimeMs) {
        //空判断
        if (enemies == null || enemies.isEmpty()) {
            purgeStaleEnemies(serverTimeMs);
            return;
        }

        removePlaceholderEnemy();
        //遍历敌人状态
        for (Message.EnemyState enemy : enemies) {
            if (enemy == null || !enemy.hasPosition()) {
                continue;
            }
            //敌人死亡处理
            int enemyId = (int) enemy.getEnemyId();
            if (!enemy.getIsAlive()) {
                removeEnemy(enemyId);
                continue;
            }
            EnemyView view = ensureEnemyView(enemy);
            //位置处理
            renderBuffer.set(enemy.getPosition().getX(), enemy.getPosition().getY());
            clampPositionToMap(renderBuffer);
            //渲染服务器状态到视图
            int typeId = (int) enemy.getTypeId();
            view.updateFromServer(
                    typeId,
                    enemy.getIsAlive(),
                    enemy.getHealth(),
                    enemy.getMaxHealth(),
                    renderBuffer,
                    serverTimeMs,
                    resolveEnemyAnimation(typeId),
                    resolveEnemyAttackAnimation(typeId),
                    getEnemyFallbackRegion()
            );
            //记录最后可见时间
            enemyLastSeen.put(enemyId, serverTimeMs);
        }
        //清理过期敌人
        purgeStaleEnemies(serverTimeMs);
    }

    /**
     * 客户端对本地玩家进行服务器状态矫正,差不多就是跟服务器同步的过程
     * @param selfStateFromServer
     */
    private void applySelfStateFromServer(Message.PlayerState selfStateFromServer) {
        //获取服务器位置
        Vector2 serverPos = new Vector2(
                selfStateFromServer.getPosition().getX(),
                selfStateFromServer.getPosition().getY()
        );
        clampPositionToMap(serverPos);
        //提取元数据,处理最后输入的序号
        int lastProcessedSeq = selfStateFromServer.getLastProcessedInputSeq();
        //创建存储状态快照
        PlayerStateSnapshot snapshot = new PlayerStateSnapshot(
                serverPos,
                selfStateFromServer.getRotation(),
                lastProcessedSeq
        );
        snapshotHistory.offer(snapshot);
        while (snapshotHistory.size() > 10) {
            snapshotHistory.poll();
        }
        //状态协调
        reconcileWithServer(snapshot);
    }

    /**
     * 客户端接收远程玩家快照之后构建服务器快照并维护滑动窗口缓存
     * @param playerId
     * @param position
     * @param rotation
     * @param serverTimeMs
     */
    private void pushRemoteSnapshot(int playerId, Vector2 position, float rotation, long serverTimeMs) {
        clampPositionToMap(position);
        //获取快照队列
        Deque<ServerPlayerSnapshot> queue = remotePlayerServerSnapshots
                .computeIfAbsent(playerId, k -> new ArrayDeque<>());
        //速度估算
        Vector2 velocity = new Vector2();
        ServerPlayerSnapshot previous = queue.peekLast();//最新快照
        if (previous != null) {
            long deltaMs = serverTimeMs - previous.serverTimestampMs;
            if (deltaMs > 0) {
                velocity.set(position).sub(previous.position).scl(1000f / deltaMs);
            } else {
                velocity.set(previous.velocity);//用旧速度
            }
            //速度上限限制
            float maxSpeed = PLAYER_SPEED * 1.5f;
            if (velocity.len2() > maxSpeed * maxSpeed) {
                velocity.clamp(0f, maxSpeed);
            }
        }
        //更新朝向
        updateRemoteFacing(playerId, velocity, rotation);
        //创建并存入快照
        ServerPlayerSnapshot snap = new ServerPlayerSnapshot(position, rotation, velocity, serverTimeMs);
        queue.addLast(snap);
        //滑动窗口清理
        while (queue.size() > 1 &&
                (serverTimeMs - queue.peekFirst().serverTimestampMs) > SNAPSHOT_RETENTION_MS) {
            queue.removeFirst();
        }
    }

    /**
     * 更新玩家朝向,设定是只能左右翻转
     * @param playerId
     * @param velocity
     * @param rotation
     */
    private void updateRemoteFacing(int playerId, Vector2 velocity, float rotation) {
        boolean faceRight = remoteFacingRight.getOrDefault(playerId, true);
        if (Math.abs(velocity.x) > 0.001f) {
            faceRight = velocity.x >= 0f;
        } else {
            faceRight = inferFacingFromRotation(rotation);
        }
        remoteFacingRight.put(playerId, faceRight);
    }

    /**
     * 清理长时间未收到消息的过期玩家
     * @param currentServerTimeMs
     */
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
                remoteDisplayPositions.remove(playerId);
                serverPlayerStates.remove(playerId);
            }
        }
    }

    /**
     * 左右翻转核心逻辑-确认角色是否该朝向右边
     * @param rotation
     * @return
     */
    private boolean inferFacingFromRotation(float rotation) {
        float normalized = rotation % 360f;
        if (normalized < 0f) {
            normalized += 360f;
        }
        return !(normalized > 90f && normalized < 270f);
    }

    /**
     * 服务器一致性的操作,包括校正,平滑,重放
     * @param serverSnapshot
     */
    private void reconcileWithServer(PlayerStateSnapshot serverSnapshot) {
        //RTT偏移量的估算
        PlayerInputCommand acknowledged = unconfirmedInputs.get(serverSnapshot.lastProcessedInputSeq);
        if (acknowledged != null) {
            Long sentLogical = inputSendTimes.remove(acknowledged.seq);
            float sample;
            if (sentLogical != null) {
                sample = logicalTimeMs - sentLogical;//游戏逻辑时间
            } else {
                sample = System.currentTimeMillis() - acknowledged.timestampMs;//回退到系统时间
            }
            if (sample > 0f) {
                smoothedRttMs = MathUtils.lerp(smoothedRttMs, sample, 0.2f);
            }
        }
        //应用服务器状态和初始化处理
        float correctionDist = predictedPosition.dst(serverSnapshot.position);
        boolean wasInitialized = hasReceivedInitialState;
        predictedPosition.set(serverSnapshot.position);
        predictedRotation = serverSnapshot.rotation;
        facingRight = inferFacingFromRotation(predictedRotation);
        clampPositionToMap(predictedPosition);
        logServerCorrection(correctionDist, serverSnapshot.lastProcessedInputSeq);
        if (!wasInitialized) {
            clearInitialStateWait();
            displayPosition.set(predictedPosition);//首次同步直接跳转
        }
        //重放未确认输入
        for (PlayerInputCommand input : unconfirmedInputs.values()) {
            if (input.seq > serverSnapshot.lastProcessedInputSeq) {
                applyInputLocally(predictedPosition, predictedRotation, input, input.deltaSeconds);
            }
        }
        //清理已确认输入
        unconfirmedInputs.entrySet().removeIf(entry -> {
            boolean applied = entry.getKey() <= serverSnapshot.lastProcessedInputSeq;
            if (applied) {
                inputSendTimes.remove(entry.getKey());
            }
            return applied;
        });
        pruneUnconfirmedInputs();
        //状态标志更新
        hasReceivedInitialState = true;
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
        //渲染位置平滑过度
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= (DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE * 4f)) {
            displayPosition.set(predictedPosition);
        } else {
            displayPosition.lerp(predictedPosition, 0.35f);
        }
    }

    /**
     * 处理玩家增量变化
     * @param delta
     * @return
     */
    private Message.PlayerState mergePlayerDelta(Message.PlayerStateDelta delta) {
        //空值检查
        if (delta == null) {
            return null;
        }
        int playerId = (int) delta.getPlayerId();
        Message.PlayerState base = serverPlayerStates.get(playerId);
        //玩家首次出现或者已过期
        if (base == null) {
            requestDeltaResync("player_" + playerId);
            return null;
        }
        //更新
        Message.PlayerState.Builder builder = base.toBuilder();
        //位掩码驱动更新
        int mask = delta.getChangedMask();
        if ((mask & PLAYER_DELTA_POSITION_MASK) != 0 && delta.hasPosition()) {
            builder.setPosition(delta.getPosition());
        }
        if ((mask & PLAYER_DELTA_ROTATION_MASK) != 0 && delta.hasRotation()) {
            builder.setRotation(delta.getRotation());
        }
        if ((mask & PLAYER_DELTA_IS_ALIVE_MASK) != 0 && delta.hasIsAlive()) {
            builder.setIsAlive(delta.getIsAlive());
        }
        if ((mask & PLAYER_DELTA_LAST_INPUT_MASK) != 0 && delta.hasLastProcessedInputSeq()) {
            builder.setLastProcessedInputSeq(delta.getLastProcessedInputSeq());
        }
        //更新缓存,返回
        Message.PlayerState updated = builder.build();
        serverPlayerStates.put(playerId, updated);
        return updated;
    }

    /**
     * 处理敌人增量变化
     * @param delta
     * @return
     */
    private Message.EnemyState mergeEnemyDelta(Message.EnemyStateDelta delta) {
        //空值检查
        if (delta == null) {
            return null;
        }
        int enemyId = (int) delta.getEnemyId();
        Message.EnemyState base = enemyStateCache.get(enemyId);
        //敌人首次出现或者状态丢失
        if (base == null) {
            requestDeltaResync("enemy_" + enemyId);
            return null;
        }
        //更新
        Message.EnemyState.Builder builder = base.toBuilder();
        //位掩码驱动状态更新
        int mask = delta.getChangedMask();
        if ((mask & ENEMY_DELTA_POSITION_MASK) != 0 && delta.hasPosition()) {
            builder.setPosition(delta.getPosition());
        }
        if ((mask & ENEMY_DELTA_HEALTH_MASK) != 0 && delta.hasHealth()) {
            builder.setHealth(delta.getHealth());
        }
        if ((mask & ENEMY_DELTA_IS_ALIVE_MASK) != 0 && delta.hasIsAlive()) {
            builder.setIsAlive(delta.getIsAlive());
        }
        //更新缓存,返回
        Message.EnemyState updated = builder.build();
        enemyStateCache.put(enemyId, updated);
        return updated;
    }

    /**
     * 进行全量同步
     * @param reason
     */
    private void requestDeltaResync(String reason) {
        //game有效并且不在冷却时间内
        if (game == null) {
            return;
        }
        long now = TimeUtils.millis();
        if ((now - lastDeltaResyncRequestMs) < DELTA_RESYNC_COOLDOWN_MS) {
            return;
        }
        //更新时间并且全量同步
        lastDeltaResyncRequestMs = now;
        game.requestFullGameStateSync("delta:" + reason);
    }

    private void handleProjectileSpawnEvent(Message.S2C_ProjectileSpawn spawn) {
        if (spawn == null || spawn.getProjectilesCount() == 0) {
            return;
        }
        long serverTimeMs = spawn.getServerTimeMs();
        if (serverTimeMs == 0L) {
            serverTimeMs = estimateServerTimeMs();
        }
        for (Message.ProjectileState state : spawn.getProjectilesList()) {
            if (state == null) {
                continue;
            }
            long projectileId = Integer.toUnsignedLong(state.getProjectileId());
            ProjectileView view = new ProjectileView(projectileId);
            if (state.hasPosition()) {
                setVectorFromProto(state.getPosition(), view.position);
            }
            float rotationDeg = state.getRotation();
            view.rotationDeg = rotationDeg;
            float speed = state.hasProjectile() ? state.getProjectile().getSpeed() : 0f;
            float dirX = MathUtils.cosDeg(rotationDeg);
            float dirY = MathUtils.sinDeg(rotationDeg);
            if (Math.abs(dirX) > 0.001f || Math.abs(dirY) > 0.001f) {
                view.velocity.set(dirX, dirY).nor().scl(speed);
            } else {
                view.velocity.setZero();
            }
            long ttlMs = Math.max(50L, state.getTtlMs());
            view.spawnServerTimeMs = serverTimeMs;
            view.expireServerTimeMs = serverTimeMs + ttlMs;
            projectileViews.put(projectileId, view);
        }
    }

    private void handleProjectileDespawnEvent(Message.S2C_ProjectileDespawn despawn) {
        if (despawn == null || despawn.getProjectilesCount() == 0) {
            return;
        }
        for (Message.ProjectileDespawn entry : despawn.getProjectilesList()) {
            if (entry == null) {
                continue;
            }
            long projectileId = Integer.toUnsignedLong(entry.getProjectileId());
            ProjectileView removed = projectileViews.remove(projectileId);
            boolean isHit = entry.getReason() == Message.ProjectileDespawnReason.PROJECTILE_DESPAWN_HIT;
            if (!isHit) {
                continue;
            }
            if (entry.hasPosition()) {
                projectileTempVector.set(entry.getPosition().getX(), entry.getPosition().getY());
                spawnImpactEffect(projectileTempVector);
            } else if (removed != null) {
                spawnImpactEffect(removed.position);
            }
        }
    }

    private void handlePlayerHurt(Message.S2C_PlayerHurt hurt) {
        if (hurt == null) {
            return;
        }
        int playerId = (int) hurt.getPlayerId();
        Message.PlayerState base = serverPlayerStates.get(playerId);
        if (base != null) {
            Message.PlayerState.Builder builder = base.toBuilder();
            builder.setHealth(hurt.getRemainingHealth());
            serverPlayerStates.put(playerId, builder.build());
        }
        String toast = playerId == game.getPlayerId()
                ? "你受到 " + hurt.getDamage() + " 点伤害，剩余 " + hurt.getRemainingHealth()
                : "玩家 " + playerId + " 受到 " + hurt.getDamage() + " 伤害";
        showStatusToast(toast);
        triggerEnemyAttackAnimation(hurt.getSourceId());
    }

    private void triggerEnemyAttackAnimation(long sourceId) {
        if (sourceId <= 0) {
            return;
        }
        EnemyView attacker = enemyViews.get((int) sourceId);
        if (attacker == null) {
            return;
        }
        if (attacker.isAttackStateSynced()) {
            return;
        }
        long serverTimeMs = estimateServerTimeMs();
        attacker.triggerAttack(serverTimeMs, attacker.getAttackAnimationDurationMs());
    }

    private void handleEnemyAttackStateSync(Message.S2C_EnemyAttackStateSync sync) {
        if (sync == null || sync.getEnemiesCount() == 0) {
            return;
        }
        long serverTimeMs = sync.getServerTimeMs();
        if (serverTimeMs <= 0L) {
            serverTimeMs = estimateServerTimeMs();
        }
        for (Message.EnemyAttackStateDelta delta : sync.getEnemiesList()) {
            if (delta == null) {
                continue;
            }
            int enemyId = (int) delta.getEnemyId();
            EnemyView view = enemyViews.get(enemyId);
            if (view == null) {
                continue;
            }
            long expectedDurationMs = resolveEnemyAttackDurationMs(view);
            view.setAttacking(delta.getIsAttacking(), serverTimeMs, expectedDurationMs);
            if (delta.getIsAttacking()) {
                orientEnemyTowardsTarget(delta.getTargetPlayerId(), view);
            }
        }
    }

    private long resolveEnemyAttackDurationMs(EnemyView view) {
        if (view == null) {
            return 0L;
        }
        EnemyDefinitions.Definition definition = EnemyDefinitions.get(view.getTypeId());
        if (definition != null) {
            float intervalSeconds = Math.max(0f, definition.getAttackIntervalSeconds());
            if (intervalSeconds > 0f) {
                return (long) (intervalSeconds * 1000f);
            }
        }
        return view.getAttackAnimationDurationMs();
    }

    private void orientEnemyTowardsTarget(int targetPlayerId, EnemyView view) {
        if (targetPlayerId <= 0 || view == null) {
            return;
        }
        Message.PlayerState target = serverPlayerStates.get(targetPlayerId);
        if (target == null || !target.hasPosition()) {
            return;
        }
        renderBuffer.set(target.getPosition().getX(), target.getPosition().getY());
        view.faceTowards(renderBuffer);
    }

    private void handleEnemyDied(Message.S2C_EnemyDied died) {
        if (died == null) {
            return;
        }
        int enemyId = (int) died.getEnemyId();
        enemyStateCache.remove(enemyId);
        enemyViews.remove(enemyId);
        if (died.hasPosition()) {
            projectileTempVector.set(died.getPosition().getX(), died.getPosition().getY());
            spawnImpactEffect(projectileTempVector);
        }
        String toast = died.getKillerPlayerId() > 0
                ? "敌人被玩家 " + died.getKillerPlayerId() + " 击败"
                : "敌人 " + enemyId + " 被消灭";
        showStatusToast(toast);
    }

    private void handlePlayerLevelUp(Message.S2C_PlayerLevelUp levelUp) {
        if (levelUp == null) {
            return;
        }
        int playerId = (int) levelUp.getPlayerId();
        Message.PlayerState base = serverPlayerStates.get(playerId);
        if (base != null) {
            Message.PlayerState.Builder builder = base.toBuilder();
            builder.setLevel(levelUp.getNewLevel());
            builder.setExpToNext(levelUp.getExpToNext());
            serverPlayerStates.put(playerId, builder.build());
        }
        String toast = playerId == game.getPlayerId()
                ? "你升级到 Lv." + levelUp.getNewLevel()
                : "玩家 " + playerId + " 升级到 Lv." + levelUp.getNewLevel();
        showStatusToast(toast);
    }

    private void handleDroppedItem(Message.S2C_DroppedItem droppedItem) {
        if (droppedItem == null || droppedItem.getItemsCount() == 0) {
            return;
        }
        showStatusToast("掉落了 " + droppedItem.getItemsCount() + " 个道具");
    }

    private void handleGameOver(Message.S2C_GameOver gameOver) {
        if (gameOver == null) {
            showStatusToast("游戏结束");
        } else {
            showStatusToast(gameOver.getVictory() ? "战斗胜利！" : "战斗失败");
        }
        projectileViews.clear();
        projectileImpacts.clear();
        autoAttackToggle = false;
    }
    /**
     * 游戏环境状态的枚举判断
     * @param type
     * @param message
     */
    public void onGameEvent(Message.MessageType type, Object message) {
        switch (type) {
            case MSG_S2C_PLAYER_HURT:
                handlePlayerHurt((Message.S2C_PlayerHurt) message);
                break;
            case MSG_S2C_ENEMY_DIED:
                handleEnemyDied((Message.S2C_EnemyDied) message);
                break;
            case MSG_S2C_PLAYER_LEVEL_UP:
                handlePlayerLevelUp((Message.S2C_PlayerLevelUp) message);
                break;
            case MSG_S2C_DROPPED_ITEM:
                handleDroppedItem((Message.S2C_DroppedItem) message);
                break;
            case MSG_S2C_GAME_OVER:
                handleGameOver((Message.S2C_GameOver) message);
                break;
            case MSG_S2C_PROJECTILE_SPAWN:
                handleProjectileSpawnEvent((Message.S2C_ProjectileSpawn) message);
                break;
            case MSG_S2C_PROJECTILE_DESPAWN:
                handleProjectileDespawnEvent((Message.S2C_ProjectileDespawn) message);
                break;
            case MSG_S2C_ENEMY_ATTACK_STATE_SYNC:
                handleEnemyAttackStateSync((Message.S2C_EnemyAttackStateSync) message);
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
        disposeEnemyAtlases();
        disposeProjectileAssets();
        enemyAnimations.clear();
        if (enemyFallbackTexture != null) {
            enemyFallbackTexture.dispose();
            enemyFallbackTexture = null;
            enemyFallbackRegion = null;
        }
        enemyViews.clear();
        projectileViews.clear();
        projectileImpacts.clear();
        if (backgroundTexture != null) backgroundTexture.dispose();
        if (loadingFont != null) {
            loadingFont.dispose();
            loadingFont = null;
        }
    }

    /**
     * 位置修正和日志输出
     * @param delta
     */
    private void updateDisplayPosition(float delta) {
        //未初始化则跳过
        if (!hasReceivedInitialState) {
            return;
        }
        //非本地控制角色就直接同步
        if (!isLocallyMoving) {
            displayPosition.set(predictedPosition);
            return;
        }
        //小偏差修正
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE) {
            displayPosition.set(predictedPosition);
            return;
        }
        //大偏差,报警
        float distance = (float) Math.sqrt(distSq);
        if (distance > DISPLAY_DRIFT_LOG_THRESHOLD) {
            logDisplayDrift(distance);
        }
        //平滑插值,向预测位置靠拢
        float alpha = MathUtils.clamp(delta * DISPLAY_LERP_RATE, 0f, 1f);
        displayPosition.lerp(predictedPosition, alpha);
    }

    /**
     * 给予一个稳定的日志输出
     * @param rawDelta
     * @param stableDelta
     */
    private void logFrameDeltaSpike(float rawDelta, float stableDelta) {
        //只处理超过超时限度的报错
        if (Math.abs(rawDelta - stableDelta) < DELTA_SPIKE_THRESHOLD) {
            return;
        }
        //给予一个稳定的日志输出频率
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDeltaSpikeLogMs < DELTA_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Frame delta spike raw=" + rawDelta + " stable=" + stableDelta
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDeltaSpikeLogMs = nowMs;
    }

    /**
     * 输出日志,记录服务器位置校正的诊断函数
     * @param correctionDist
     * @param lastProcessedSeq
     */
    private void logServerCorrection(float correctionDist, int lastProcessedSeq) {
        //过滤微小矫正
        if (correctionDist < POSITION_CORRECTION_LOG_THRESHOLD) {
            return;
        }
        //获取时间,确保日志发送频率
        long nowMs = TimeUtils.millis();
        if (nowMs - lastCorrectionLogMs < POSITION_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Server correction dist=" + correctionDist
                + " lastSeq=" + lastProcessedSeq
                + " pendingInputs=" + unconfirmedInputs.size());
        lastCorrectionLogMs = nowMs;
    }

    /**
     * 输出日志
     * @param drift
     */
    private void logDisplayDrift(float drift) {
        //防御性编程,二次校验漂移阈值
        if (drift < DISPLAY_DRIFT_LOG_THRESHOLD) {
            return;
        }
        //获取时间,确保日志发送频率
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDisplayDriftLogMs < DISPLAY_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Display drift=" + drift + " facingRight=" + facingRight
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDisplayDriftLogMs = nowMs;
    }

    /**
     * 计算本次输入的持续时间
     * @return
     */
    private float resolvePendingDurationSeconds() {
        float elapsed = pendingInputDuration;
        if (pendingInputStartMs > 0L) {
            long elapsedMs = logicalTimeMs - pendingInputStartMs;
            if (elapsedMs > 0L) {
                elapsed = elapsedMs / 1000f;
            }
        }
        return Math.max(elapsed, MIN_COMMAND_DURATION);
    }

    /**
     * 清理玩家输入并发送给服务器后余下的临时变量减少资源消耗
     */
    private void resetPendingInputAccumulator() {
        hasPendingInputChunk = false;
        pendingAttack = false;
        pendingInputDuration = 0f;
        pendingInputStartMs = 0L;
    }

    /**
     * 重置客户端状态并发送全量请求
     */
    private void resetInitialStateTracking() {
        initialStateStartMs = TimeUtils.millis();
        lastInitialStateRequestMs = 0L;
        initialStateWarningLogged = false;
        initialStateCriticalLogged = false;
        initialStateFailureLogged = false;
        initialStateRequestCount = 0;
        lastDeltaResyncRequestMs = 0L;
        maybeSendInitialStateRequest(initialStateStartMs, "initial_enter");
    }

    /**
     * 清理状态,以便退出初始同步状态
     */
    private void clearInitialStateWait() {
        initialStateStartMs = 0L;
        lastInitialStateRequestMs = 0L;
        initialStateWarningLogged = false;
        initialStateCriticalLogged = false;
        initialStateFailureLogged = false;
        initialStateRequestCount = 0;
        lastDeltaResyncRequestMs = 0L;
    }

    /**
     * 获取全量信息并附带获取不到的办法
     */
    private void maybeRequestInitialStateResync() {
        //再判断一下避免误判,如果这个不是那就是角色纹理没加载好
        if (hasReceivedInitialState) {
            return;
        }
        //时间戳
        long now = TimeUtils.millis();
        //如果客户端初始化开始时间还是0就获取一次初始状态
        if (initialStateStartMs == 0L) {
            resetInitialStateTracking();
            return;
        }
        //固定间隔重试
        if ((now - lastInitialStateRequestMs) >= INITIAL_STATE_REQUEST_INTERVAL_MS) {
            maybeSendInitialStateRequest(now, "retry_interval");
        }
        //分级重试,从正常重试到需要抛警告到严重警告
        long waitDuration = now - initialStateStartMs;
        if (!initialStateWarningLogged && waitDuration >= INITIAL_STATE_WARNING_MS) {
            Gdx.app.log(TAG, "Still waiting for first GameStateSync, waitMs=" + waitDuration);
            initialStateWarningLogged = true;
        } else if (!initialStateCriticalLogged && waitDuration >= INITIAL_STATE_CRITICAL_MS) {
            Gdx.app.log(TAG, "Requesting another full sync after waitMs=" + waitDuration);
            initialStateCriticalLogged = true;
            maybeSendInitialStateRequest(now, "retry_critical");
        } else if (!initialStateFailureLogged && waitDuration >= INITIAL_STATE_FAILURE_HINT_MS) {
            Gdx.app.log(TAG, "Long wait for initial sync, consider checking network. waitMs=" + waitDuration);
            initialStateFailureLogged = true;
        }
    }

    /**请求客户端再一次发起全量同步,记录请求次数如果次数过多也会取消重试*/
    private void maybeSendInitialStateRequest(long timestampMs, String reason) {
        initialStateRequestCount++;
        if (game != null) {
            String tag = reason != null ? reason : "unknown";
            game.requestFullGameStateSync("GameScreen:" + tag + "#"+ initialStateRequestCount);
        }
        lastInitialStateRequestMs = timestampMs;
    }

    /**
     * 在游戏初始资源没加载成功的时候加载一个初始界面
     */
    private void renderLoadingOverlay() {
        Gdx.gl.glClearColor(0.06f, 0.06f, 0.08f, 1f);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);
        if (camera == null || batch == null || loadingFont == null || loadingLayout == null) {
            return;
        }
        camera.position.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f, 0f);
        camera.update();
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        String message = getLoadingMessage();
        loadingLayout.setText(loadingFont, message);
        float centerX = WORLD_WIDTH / 2f;
        float centerY = WORLD_HEIGHT / 2f;
        float x = centerX - loadingLayout.width / 2f;
        float y = centerY + loadingLayout.height / 2f;
        loadingFont.setColor(Color.WHITE);
        loadingFont.draw(batch, loadingLayout, x, y);
        batch.end();
    }

    /**
     * 游戏加载问题处理
     * @return
     */
    private String getLoadingMessage() {
        if (initialStateStartMs == 0L) {
            return "加载中...";
        }
        long waitMs = TimeUtils.millis() - initialStateStartMs;
        if (waitMs >= INITIAL_STATE_FAILURE_HINT_MS) {
            return "等待服务器同步超时，请检查网络连接（已请求 " + initialStateRequestCount + " 次）";
        }
        if (waitMs >= INITIAL_STATE_CRITICAL_MS) {
            return "等待服务器同步超时，正在重试...（第 " + initialStateRequestCount + " 次）";
        }
        if (waitMs >= INITIAL_STATE_WARNING_MS) {
            return "正在请求服务器同步...（第 " + initialStateRequestCount + " 次）";
        }
        return "加载中...（第 " + Math.max(1, initialStateRequestCount) + " 次请求）";
    }
}
