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

import java.io.BufferedOutputStream;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.util.*;

public class GameScreen implements Screen {

    private static final String TAG = "GameScreen";
    static {
        configureConsoleEncoding();
    }

    private static final float WORLD_WIDTH = 1280f;
    private static final float WORLD_HEIGHT = 720f;
    // 涓庢湇鍔″櫒 GameManager::SceneConfig.move_speed (200f) 瀵归綈锛岄伩鍏嶉娴?鏉冨▉閫熷害涓嶄竴鑷?
    private static final float PLAYER_SPEED = 200f;

    // 杈撳叆鍒嗘鏇寸粏锛?0~33ms锛夛紝闄嶄綆鏈嶅姟绔爢绉?
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
    private static final float AUTO_ATTACK_INTERVAL = 1f;
    private static final float AUTO_ATTACK_HOLD_TIME = 0.18f;
    private static final float TARGET_REFRESH_INTERVAL = 0.2f;
    private static final float PEA_PROJECTILE_SPEED = 200f;
    private static final float PEA_PROJECTILE_MUZZLE_Y_OFFSET = 18f;
    private static final float PEA_PROJECTILE_MUZZLE_X_OFFSET = 36f;

    private static final int MAX_UNCONFIRMED_INPUTS = 240;
    private static final long MAX_UNCONFIRMED_INPUT_AGE_MS = 1500L;
    private static final long REMOTE_PLAYER_TIMEOUT_MS = 5000L;
    private static final long MIN_INPUT_SEND_INTERVAL_MS = 10L; // ~100Hz

    /*
     * 澧為噺鍚屾,鏍囪瘑鏈嶅姟绔紶鍏ョ殑鍙樺寲鍊?閲囩敤浣嶆帺鐮?鏇存柟渚胯兘鐪嬪嚭鏉ラ渶瑕佷紶鍏ョ殑鍊兼槸鍚﹀彂鐢熶簡鍙樺寲,姣斿浣嶇疆淇℃伅
     * 鑰屼笖杩欎釜鏂瑰紡鏄皢Message涓殑瀛楁缂撳瓨鎴愬彉閲?瑙ｈ€︿唬鐮?     */
    private static final int PLAYER_DELTA_POSITION_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_POSITION_VALUE;
    private static final int PLAYER_DELTA_ROTATION_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_ROTATION_VALUE;
    private static final int PLAYER_DELTA_IS_ALIVE_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_IS_ALIVE_VALUE;
    private static final int PLAYER_DELTA_LAST_INPUT_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_LAST_PROCESSED_INPUT_SEQ_VALUE;

    private static final int ENEMY_DELTA_POSITION_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_POSITION_VALUE;
    private static final int ENEMY_DELTA_HEALTH_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_HEALTH_VALUE;
    private static final int ENEMY_DELTA_IS_ALIVE_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_IS_ALIVE_VALUE;

    private final Vector2 renderBuffer = new Vector2();
    private final Vector2 projectileTempVector = new Vector2();
    private final Vector2 projectileOriginBuffer = new Vector2();
    private final Vector2 projectileDirectionBuffer = new Vector2();
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
    private final Vector2 targetingBuffer = new Vector2();

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
    private boolean autoAttackToggle = true;
    private boolean hasShownGameOver = false;
    private boolean isSelfAlive = true;
    private float autoAttackAccumulator = AUTO_ATTACK_INTERVAL;
    private float autoAttackHoldTimer = 0f;
    private int lockedEnemyId = 0;
    private float targetRefreshTimer = TARGET_REFRESH_INTERVAL;

    private double clockOffsetMs = 0.0;
    private final long localClockBaseMillis = System.currentTimeMillis();
    private final long localClockBaseNano = System.nanoTime();
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
    // 30Hz 鐩爣鍚屾闂撮殧绾?33ms锛岄缃竴涓潬杩戠洰鏍囩殑鍒濆€间究浜庡钩婊?
    private float smoothedSyncIntervalMs = 35f;
    private float smoothedSyncDeviationMs = 30f;
    private Message.C2S_PlayerInput pendingRateLimitedInput;
    private long lastInputSendMs = 0L;
    private String statusToastMessage = "";
    private float statusToastTimer = 0f;
    private static final float STATUS_TOAST_DURATION = 2.75f;
    private String lastProjectileDebugReason = "";
    private long lastProjectileDebugLogMs = 0L;

    public GameScreen(Main game) {
        this.game = Objects.requireNonNull(game);
    }

    @Override
    public void show() {
        camera = new OrthographicCamera();
        viewport = new FitViewport(WORLD_WIDTH, WORLD_HEIGHT, camera);
        batch = new SpriteBatch();//缁樺埗浜虹墿鐢ㄧ殑
        loadingFont = new BitmapFont();//缁樺埗瀛椾綋
        loadingFont.getData().setScale(1.3f);//瀛椾綋鏀惧ぇ1.3鍊?
        loadingLayout = new GlyphLayout();//甯冨眬鏂囨湰,鏄竴绉嶆瘮杈冮珮绾х殑甯冨眬

        //鑳屾櫙
        try {
            backgroundTexture = new Texture(Gdx.files.internal("background/roomListBackground.png"));
        } catch (Exception e) {
            Pixmap bgPixmap = new Pixmap((int) WORLD_WIDTH, (int) WORLD_HEIGHT, Pixmap.Format.RGBA8888);//璁剧疆绾壊鍥?
            bgPixmap.setColor(0.05f, 0.15f, 0.05f, 1f);
            bgPixmap.fill();
            backgroundTexture = new Texture(bgPixmap);
            bgPixmap.dispose();
        }

        //浜虹墿
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
        //鍔犺浇璧勬簮
        loadEnemyAssets();
        loadProjectileAssets();
        enemyViews.clear();
        enemyLastSeen.clear();
        projectileViews.clear();
        projectileImpacts.clear();
        resetTargetingState();
        spawnPlaceholderEnemy();

        //璁剧疆浜虹墿鍒濆浣嶇疆涓哄湴鍥句腑澶?
        predictedPosition.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f);
        displayPosition.set(predictedPosition);
        resetInitialStateTracking();
        resetAutoAttackState();
        isSelfAlive = true;
    }

    @Override
    /**
    娓告垙涓诲惊鐜?     */
    public void render(float delta) {
        advanceLogicalClock(delta);//鏇存柊涓€涓ǔ瀹氱殑鏃堕棿璺冲彉
        pumpPendingNetworkInput();

        /*
        hasReceivedInitialState:娓告垙鍒濆鐘舵€?        playerTextureRegion:瑙掕壊绾圭悊
         */
        if (!hasReceivedInitialState || playerTextureRegion == null) {
            maybeRequestInitialStateResync();
            renderLoadingOverlay();
            return;
        }
        /*
        閲囬泦鏈湴杈撳叆
         */
        float renderDelta = getStableDelta(delta);
        Vector2 dir = getMovementInput();
        isLocallyMoving = dir.len2() > 0.0001f;//鍒ゆ柇鏄惁鍦ㄧЩ鍔?
        if (Gdx.input.isKeyJustPressed(AUTO_ATTACK_TOGGLE_KEY)) {
            autoAttackToggle = !autoAttackToggle;
            if (autoAttackToggle) {
                resetAutoAttackState();
            } else {
                autoAttackAccumulator = 0f;
                autoAttackHoldTimer = 0f;
            }
            showStatusToast(autoAttackToggle ? "鑷姩鏀诲嚮宸插紑鍚?" : "姩鏀诲嚮宸插叧闂?");
        }
        boolean attacking = resolveAttackingState(renderDelta);

        /*
        棰勬祴鎿嶄綔
         */
        simulateLocalStep(dir, renderDelta);
        processInputChunk(dir, attacking, renderDelta);

        /*
        娓呭睆鍔犵浉鏈鸿窡闅?         */
        Gdx.gl.glClearColor(0, 0, 0, 1);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);

        clampPositionToMap(predictedPosition);
        updatePlayerFacing(renderDelta);
        updateDisplayPosition(renderDelta);
        clampPositionToMap(displayPosition);
        camera.position.set(displayPosition.x, displayPosition.y, 0);
        camera.update();

        /*
        寮€濮嬫覆鏌?         */
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        batch.draw(backgroundTexture, 0, 0, WORLD_WIDTH, WORLD_HEIGHT);
        /*
        鎾斁鐜╁绌洪棽鍔ㄧ敾
         */
        playerAnimationTime += renderDelta;
        TextureRegion currentFrame = playerIdleAnimation != null
                ? playerIdleAnimation.getKeyFrame(playerAnimationTime, true)//寰幆鎾斁
                : playerTextureRegion;
        if (currentFrame == null) {
            batch.end();
            return;
        }
        playerTextureRegion = currentFrame;
        /*
        浼扮畻鏈嶅姟鍣ㄦ椂闂?璁＄畻娓叉煋寤惰繜
         */
        long estimatedServerTimeMs = estimateServerTimeMs();
        long renderServerTimeMs = estimatedServerTimeMs - computeRenderDelayMs();
        updateProjectiles(renderDelta, renderServerTimeMs);
        /*
        娓叉煋鏁屼汉鍜岀帺瀹?         */
        renderEnemies(renderDelta, renderServerTimeMs);
        renderRemotePlayers(renderServerTimeMs, currentFrame, renderDelta);
        renderProjectiles();
        renderProjectileImpacts(renderDelta);
        /*
        娓叉煋鏈湴鐜╁瑙掕壊
         */
        if (isSelfAlive) {
            drawCharacterFrame(currentFrame, displayPosition.x, displayPosition.y, facingRight);
        }
        renderStatusToast(renderDelta);

        batch.end();
    }

    /**
     * 鏇存柊涓€涓ǔ瀹氱殑鏃堕棿璺冲彉
     * @param delta
     */
    private void advanceLogicalClock(float delta) {
        double total = delta * 1000.0 + logicalClockRemainderMs;
        long advance = (long) total;
        logicalClockRemainderMs = total - advance;
        logicalTimeMs += advance;
    }

    /**
     * 骞虫粦鎿嶄綔
     * @param rawDelta
     * @return
     */
    private float getStableDelta(float rawDelta) {
        float clamped = Math.min(rawDelta, MAX_FRAME_DELTA);//璁剧疆鏈€澶у抚闂撮殧闃叉鏋佸ぇ甯?
        smoothedFrameDelta += (clamped - smoothedFrameDelta) * DELTA_SMOOTH_ALPHA;//鎸囨暟骞虫粦鎿嶄綔,鍒╃敤IIR浣庨€氭护娉㈠櫒
        logFrameDeltaSpike(rawDelta, smoothedFrameDelta);//鏃ュ織杈撳嚭
        return smoothedFrameDelta;
    }

    /**
     * 棰勬祴鎿嶄綔,鏈湴鐩存帴鐢ㄩ娴嬭繘琛岀Щ鍔?     * @param dir
     * @param delta
     */
    private void simulateLocalStep(Vector2 dir, float delta) {
        if (dir.len2() > 0.1f) {
            predictedPosition.add(dir.x * PLAYER_SPEED * delta, dir.y * PLAYER_SPEED * delta);
            predictedRotation = dir.angleDeg();
            clampPositionToMap(predictedPosition);
        }
    }

    /**
     * 鎵撳寘鍙戦€佺帺瀹惰緭鍏?     * @param dir
     * @param attacking
     * @param delta
     */
    private void processInputChunk(Vector2 dir, boolean attacking, float delta) {
        boolean moving = dir.len2() > 0.0001f;//鍒ゆ柇鏄惁鍦ㄧЩ鍔?        //鏈Щ鍔ㄦ湭鏀诲嚮
        if (!moving && !attacking) {
            if (hasPendingInputChunk) {
                flushPendingInput();
            }
            if (!idleAckSent) {
                sendIdleCommand(delta);
            }
            return;
        }
        //閲嶇疆绌洪棽鏍囧織
        idleAckSent = false;
        //棣栨鎿嶄綔
        if (!hasPendingInputChunk) {
            startPendingChunk(dir, attacking, delta);
            if (pendingAttack) {
                flushPendingInput();
            }
            return;
        }
        //鍒ゆ柇鏄惁鍙悎骞?
        if (pendingMoveDir.epsilonEquals(dir, 0.001f) && pendingAttack == attacking) {
            pendingInputDuration += delta;//鍙互,寤堕暱鎸佺画鏃堕棿
        } else {
            flushPendingInput();//涓嶅彲浠?缁撴潫鏃у潡
            startPendingChunk(dir, attacking, delta);//寮€濮嬫柊鍧?
        }
        //鏀诲嚮鍜屾寜閿寔缁椂闂磋秴杩囦笂闄?
            if (pendingAttack || pendingInputDuration >= MAX_COMMAND_DURATION) {
            flushPendingInput();
        }
    }

    /**
     * 鍒濆鍖栨墍闇€鐘舵€?     * @param dir
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
     * 灏嗗鎴风鐨勮緭鍏ユ墦鍖呭彂缁欐湇鍔″櫒
     */
    private void flushPendingInput() {
        //妫€鏌ユ槸鍚︽湁寰呭彂閫佺殑杈撳叆
        if (!hasPendingInputChunk) {
            return;
        }
        float duration = resolvePendingDurationSeconds();
        PlayerInputCommand cmd = new PlayerInputCommand(inputSequence++, pendingMoveDir, pendingAttack, duration);
        unconfirmedInputs.put(cmd.seq, cmd);//瀛樺叆鏈‘璁よ緭鍏ョ殑缂撳瓨
        inputSendTimes.put(cmd.seq, logicalTimeMs);
        pruneUnconfirmedInputs();
        sendPlayerInputToServer(cmd);
        resetPendingInputAccumulator();
    }

    /**
     * 鍦ㄦ棤杈撳叆鏃跺悜鏈嶅姟鍣ㄥ彂閫佸績璺?     * @param delta
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
     * 娓呯悊鏈‘璁ら槦鍒?     */
    private void pruneUnconfirmedInputs() {
        if (unconfirmedInputs.isEmpty()) {
            return;
        }

        /*
        瀹夊叏杩唬鍒犻櫎杩囨湡鏉＄洰
         */
        long now = logicalTimeMs;
        Iterator<Map.Entry<Integer, PlayerInputCommand>> iterator = unconfirmedInputs.entrySet().iterator();
        boolean removed = false;
        while (iterator.hasNext()) {
            Map.Entry<Integer, PlayerInputCommand> entry = iterator.next();
            int seq = entry.getKey();
            long sentAt = inputSendTimes.getOrDefault(seq, entry.getValue().timestampMs);
            /*
            鍒ゆ柇鏄惁鐪熺殑闇€瑕佸垹闄?             */
            boolean tooOld = (now - sentAt) > MAX_UNCONFIRMED_INPUT_AGE_MS;
            boolean overflow = unconfirmedInputs.size() > MAX_UNCONFIRMED_INPUTS;
            if (tooOld || overflow) {
                iterator.remove();
                inputSendTimes.remove(seq);
                removed = true;
                continue;
            }
            //鎻愬墠缁堟閬嶅巻
            if (!tooOld && !overflow) {
                break;
            }
        }

        /*
        鏃ュ織涓庣┖闂茬姸鎬佸鐞?         */
        if (removed) {
            Gdx.app.log(TAG, "Pruned stale inputs, remaining=" + unconfirmedInputs.size());
        }
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
    }

    /**
     * 娓叉煋鍏朵粬鐜╁
     * @param renderServerTimeMs
     * @param frame
     * @param delta
     */
    private void renderRemotePlayers(long renderServerTimeMs, TextureRegion frame, float delta) {
        //绌烘鏌?
        if (frame == null) {
            return;
        }
        //閬嶅巻姣忎釜鐜╁鐨勫揩鐓?
         for(Map.Entry<Integer, Deque<ServerPlayerSnapshot>> entry : remotePlayerServerSnapshots.entrySet()) {
            int playerId = entry.getKey();
            Message.PlayerState state = serverPlayerStates.get(playerId);
            if (state != null && !state.getIsAlive()) {
                continue;
            }
            Deque<ServerPlayerSnapshot> snapshots = entry.getValue();
            if (snapshots == null || snapshots.isEmpty()) {
                continue;
            }
            //鎻掑€煎尯闂?
             ServerPlayerSnapshot prev = null;
            ServerPlayerSnapshot next = null;
            for (ServerPlayerSnapshot snap : snapshots) {
                if (snap.serverTimestampMs <= renderServerTimeMs) {
                    prev = snap;//鏈€鏂扮殑鏃у揩鐓?
                    } else {
                    next = snap;//涓嬩竴涓揩鐓?
                    break;
                }
            }

            Vector2 targetPos = renderBuffer;
            /*
             鎻掑€艰绠楃瓥鐣?             */
            //鏈変笂涓€涓拰涓嬩竴涓氨骞虫粦鎻掍腑鍊?
             if (prev != null && next != null && next.serverTimestampMs > prev.serverTimestampMs) {
                float t = (renderServerTimeMs - prev.serverTimestampMs) /
                        (float) (next.serverTimestampMs - prev.serverTimestampMs);
                t = MathUtils.clamp(t, 0f, 1f);
                targetPos.set(prev.position).lerp(next.position, t);
            }//鍙湁prev灏遍娴嬩竴涓柊鍊
             else if (prev != null) {
                long ahead = Math.max(0L, renderServerTimeMs - prev.serverTimestampMs);
                long clampedAhead = Math.min(ahead, MAX_EXTRAPOLATION_MS);
                float seconds = clampedAhead / 1000f;
                targetPos.set(prev.position).mulAdd(prev.velocity, seconds);
            }//鍙湁鏂板€煎氨鐩存帴鐢?
             else {
                targetPos.set(next.position);
            }
            clampPositionToMap(targetPos);
            boolean remoteFacing = remoteFacingRight.getOrDefault(playerId, true);
            //鑾峰彇鍒濆鍖栦綅缃紦瀛?
             Vector2 displayPos = remoteDisplayPositions
                    .computeIfAbsent(playerId, id -> new Vector2(targetPos));
            //骞虫粦杩囧害,灞炰簬鏄繚闄╃殑淇濋櫓,闃叉鍥犲寘璺宠穬閫犳垚鐨勭獊鍙?
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
     * 娓叉煋鏁屼汉
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
        long currentClientTimeMs = logicalTimeMs;
        Iterator<Map.Entry<Long, ProjectileView>> iterator = projectileViews.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<Long, ProjectileView> entry = iterator.next();
            ProjectileView view = entry.getValue();
            view.animationTime += delta;
            view.position.mulAdd(view.velocity, delta);
            boolean expiredClient = view.expireClientTimeMs > 0 && currentClientTimeMs >= view.expireClientTimeMs;
            boolean expiredServer = view.expireServerTimeMs > 0 && serverTimeMs >= view.expireServerTimeMs;
            if (expiredServer) {
                long clientTtl = view.expireClientTimeMs - view.spawnClientTimeMs;
                if (!expiredClient && view.expireClientTimeMs > currentClientTimeMs) {
                    expiredServer = false;
                } else if (view.spawnServerTimeMs > 0) {
                    long serverTtl = Math.max(0L, view.expireServerTimeMs - view.spawnServerTimeMs);
                    long serverElapsed = serverTimeMs - view.spawnServerTimeMs;
                    if (serverTtl > 0 && (serverElapsed - serverTtl) > 250L) {
                        expiredServer = false;
                    }
                } else if (clientTtl > 0 && !expiredClient) {
                    expiredServer = false;
                }
            }
            boolean outOfBounds = isProjectileOutOfBounds(view.position);
            boolean expired = expiredClient || expiredServer;
            if (expired || outOfBounds) {
                String reason;
                if (outOfBounds) {
                    reason = "out_of_bounds";
                } else if (expiredClient) {
                    reason = "expired_client";
                } else {
                    reason = "expired_server";
                }
                Gdx.app.log(TAG, "ProjectileRemoved id=" + view.projectileId
                        + " reason=" + reason
                        + " pos=" + view.position
                        + " serverTime=" + serverTimeMs
                        + " clientTime=" + currentClientTimeMs);
                iterator.remove();
            }
        }
    }

    /**
     * 鎵归噺娓叉煋鎶曞皠鐗?     */
    private void renderProjectiles() {
        //鍓嶇疆鏉′欢
        if (projectileViews.isEmpty() || batch == null) {
            logProjectileRenderState(projectileViews.isEmpty() ? "skip_empty" : "skip_batch_null");
            return;
        }
        //閬嶅巻鎶曞皠鐗?
        for (ProjectileView view : projectileViews.values()) {
            TextureRegion frame = resolveProjectileFrame(view);
            //鍔ㄦ€佽В鏋愯创鍥惧抚
            if (frame == null) {
                logProjectileRenderState("skip_frame_null");
                continue;
            }
            //鑾峰彇灏哄缁樺埗浣嶇疆
            float width = frame.getRegionWidth();
            float height = frame.getRegionHeight();
            float drawX = view.position.x - width / 2f;
            float drawY = view.position.y - height / 2f;
            float originX = width / 2f;
            float originY = height / 2f;
            batch.draw(frame, drawX, drawY, originX, originY, width, height,
                    1f, 1f, view.rotationDeg);
        }
        logProjectileRenderState("render");
    }

    /**
     * 娓叉煋鎶曞皠鐗╁懡涓洰鏍囨椂鐨勭灛闂寸壒鏁?     * @param delta
     */
    private void renderProjectileImpacts(float delta) {
        //瀹夊叏妫€鏌?
        if (projectileImpacts.isEmpty() || batch == null) {
            return;
        }
        if (projectileImpactAnimation == null) {
            projectileImpacts.clear();
            return;
        }
        //鍙嶅悜閬嶅巻鏇存柊鐢熷懡鍛ㄦ湡
        for (int i = projectileImpacts.size - 1; i >= 0; i--) {
            ProjectileImpact impact = projectileImpacts.get(i);
            impact.elapsed += delta;
            //鑾峰彇褰撳墠鍔ㄧ敾甯?
            TextureRegion frame = projectileImpactAnimation.getKeyFrame(impact.elapsed, false);
            if (frame == null || projectileImpactAnimation.isAnimationFinished(impact.elapsed)) {
                projectileImpacts.removeIndex(i);
                continue;
            }
            //娓叉煋
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
     * 鎶曞皠鐗╁姩鐢诲抚瑙ｆ瀽鍣?     */
    private TextureRegion resolveProjectileFrame(ProjectileView view) {
        if (projectileAnimation != null) {
            return projectileAnimation.getKeyFrame(view.animationTime, true);
        }
        return projectileFallbackRegion;
    }

    /**
     * 鍒ゆ柇鎶曞皠鐗╂槸鍚﹂鍑鸿竟鐣?     * @param position
     * @return
     */
    private boolean isProjectileOutOfBounds(Vector2 position) {
        float margin = 32f;
        return position.x < -margin || position.x > WORLD_WIDTH + margin
                || position.y < -margin || position.y > WORLD_HEIGHT + margin;
    }

    /**
     * 鎸囧畾鍧愭爣瑙﹀彂鍛戒腑鐗规晥
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
     * ui璧勬簮鍙嶉缁勪欢
     * @param delta
     */
    private void renderStatusToast(float delta) {
        //鏇存柊鍊掕鏃?
        if (statusToastTimer > 0f) {
            statusToastTimer -= delta;
        }
        //澶氶噸閫€鍑烘潯浠?
        if (statusToastTimer <= 0f || loadingFont == null || statusToastMessage == null
                || statusToastMessage.isBlank() || batch == null) {
            return;
        }
        //鏂囧瓧棰滆壊
        loadingFont.setColor(Color.WHITE);
        //灞忓箷宸︿笂瑙掔殑涓栫晫鍧愭爣
        float padding = 20f;
        float drawX = camera.position.x - WORLD_WIDTH / 2f + padding;
        float drawY = camera.position.y + WORLD_HEIGHT / 2f - padding;
        loadingFont.draw(batch, statusToastMessage, drawX, drawY);
    }

    /**
     * 鐘舵€佹彁绀烘柟娉?姣斿杩炴帴鎴愬姛,鎶€鑳藉凡缁忚В閿?     * @param message
     */
    private void showStatusToast(String message) {
        if (message == null || message.isBlank()) {
            return;
        }
        statusToastMessage = message;
        statusToastTimer = STATUS_TOAST_DURATION;
    }

    private void logProjectileRenderState(String reason) {
        long now = TimeUtils.millis();
        if (reason.equals(lastProjectileDebugReason)
                && (now - lastProjectileDebugLogMs) < 400L) {
            return;
        }
        lastProjectileDebugReason = reason;
        lastProjectileDebugLogMs = now;
        ProjectileView sample = null;
        if (!projectileViews.isEmpty()) {
            sample = projectileViews.values().iterator().next();
        }
        StringBuilder builder = new StringBuilder("[ProjectileRender] reason=")
                .append(reason)
                .append(" views=").append(projectileViews.size())
                .append(" animReady=").append(projectileAnimation != null)
                .append(" batchNull=").append(batch == null);
        if (camera != null) {
            builder.append(" cameraPos=")
                    .append('(')
                    .append(camera.position.x).append(',')
                    .append(camera.position.y).append(',')
                    .append(camera.position.z).append(')');
        }
        if (sample != null) {
            builder.append(" samplePos=").append(sample.position)
                    .append(" sampleVel=").append(sample.velocity)
                    .append(" ttl(ms)=").append(sample.expireClientTimeMs - logicalTimeMs);
        }
        Gdx.app.log(TAG, builder.toString());
    }

    /**
     * 鎶曞皠鐗╁湪瀹㈡埛绔交閲忕殑瑙嗗浘
     */
    private static final class ProjectileView {
        final long projectileId;
        final Vector2 position = new Vector2();
        final Vector2 velocity = new Vector2();
        float rotationDeg;
        float animationTime;
        long spawnServerTimeMs;
        long expireServerTimeMs;
        long spawnClientTimeMs;
        long expireClientTimeMs;

        ProjectileView(long projectileId) {
            this.projectileId = projectileId;
        }
    }

    /**
     * 鎶曞皠鐗╁懡涓殑鐬棿鐗规晥
     */
    private static final class ProjectileImpact {
        final Vector2 position = new Vector2();
        float elapsed;
    }

    /**
     * 鍒涘缓涓€涓崰浣嶆晫浜?     */
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
     * 绉婚櫎鍗犱綅鏁屼汉
     */
    private void removePlaceholderEnemy() {
        enemyViews.remove(PLACEHOLDER_ENEMY_ID);
        enemyLastSeen.remove(PLACEHOLDER_ENEMY_ID);
    }

    /**
     * 绉婚櫎姝讳骸鏁屼汉
     * @param enemyId
     */
    private void removeEnemy(int enemyId) {
        enemyViews.remove(enemyId);
        enemyStateCache.remove(enemyId);
        enemyLastSeen.remove(enemyId);
        if (enemyId == lockedEnemyId) {
            lockedEnemyId = 0;
        }
    }

    /**
     * 娓呯悊闀挎椂闂存湭鏇存柊鐨勬晫浜?     * @param serverTimeMs
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
     * 灏佽涓€涓垱寤篍nemyView瀵硅薄鐨勬柟娉?     * @param enemyState
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
     * 鍔犺浇鎵€鏈夊姩鐢昏祫婧?     */
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
     * 鍩轰簬鍔ㄦ€侀厤缃姞杞借祫婧?     * @param definition
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
     * 鏌ユ壘鍔ㄧ敾,鑳芥壘鍒板氨鐢?鎵句笉鍒板氨闄嶇骇涓洪粯璁ゅ姩鐢?     * @param typeId
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
     * 鎳掑姞杞戒竴涓晫浜烘潗璐ㄧ殑鍗犱綅闃叉璧勬簮涓㈠け
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
     * 娓呯悊鏁屼汉淇℃伅
     */
    private void disposeEnemyAtlases() {
        for (TextureAtlas atlas : enemyAtlasCache.values()) {
            atlas.dispose();
        }
        enemyAtlasCache.clear();
    }

    /**
     * 鍔犺浇鏀诲嚮璧勬簮
     */
    private void loadProjectileAssets() {
        disposeProjectileAssets();
        Array<TextureAtlas.AtlasRegion> projectileRegions = null;
        try {
            projectileAtlas = new TextureAtlas(Gdx.files.internal("Plants/PeaShooter/Pea/pea.atlas"));
            projectileRegions = projectileAtlas.getRegions();
            if (projectileRegions != null && projectileRegions.size > 0) {
                projectileAnimation = new Animation<>(0.04f, projectileRegions, Animation.PlayMode.LOOP);
            } else {
                projectileAnimation = null;
            }
        } catch (Exception e) {
            projectileAtlas = null;
            projectileAnimation = null;
            projectileRegions = null;
            Gdx.app.log(TAG, "Failed to load projectile atlas", e);
        }

        if (projectileAnimation == null && projectileFallbackRegion == null) {
            createProjectileFallbackTexture();
        }

        if (projectileRegions != null && projectileRegions.size > 0) {
            projectileImpactAtlas = projectileAtlas;
            projectileImpactAnimation = new Animation<>(0.05f,
                    new Array<>(projectileRegions),
                    Animation.PlayMode.NORMAL);
        } else {
            projectileImpactAtlas = null;
            projectileImpactAnimation = null;
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
        if (projectileImpactAtlas != null && projectileImpactAtlas != projectileAtlas) {
            projectileImpactAtlas.dispose();
        }
        projectileImpactAtlas = null;
        if (projectileAtlas != null) {
            projectileAtlas.dispose();
            projectileAtlas = null;
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
     * 缁樺埗瑙掕壊
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
     * 骞宠　缃戠粶鎶栧姩鍜屽欢杩?浣挎彃鍊间笉杩囧害婊炲悗
     * @return
     */
    private long computeRenderDelayMs() {
        //璁＄畻RTT寤惰繜
        float latencyComponent = smoothedRttMs * 0.25f + 12f;
        if (Float.isNaN(latencyComponent) || Float.isInfinite(latencyComponent)) {
            latencyComponent = 90f;
        }
        latencyComponent = MathUtils.clamp(latencyComponent, 45f, 150f);
        //鍚屾鎶栧姩鐨勭紦鍐插偍澶?
        float jitterReserve = Math.abs(smoothedSyncIntervalMs - 33f) * 0.5f
                + (smoothedSyncDeviationMs * 1.3f) + 18f;
        jitterReserve = MathUtils.clamp(jitterReserve, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        //纭畾寤惰繜
        float target = Math.max(latencyComponent, jitterReserve);
        target = MathUtils.clamp(target, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        //骞虫粦杩囧害鍒扮洰鏍囧€?
        float delta = target - renderDelayMs;
        delta = MathUtils.clamp(delta, -MAX_RENDER_DELAY_STEP_MS, MAX_RENDER_DELAY_STEP_MS);
        renderDelayMs = MathUtils.clamp(renderDelayMs + delta * RENDER_DELAY_LERP,
                INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        return Math.round(renderDelayMs);
    }

    /**
     * 鎶栧姩鏃ュ織
     * @param intervalMs
     * @param smoothInterval
     */
    private void logSyncIntervalSpike(float intervalMs, float smoothInterval) {
        //鍚堢悊鐨勬姈鍔ㄤ笉澶勭悊
        if (intervalMs < SYNC_INTERVAL_LOG_THRESHOLD_MS) {
            return;
        }
        long nowMs = TimeUtils.millis();
        if (nowMs - lastSyncLogMs < SYNC_INTERVAL_LOG_INTERVAL_MS) {
            return;
        }
        //鍙鐞嗗紓甯告姈鍔?
        Gdx.app.log(TAG, "Sync interval spike=" + intervalMs + "ms smooth=" + smoothInterval
                + "ms renderDelay=" + renderDelayMs);
        lastSyncLogMs = nowMs;
    }

    /**
     * 闃叉澶勭悊杩囨湡鎴栬€呬贡搴忓寘
     * @param syncTime
     * @param serverTimeMs
     * @param arrivalMs
     * @return
     */
    private boolean shouldAcceptStatePacket(Message.Timestamp syncTime, long serverTimeMs, long arrivalMs) {
        //鏍囧噯鍖杢ick
        long incomingTick = syncTime != null
                ? Integer.toUnsignedLong(syncTime.getTick())
                : -1L;
        //tick鏍￠獙
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
        //tick鏃犳晥鐨勫洖閫€
        if (lastAppliedServerTimeMs >= 0L && serverTimeMs <= lastAppliedServerTimeMs) {
            logDroppedSync("serverTime", serverTimeMs, serverTimeMs, arrivalMs);
            return false;
        }

        lastAppliedServerTimeMs = serverTimeMs;
        return true;
    }

    /**
     * 鍙鐞嗘寚瀹氬欢杩熺殑鏃ュ織
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
     * 杩涜缃戠粶璇婃柇,骞舵彁渚涘钩婊戝鐞?     * @param arrivalMs
     */
    private void updateSyncArrivalStats(long arrivalMs) {
        //棣栨涓嶈繘琛屽鐞?
        if (lastSyncArrivalMs != 0L) {
            //褰撳墠鍖呴棿闅旂害绛変簬鏈嶅姟鍣ㄧ殑鍙戦€侀鐜?
            float interval = arrivalMs - lastSyncArrivalMs;
            //鎸囨暟骞虫粦骞冲潎闂撮殧---浣庨€氭护娉?
            smoothedSyncIntervalMs += (interval - smoothedSyncIntervalMs) * SYNC_INTERVAL_SMOOTH_ALPHA;
            //璁＄畻骞跺钩婊戞姈鍔?
            float deviation = Math.abs(interval - smoothedSyncIntervalMs);
            smoothedSyncDeviationMs += (deviation - smoothedSyncDeviationMs) * SYNC_DEVIATION_SMOOTH_ALPHA;
            //瀹夊叏澶勭悊
            smoothedSyncDeviationMs = MathUtils.clamp(smoothedSyncDeviationMs, 0f, 220f);
            //鏃ュ織澶勭悊
            logSyncIntervalSpike(interval, smoothedSyncIntervalMs);
        }
        //鏇存柊鍒拌揪鏃堕棿
        lastSyncArrivalMs = arrivalMs;
    }

    /**
     * 姝ｇ‘鐨勪及绠楄繙绋嬬帺瀹朵紶鍒版湰鍦版湇鍔″櫒鐨勬椂闂?杩涜€屼及绠楀嚭褰撳墠鏈嶅姟鍣ㄦ椂闂?     * @param serverTimeMs
     */
    //TODO:鐢ㄤ笉鐢ㄨˉ鍋縍TT
    private void sampleClockOffset(long serverTimeMs) {
        long arrivalLocalTimeMs = getMonotonicTimeMs();
        double offsetSample = serverTimeMs - arrivalLocalTimeMs;
        clockOffsetMs += (offsetSample - clockOffsetMs) * 0.1;
    }
    /**
     * 鏈湴鏃堕棿
     */
    private long estimateServerTimeMs() {
        double estimate = getMonotonicTimeMs() + clockOffsetMs;
        return (long) Math.round(estimate);
    }

    private long getMonotonicTimeMs() {
        long elapsedNano = System.nanoTime() - localClockBaseNano;
        return localClockBaseMillis + elapsedNano / 1_000_000L;
    }

    /**
     * 纭繚瑙掕壊涓嶄細璧板嚭鍦板浘
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
     * 鑾峰彇wasd杈撳叆
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
     * 鏈湴妯℃嫙鐜╁绉诲姩
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
     * 鎵撳寘鍙戦€?     * @param cmd
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
     * 瀹㈡埛绔彂閫侀€熺巼闄愬埗
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
    鍙戦€佷唬鐮?     */
    private void sendInputImmediately(Message.C2S_PlayerInput msg, long timestampMs) {
        if (game.trySendPlayerInput(msg)) {
            lastInputSendMs = timestampMs;
            pendingRateLimitedInput = null;
        } else {
            pendingRateLimitedInput = msg;
        }
    }

    /**
    濡傛灉鏈湴杈撳叆杈惧埌浜嗘渶灏忓彂閫侀棿闅斿氨鎵撳寘鍙戝嚭鍘?     */
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
     * 瀹㈡埛绔鐞嗘湇鍔″櫒鍏ㄩ噺娓告垙鍚屾鍖?     * @param sync
     */
    public void onGameStateReceived(Message.S2C_GameStateSync sync) {
        long arrivalMs = TimeUtils.millis();//鍖呭埌杈剧殑鏃堕棿
        //瀹夊叏鏈哄埗
        Message.Timestamp syncTime = sync.hasSyncTime() ? sync.getSyncTime() : null;
        if (!shouldAcceptStatePacket(syncTime, sync.getServerTimeMs(), arrivalMs)) {
            return;
        }
        updateSyncArrivalStats(arrivalMs);
        sampleClockOffset(sync.getServerTimeMs());
        //澶勭悊鏁屼汉鐘舵€?
        if (!sync.getEnemiesList().isEmpty()) {
            for (Message.EnemyState enemy : sync.getEnemiesList()) {
                enemyStateCache.put((int) enemy.getEnemyId(), enemy);
            }
            syncEnemyViews(sync.getEnemiesList(), sync.getServerTimeMs());
        }
        handlePlayersFromServer(sync.getPlayersList(), sync.getServerTimeMs());
    }

    /**
     * 瀹㈡埛绔鐞嗗閲忓悓姝ョ殑鍏ュ彛鍑芥暟
     * @param delta
     */
    public void onGameStateDeltaReceived(Message.S2C_GameStateDeltaSync delta) {
        //鎺ユ敹鍖呰幏鍙栧悓姝ユ椂闂?
        long arrivalMs = TimeUtils.millis();
        Message.Timestamp syncTime = delta.hasSyncTime() ? delta.getSyncTime() : null;
        //鏈夋晥鎬ф楠?
        if (!shouldAcceptStatePacket(syncTime, delta.getServerTimeMs(), arrivalMs)) {
            return;
        }
        //鍚堝苟鐜╁澧為噺鍒板畬鏁寸姸鎬?
        List<Message.PlayerState> mergedPlayers = new ArrayList<>(delta.getPlayersCount());
        for (Message.PlayerStateDelta playerDelta : delta.getPlayersList()) {
            Message.PlayerState merged = mergePlayerDelta(playerDelta);
            if (merged != null) {
                mergedPlayers.add(merged);
            }
        }
        //鍚堝苟鏁屼汉澧為噺鍒板畬鏁寸姸鎬?
        List<Message.EnemyState> updatedEnemies = new ArrayList<>(delta.getEnemiesCount());
        for (Message.EnemyStateDelta enemyDelta : delta.getEnemiesList()) {
            Message.EnemyState mergedEnemy = mergeEnemyDelta(enemyDelta);
            if (mergedEnemy != null) {
                updatedEnemies.add(mergedEnemy);
            }
        }
        //缃戠粶璐ㄩ噺+鏃堕挓鏍″噯
        updateSyncArrivalStats(arrivalMs);//鏇存柊鍋忕Щ鎸囨爣
        sampleClockOffset(delta.getServerTimeMs());//浼扮畻瀹㈡埛绔埌鏈嶅姟鍣ㄧ殑鍋忕Щ閲?
        // 鍒嗗彂澶勭悊鐘舵€?
        handlePlayersFromServer(mergedPlayers, delta.getServerTimeMs());
        if (!updatedEnemies.isEmpty()) {
            syncEnemyViews(updatedEnemies, delta.getServerTimeMs());
        }
    }

    /**
     * 鏈嶅姟鍣ㄥ鐞嗘湰鍦扮帺瀹跺拰杩滅▼鐜╁鐘舵€佺殑鏍稿績閫昏緫
     * @param players
     * @param serverTimeMs
     */
    private void handlePlayersFromServer(Collection<Message.PlayerState> players, long serverTimeMs) {
        //鑾峰彇鏈湴鐜╁id
        int myId = game.getPlayerId();
        Message.PlayerState selfStateFromServer = null;
        //閬嶅巻鎵€鏈夌帺瀹剁姸鎬?
        if (players != null) {
            for (Message.PlayerState player : players) {
                int playerId = (int) player.getPlayerId();
                serverPlayerStates.put(playerId, player);
                //鏈湴鐜╁,娲荤潃鎵嶅鐞?
                if (playerId == myId) {
                    isSelfAlive = player.getIsAlive();
                    if (isSelfAlive) {
                        selfStateFromServer = player;
                    }
                    continue;
                }
                if (!player.getIsAlive()) {
                    removeRemotePlayerData(playerId);
                    continue;
                }
                //杩滅▼鐜╁--璁板綍蹇収
                if (player.hasPosition()) {
                    Vector2 position = new Vector2(player.getPosition().getX(), player.getPosition().getY());
                    pushRemoteSnapshot(playerId, position, player.getRotation(), serverTimeMs);
                    remotePlayerLastSeen.put(playerId, serverTimeMs);
                }
            }
        }
        //娓呯悊杩囨湡鐜╁
        purgeStaleRemotePlayers(serverTimeMs);
        //搴旂敤鏈嶅姟鍣ㄥ鑷韩鐨勭煫姝?
        if (selfStateFromServer != null) {
            applySelfStateFromServer(selfStateFromServer);
        }
    }

    /**
     * 瀹㈡埛绔繙绋嬪悓姝ユ晫浜轰俊鎭?     * @param enemies
     * @param serverTimeMs
     */
    private void syncEnemyViews(Collection<Message.EnemyState> enemies, long serverTimeMs) {
        //绌哄垽鏂?
        if (enemies == null || enemies.isEmpty()) {
            purgeStaleEnemies(serverTimeMs);
            return;
        }

        removePlaceholderEnemy();
        //閬嶅巻鏁屼汉鐘舵€?
        for (Message.EnemyState enemy : enemies) {
            if (enemy == null || !enemy.hasPosition()) {
                continue;
            }
            //鏁屼汉姝讳骸澶勭悊
            int enemyId = (int) enemy.getEnemyId();
            if (!enemy.getIsAlive()) {
                removeEnemy(enemyId);
                continue;
            }
            EnemyView view = ensureEnemyView(enemy);
            //浣嶇疆澶勭悊
            renderBuffer.set(enemy.getPosition().getX(), enemy.getPosition().getY());
            clampPositionToMap(renderBuffer);
            //娓叉煋鏈嶅姟鍣ㄧ姸鎬佸埌瑙嗗浘
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
            //璁板綍鏈€鍚庡彲瑙佹椂闂?
            enemyLastSeen.put(enemyId, serverTimeMs);
        }
        //娓呯悊杩囨湡鏁屼汉
        purgeStaleEnemies(serverTimeMs);
    }

    /**
     * 瀹㈡埛绔鏈湴鐜╁杩涜鏈嶅姟鍣ㄧ姸鎬佺煫姝?宸笉澶氬氨鏄窡鏈嶅姟鍣ㄥ悓姝ョ殑杩囩▼
     * @param selfStateFromServer
     */
    private void applySelfStateFromServer(Message.PlayerState selfStateFromServer) {
        //鑾峰彇鏈嶅姟鍣ㄤ綅缃?
        Vector2 serverPos = new Vector2(
                selfStateFromServer.getPosition().getX(),
                selfStateFromServer.getPosition().getY()
        );
        clampPositionToMap(serverPos);
        //鎻愬彇鍏冩暟鎹?澶勭悊鏈€鍚庤緭鍏ョ殑搴忓彿
        int lastProcessedSeq = selfStateFromServer.getLastProcessedInputSeq();
        //鍒涘缓瀛樺偍鐘舵€佸揩鐓?
        PlayerStateSnapshot snapshot = new PlayerStateSnapshot(
                serverPos,
                selfStateFromServer.getRotation(),
                lastProcessedSeq
        );
        snapshotHistory.offer(snapshot);
        while (snapshotHistory.size() > 10) {
            snapshotHistory.poll();
        }
        //鐘舵€佸崗璋?
        reconcileWithServer(snapshot);
    }

    /**
     * 瀹㈡埛绔帴鏀惰繙绋嬬帺瀹跺揩鐓т箣鍚庢瀯寤烘湇鍔″櫒蹇収骞剁淮鎶ゆ粦鍔ㄧ獥鍙ｇ紦瀛?     * @param playerId
     * @param position
     * @param rotation
     * @param serverTimeMs
     */
    private void pushRemoteSnapshot(int playerId, Vector2 position, float rotation, long serverTimeMs) {
        clampPositionToMap(position);
        //鑾峰彇蹇収闃熷垪
        Deque<ServerPlayerSnapshot> queue = remotePlayerServerSnapshots
                .computeIfAbsent(playerId, k -> new ArrayDeque<>());
        //閫熷害浼扮畻
        Vector2 velocity = new Vector2();
        ServerPlayerSnapshot previous = queue.peekLast();//鏈€鏂板揩鐓
        if (previous != null) {
            long deltaMs = serverTimeMs - previous.serverTimestampMs;
            if (deltaMs > 0) {
                velocity.set(position).sub(previous.position).scl(1000f / deltaMs);
            } else {
                velocity.set(previous.velocity);//鐢ㄦ棫閫熷害
            }
            //閫熷害涓婇檺闄愬埗
            float maxSpeed = PLAYER_SPEED * 1.5f;
            if (velocity.len2() > maxSpeed * maxSpeed) {
                velocity.clamp(0f, maxSpeed);
            }
        }
        //鏇存柊鏈濆悜
        updateRemoteFacing(playerId, velocity, rotation);
        //鍒涘缓骞跺瓨鍏ュ揩鐓?
        ServerPlayerSnapshot snap = new ServerPlayerSnapshot(position, rotation, velocity, serverTimeMs);
        queue.addLast(snap);
        //婊戝姩绐楀彛娓呯悊
        while (queue.size() > 1 &&
                (serverTimeMs - queue.peekFirst().serverTimestampMs) > SNAPSHOT_RETENTION_MS) {
            queue.removeFirst();
        }
    }

    /**
     * 鏇存柊鐜╁鏈濆悜,璁惧畾鏄彧鑳藉乏鍙崇炕杞?     * @param playerId
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

    private void removeRemotePlayerData(int playerId) {
        remotePlayerServerSnapshots.remove(playerId);
        remotePlayerLastSeen.remove(playerId);
        remoteFacingRight.remove(playerId);
        remoteDisplayPositions.remove(playerId);
    }

    /**
     * 娓呯悊闀挎椂闂存湭鏀跺埌娑堟伅鐨勮繃鏈熺帺瀹?     * @param currentServerTimeMs
     */
    private void purgeStaleRemotePlayers(long currentServerTimeMs) {
        Iterator<Map.Entry<Integer, Long>> iterator = remotePlayerLastSeen.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<Integer, Long> entry = iterator.next();
            long lastSeen = entry.getValue();
            if ((currentServerTimeMs - lastSeen) > REMOTE_PLAYER_TIMEOUT_MS) {
                int playerId = entry.getKey();
                iterator.remove();
                removeRemotePlayerData(playerId);
                serverPlayerStates.remove(playerId);
            }
        }
    }

    /**
     * 宸﹀彸缈昏浆鏍稿績閫昏緫-纭瑙掕壊鏄惁璇ユ湞鍚戝彸杈?     * @param rotation
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
     * 鏈嶅姟鍣ㄤ竴鑷存€х殑鎿嶄綔,鍖呮嫭鏍℃,骞虫粦,閲嶆斁
     * @param serverSnapshot
     */
    private void reconcileWithServer(PlayerStateSnapshot serverSnapshot) {
        //RTT鍋忕Щ閲忕殑浼扮畻
        PlayerInputCommand acknowledged = unconfirmedInputs.get(serverSnapshot.lastProcessedInputSeq);
        if (acknowledged != null) {
            Long sentLogical = inputSendTimes.remove(acknowledged.seq);
            float sample;
            if (sentLogical != null) {
                sample = logicalTimeMs - sentLogical;//娓告垙閫昏緫鏃堕棿
            } else {
                sample = System.currentTimeMillis() - acknowledged.timestampMs;//鍥為€€鍒扮郴缁熸椂闂?
                }
            if (sample > 0f) {
                smoothedRttMs = MathUtils.lerp(smoothedRttMs, sample, 0.2f);
            }
        }
        //搴旂敤鏈嶅姟鍣ㄧ姸鎬佸拰鍒濆鍖栧鐞?
            float correctionDist = predictedPosition.dst(serverSnapshot.position);
        boolean wasInitialized = hasReceivedInitialState;
        predictedPosition.set(serverSnapshot.position);
        predictedRotation = serverSnapshot.rotation;
        facingRight = inferFacingFromRotation(predictedRotation);
        clampPositionToMap(predictedPosition);
        logServerCorrection(correctionDist, serverSnapshot.lastProcessedInputSeq);
        if (!wasInitialized) {
            clearInitialStateWait();
            displayPosition.set(predictedPosition);//棣栨鍚屾鐩存帴璺宠浆
        }
        //閲嶆斁鏈‘璁よ緭鍏?
            for (PlayerInputCommand input : unconfirmedInputs.values()) {
            if (input.seq > serverSnapshot.lastProcessedInputSeq) {
                applyInputLocally(predictedPosition, predictedRotation, input, input.deltaSeconds);
            }
        }
        //娓呯悊宸茬‘璁よ緭鍏?
            unconfirmedInputs.entrySet().removeIf(entry -> {
            boolean applied = entry.getKey() <= serverSnapshot.lastProcessedInputSeq;
            if (applied) {
                inputSendTimes.remove(entry.getKey());
            }
            return applied;
        });
        pruneUnconfirmedInputs();
        //鐘舵€佹爣蹇楁洿鏂?
            hasReceivedInitialState = true;
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
        //娓叉煋浣嶇疆骞虫粦杩囧害
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= (DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE * 4f)) {
            displayPosition.set(predictedPosition);
        } else {
            displayPosition.lerp(predictedPosition, 0.35f);
        }
    }

    /**
     * 澶勭悊鐜╁澧為噺鍙樺寲
     * @param delta
     * @return
     */
    private Message.PlayerState mergePlayerDelta(Message.PlayerStateDelta delta) {
        //绌哄€兼鏌?
        if (delta == null) {
            return null;
        }
        int playerId = (int) delta.getPlayerId();
        Message.PlayerState base = serverPlayerStates.get(playerId);
        //鐜╁棣栨鍑虹幇鎴栬€呭凡杩囨湡
        if (base == null) {
            requestDeltaResync("player_" + playerId);
            return null;
        }
        //鏇存柊
        Message.PlayerState.Builder builder = base.toBuilder();
        //浣嶆帺鐮侀┍鍔ㄦ洿鏂?
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
        //鏇存柊缂撳瓨,杩斿洖
        Message.PlayerState updated = builder.build();
        serverPlayerStates.put(playerId, updated);
        return updated;
    }

    /**
     * 澶勭悊鏁屼汉澧為噺鍙樺寲
     * @param delta
     * @return
     */
    private Message.EnemyState mergeEnemyDelta(Message.EnemyStateDelta delta) {
        //绌哄€兼鏌?
        if (delta == null) {
            return null;
        }
        int enemyId = (int) delta.getEnemyId();
        Message.EnemyState base = enemyStateCache.get(enemyId);
        //鏁屼汉棣栨鍑虹幇鎴栬€呯姸鎬佷涪澶?
        if (base == null) {
            requestDeltaResync("enemy_" + enemyId);
            return null;
        }
        //鏇存柊
        Message.EnemyState.Builder builder = base.toBuilder();
        //浣嶆帺鐮侀┍鍔ㄧ姸鎬佹洿鏂?
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
        //鏇存柊缂撳瓨,杩斿洖
        Message.EnemyState updated = builder.build();
        enemyStateCache.put(enemyId, updated);
        return updated;
    }

    /**
     * 杩涜鍏ㄩ噺鍚屾
     * @param reason
     */
    private void requestDeltaResync(String reason) {
        //game鏈夋晥骞朵笖涓嶅湪鍐峰嵈鏃堕棿鍐?
        if (game == null) {
            return;
        }
        long now = TimeUtils.millis();
        if ((now - lastDeltaResyncRequestMs) < DELTA_RESYNC_COOLDOWN_MS) {
            return;
        }
        //鏇存柊鏃堕棿骞朵笖鍏ㄩ噺鍚屾
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
        int spawned = 0;
        for (Message.ProjectileState state : spawn.getProjectilesList()) {
            if (state == null) {
                continue;
            }
            long projectileId = Integer.toUnsignedLong(state.getProjectileId());
            ProjectileView view = new ProjectileView(projectileId);
            Vector2 targetPosition = projectileTempVector;
            if (state.hasPosition()) {
                setVectorFromProto(state.getPosition(), targetPosition);
            } else {
                targetPosition.set(displayPosition);
            }
            Vector2 originPosition = projectileOriginBuffer;
            resolveProjectileOrigin(state, targetPosition, originPosition);
            view.position.set(originPosition);
            float rotationDeg = state.getRotation();
            view.rotationDeg = rotationDeg;
            float serverSpeed = state.hasProjectile() ? state.getProjectile().getSpeed() : 0f;
            float appliedSpeed = PEA_PROJECTILE_SPEED > 0f ? PEA_PROJECTILE_SPEED : serverSpeed;
            Vector2 direction = projectileDirectionBuffer;
            direction.set(targetPosition).sub(originPosition);
            float travelDistance = direction.len();
            if (travelDistance > 0.001f) {
                direction.scl(1f / travelDistance);
            } else {
                float dirX = MathUtils.cosDeg(rotationDeg);
                float dirY = MathUtils.sinDeg(rotationDeg);
                direction.set(dirX, dirY);
                if (direction.isZero(0.001f)) {
                    direction.set(1f, 0f);
                } else {
                    direction.nor();
                }
                travelDistance = Math.max(0f, appliedSpeed * state.getTtlMs() / 1000f);
            }
            view.velocity.set(direction).scl(appliedSpeed);
            long ttlMs = Math.max(50L, state.getTtlMs());
            if (travelDistance > 0.001f && appliedSpeed > 0f) {
                long travelTtl = (long) Math.ceil((travelDistance / appliedSpeed) * 1000f);
                ttlMs = Math.max(ttlMs, travelTtl);
            }
            ttlMs = Math.max(50L, ttlMs);
            view.spawnServerTimeMs = serverTimeMs;
            view.expireServerTimeMs = serverTimeMs + ttlMs;
            view.spawnClientTimeMs = logicalTimeMs;
            view.expireClientTimeMs = logicalTimeMs + ttlMs;
            projectileViews.put(projectileId, view);
            spawned++;
            Gdx.app.log(TAG, "ProjectileSpawn id=" + projectileId
                    + " pos=" + view.position
                    + " vel=" + view.velocity
                    + " speed=" + appliedSpeed
                    + " ttlMs=" + ttlMs
                    + " serverTime=" + serverTimeMs
                    + " clientTime=" + logicalTimeMs);
            if (view.velocity.isZero(0.0001f)) {
                Gdx.app.log(TAG, "Projectile " + projectileId + " has zero velocity; check lock direction.");
            }
        }
        if (spawned > 0) {
            Gdx.app.log(TAG, "Spawn batch=" + spawned + ", activeProjectiles=" + projectileViews.size());
        }
    }

    private void resolveProjectileOrigin(Message.ProjectileState state, Vector2 targetPosition, Vector2 outOrigin) {
        if (state == null || outOrigin == null) {
            return;
        }
        int ownerId = (int) state.getOwnerPlayerId();
        boolean shouldApplyOffset = ownerId > 0;
        boolean facingResolved = false;
        boolean ownerFacingRight = true;

        if (ownerId == game.getPlayerId()) {
            outOrigin.set(displayPosition);
            ownerFacingRight = facingRight;
            facingResolved = true;
        } else if (ownerId > 0) {
            Vector2 remoteDisplay = remoteDisplayPositions.get(ownerId);
            if (remoteDisplay != null) {
                outOrigin.set(remoteDisplay);
            } else {
                Message.PlayerState ownerState = serverPlayerStates.get(ownerId);
                if (ownerState != null && ownerState.hasPosition()) {
                    outOrigin.set(ownerState.getPosition().getX(), ownerState.getPosition().getY());
                } else if (targetPosition != null) {
                    outOrigin.set(targetPosition);
                } else {
                    outOrigin.set(displayPosition);
                }
            }
            Boolean remoteFacing = remoteFacingRight.get(ownerId);
            if (remoteFacing != null) {
                ownerFacingRight = remoteFacing;
                facingResolved = true;
            }
        } else if (targetPosition != null) {
            outOrigin.set(targetPosition);
        } else {
            outOrigin.set(displayPosition);
        }

        if (!facingResolved) {
            ownerFacingRight = inferFacingFromRotation(state.getRotation());
        }
        if (shouldApplyOffset) {
            applyPeaProjectileOffset(outOrigin, ownerFacingRight);
        }
    }

    private void applyPeaProjectileOffset(Vector2 origin, boolean faceRight) {
        if (origin == null) {
            return;
        }
        origin.y += PEA_PROJECTILE_MUZZLE_Y_OFFSET;
        origin.x += faceRight ? PEA_PROJECTILE_MUZZLE_X_OFFSET : -PEA_PROJECTILE_MUZZLE_X_OFFSET;
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
                ? "你受到了" + hurt.getDamage() + "点伤害, 剩余血量 " + hurt.getRemainingHealth()
                : "玩家 " + playerId + " 受到了 " + hurt.getDamage() + " 点伤害";
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
        if (enemyId == lockedEnemyId) {
            lockedEnemyId = 0;
        }
        String toast = died.getKillerPlayerId() > 0
                ? "已有敌人被" + died.getKillerPlayerId() + "击败"
                : "敌人" + enemyId + " 被消灭";
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
                ? "你已升级到 Lv." + levelUp.getNewLevel()
                : "玩家" + playerId + " 到达了Lv." + levelUp.getNewLevel();
        showStatusToast(toast);
    }

    private void handleDroppedItem(Message.S2C_DroppedItem droppedItem) {
        if (droppedItem == null || droppedItem.getItemsCount() == 0) {
            return;
        }
        showStatusToast("掉落了" + droppedItem.getItemsCount() + " 个战利品");
    }

    private void handleGameOver(Message.S2C_GameOver gameOver) {
        if (gameOver == null) {
            showStatusToast( "游戏结束");
        } else {
            showStatusToast(gameOver.getVictory() ? "战斗胜利！" : "战斗失败");
        }
        projectileViews.clear();
        projectileImpacts.clear();
        resetTargetingState();
        resetAutoAttackState();
        if (!hasShownGameOver) {
            hasShownGameOver = true;
            game.setScreen(new GameOverScreen(game, gameOver));
        }
     else {
            showStatusToast(gameOver.getVictory() ? "恭喜通关" : "下次努力");
        }
        projectileViews.clear();
        projectileImpacts.clear();
        resetTargetingState();
        resetAutoAttackState();
    }
    /**
     * 娓告垙鐜鐘舵€佺殑鏋氫妇鍒ゆ柇
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
     * 浣嶇疆淇鍜屾棩蹇楄緭鍑?     * @param delta
     */
    private void updateDisplayPosition(float delta) {
        //鏈垵濮嬪寲鍒欒烦杩?
        if (!hasReceivedInitialState) {
            return;
        }
        //闈炴湰鍦版帶鍒惰鑹插氨鐩存帴鍚屾
        if (!isLocallyMoving) {
            displayPosition.set(predictedPosition);
            return;
        }
        //灏忓亸宸慨姝?
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE) {
            displayPosition.set(predictedPosition);
            return;
        }
        //澶у亸宸?鎶ヨ
        float distance = (float) Math.sqrt(distSq);
        if (distance > DISPLAY_DRIFT_LOG_THRESHOLD) {
            logDisplayDrift(distance);
        }
        //骞虫粦鎻掑€?鍚戦娴嬩綅缃潬鎷?
        float alpha = MathUtils.clamp(delta * DISPLAY_LERP_RATE, 0f, 1f);
        displayPosition.lerp(predictedPosition, alpha);
    }

    /**
     * 缁欎簣涓€涓ǔ瀹氱殑鏃ュ織杈撳嚭
     * @param rawDelta
     * @param stableDelta
     */
    private void logFrameDeltaSpike(float rawDelta, float stableDelta) {
        //鍙鐞嗚秴杩囪秴鏃堕檺搴︾殑鎶ラ敊
        if (Math.abs(rawDelta - stableDelta) < DELTA_SPIKE_THRESHOLD) {
            return;
        }
        //缁欎簣涓€涓ǔ瀹氱殑鏃ュ織杈撳嚭棰戠巼
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDeltaSpikeLogMs < DELTA_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Frame delta spike raw=" + rawDelta + " stable=" + stableDelta
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDeltaSpikeLogMs = nowMs;
    }

    /**
     * 杈撳嚭鏃ュ織,璁板綍鏈嶅姟鍣ㄤ綅缃牎姝ｇ殑璇婃柇鍑芥暟
     * @param correctionDist
     * @param lastProcessedSeq
     */
    private void logServerCorrection(float correctionDist, int lastProcessedSeq) {
        //杩囨护寰皬鐭
        if (correctionDist < POSITION_CORRECTION_LOG_THRESHOLD) {
            return;
        }
        //鑾峰彇鏃堕棿,纭繚鏃ュ織鍙戦€侀鐜?
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
     * 杈撳嚭鏃ュ織
     * @param drift
     */
    private void logDisplayDrift(float drift) {
        //闃插尽鎬х紪绋?浜屾鏍￠獙婕傜Щ闃堝€?
        if (drift < DISPLAY_DRIFT_LOG_THRESHOLD) {
            return;
        }
        //鑾峰彇鏃堕棿,纭繚鏃ュ織鍙戦€侀鐜?
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDisplayDriftLogMs < DISPLAY_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Display drift=" + drift + " facingRight=" + facingRight
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDisplayDriftLogMs = nowMs;
    }

    /**
     * 璁＄畻鏈杈撳叆鐨勬寔缁椂闂?     * @return
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
     * 娓呯悊鐜╁杈撳叆骞跺彂閫佺粰鏈嶅姟鍣ㄥ悗浣欎笅鐨勪复鏃跺彉閲忓噺灏戣祫婧愭秷鑰?     */
    private void resetPendingInputAccumulator() {
        hasPendingInputChunk = false;
        pendingAttack = false;
        pendingInputDuration = 0f;
        pendingInputStartMs = 0L;
    }

    private void resetAutoAttackState() {
        autoAttackAccumulator = AUTO_ATTACK_INTERVAL;
        autoAttackHoldTimer = 0f;
    }

    private boolean resolveAttackingState(float delta) {
        if (!autoAttackToggle) {
            return false;
        }
        autoAttackAccumulator += delta;
        if (autoAttackAccumulator >= AUTO_ATTACK_INTERVAL) {
            autoAttackAccumulator -= AUTO_ATTACK_INTERVAL;
            autoAttackHoldTimer = AUTO_ATTACK_HOLD_TIME;
        }
        if (autoAttackHoldTimer > 0f) {
            autoAttackHoldTimer = Math.max(0f, autoAttackHoldTimer - delta);
            return true;
        }
        return false;
    }

    private void resetTargetingState() {
        lockedEnemyId = 0;
        targetRefreshTimer = TARGET_REFRESH_INTERVAL;
    }

    private void updatePlayerFacing(float delta) {
        if (enemyViews.isEmpty()) {
            lockedEnemyId = 0;
            return;
        }
        targetRefreshTimer += delta;
        EnemyView target = enemyViews.get(lockedEnemyId);
        boolean needsRefresh = target == null
                || !target.isAlive()
                || lockedEnemyId == PLACEHOLDER_ENEMY_ID
                || targetRefreshTimer >= TARGET_REFRESH_INTERVAL;
        if (needsRefresh) {
            lockedEnemyId = findNearestEnemyId();
            targetRefreshTimer = 0f;
            target = enemyViews.get(lockedEnemyId);
        }
        if (target == null || !target.isAlive()) {
            return;
        }
        target.getDisplayPosition(targetingBuffer);
        float dx = targetingBuffer.x - predictedPosition.x;
        float dy = targetingBuffer.y - predictedPosition.y;
        if (Math.abs(dx) <= 0.001f && Math.abs(dy) <= 0.001f) {
            return;
        }
        predictedRotation = MathUtils.atan2(dy, dx) * MathUtils.radiansToDegrees;
        facingRight = dx >= 0f;
    }

    private int findNearestEnemyId() {
        float bestDistSq = Float.MAX_VALUE;
        int bestId = 0;
        for (Map.Entry<Integer, EnemyView> entry : enemyViews.entrySet()) {
            int enemyId = entry.getKey();
            if (enemyId == PLACEHOLDER_ENEMY_ID) {
                continue;
            }
            EnemyView view = entry.getValue();
            if (view == null || !view.isAlive()) {
                continue;
            }
            view.getDisplayPosition(targetingBuffer);
            float distSq = targetingBuffer.dst2(predictedPosition);
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                bestId = enemyId;
            }
        }
        return bestId;
    }

    private static void configureConsoleEncoding() {
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        if (!os.contains("win")) {
            return;
        }
        try {
            PrintStream out = new PrintStream(
                    new BufferedOutputStream(new FileOutputStream(FileDescriptor.out)), true, "GBK");
            PrintStream err = new PrintStream(
                    new BufferedOutputStream(new FileOutputStream(FileDescriptor.err)), true, "GBK");
            System.setOut(out);
            System.setErr(err);
        } catch (Exception ignored) {
        }
    }

    /**
     * 閲嶇疆瀹㈡埛绔姸鎬佸苟鍙戦€佸叏閲忚姹?     */
    private void resetInitialStateTracking() {
        initialStateStartMs = TimeUtils.millis();
        lastInitialStateRequestMs = 0L;
        initialStateWarningLogged = false;
        initialStateCriticalLogged = false;
        initialStateFailureLogged = false;
        initialStateRequestCount = 0;
        lastDeltaResyncRequestMs = 0L;
        resetTargetingState();
        maybeSendInitialStateRequest(initialStateStartMs, "initial_enter");
    }

    /**
     * 娓呯悊鐘舵€?浠ヤ究閫€鍑哄垵濮嬪悓姝ョ姸鎬?     */
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
     * 鑾峰彇鍏ㄩ噺淇℃伅骞堕檮甯﹁幏鍙栦笉鍒扮殑鍔炴硶
     */
    private void maybeRequestInitialStateResync() {
        //鍐嶅垽鏂竴涓嬮伩鍏嶈鍒?濡傛灉杩欎釜涓嶆槸閭ｅ氨鏄鑹茬汗鐞嗘病鍔犺浇濂?
        if (hasReceivedInitialState) {
            return;
        }
        //鏃堕棿鎴?
        long now = TimeUtils.millis();
        //濡傛灉瀹㈡埛绔垵濮嬪寲寮€濮嬫椂闂磋繕鏄?灏辫幏鍙栦竴娆″垵濮嬬姸鎬?
        if (initialStateStartMs == 0L) {
            resetInitialStateTracking();
            return;
        }
        //鍥哄畾闂撮殧閲嶈瘯
        if ((now - lastInitialStateRequestMs) >= INITIAL_STATE_REQUEST_INTERVAL_MS) {
            maybeSendInitialStateRequest(now, "retry_interval");
        }
        //鍒嗙骇閲嶈瘯,浠庢甯搁噸璇曞埌闇€瑕佹姏璀﹀憡鍒颁弗閲嶈鍛?
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

    /**璇锋眰瀹㈡埛绔啀涓€娆″彂璧峰叏閲忓悓姝?璁板綍璇锋眰娆℃暟濡傛灉娆℃暟杩囧涔熶細鍙栨秷閲嶈瘯*/
    private void maybeSendInitialStateRequest(long timestampMs, String reason) {
        initialStateRequestCount++;
        if (game != null) {
            String tag = reason != null ? reason : "unknown";
            game.requestFullGameStateSync("GameScreen:" + tag + "#"+ initialStateRequestCount);
        }
        lastInitialStateRequestMs = timestampMs;
    }

    /**
     * 鍦ㄦ父鎴忓垵濮嬭祫婧愭病鍔犺浇鎴愬姛鐨勬椂鍊欏姞杞戒竴涓垵濮嬬晫闈?     */
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
     * 娓告垙鍔犺浇闂澶勭悊
     * @return
     */
    private String getLoadingMessage() {
        if (initialStateStartMs == 0L) {
            return "鍔犺浇涓?..";
        }
        long waitMs = TimeUtils.millis() - initialStateStartMs;
        if (waitMs >= INITIAL_STATE_FAILURE_HINT_MS) {
            return "绛夊緟鏈嶅姟鍣ㄥ悓姝ヨ秴鏃讹紝璇锋鏌ョ綉缁滆繛鎺ワ紙宸茶姹?" + initialStateRequestCount + " 娆★級";
        }
        if (waitMs >= INITIAL_STATE_CRITICAL_MS) {
            return "绛夊緟鏈嶅姟鍣ㄥ悓姝ヨ秴鏃讹紝姝ｅ湪閲嶈瘯...锛堢 " + initialStateRequestCount + " 娆★級";
        }
        if (waitMs >= INITIAL_STATE_WARNING_MS) {
            return "姝ｅ湪璇锋眰鏈嶅姟鍣ㄥ悓姝?..锛堢 " + initialStateRequestCount + " 娆★級";
        }
        return "鍔犺浇涓?..锛堢 " + Math.max(1, initialStateRequestCount) + " 娆¤姹傦級";
    }
}
