package com.lawnmower.screens;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.badlogic.gdx.InputMultiplexer;
import com.badlogic.gdx.InputProcessor;
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
import com.badlogic.gdx.scenes.scene2d.Actor;
import com.badlogic.gdx.scenes.scene2d.InputEvent;
import com.badlogic.gdx.scenes.scene2d.InputListener;
import com.badlogic.gdx.scenes.scene2d.Stage;
import com.badlogic.gdx.scenes.scene2d.Touchable;
import com.badlogic.gdx.scenes.scene2d.actions.Actions;
import com.badlogic.gdx.scenes.scene2d.ui.Label;
import com.badlogic.gdx.scenes.scene2d.ui.Skin;
import com.badlogic.gdx.scenes.scene2d.ui.Table;
import com.badlogic.gdx.scenes.scene2d.ui.TextButton;
import com.badlogic.gdx.scenes.scene2d.utils.ClickListener;
import com.badlogic.gdx.scenes.scene2d.utils.Drawable;
import com.badlogic.gdx.scenes.scene2d.utils.TextureRegionDrawable;
import com.badlogic.gdx.utils.Array;
import com.badlogic.gdx.utils.Align;
import com.badlogic.gdx.utils.TimeUtils;
import com.badlogic.gdx.utils.viewport.FitViewport;
import com.lawnmower.Config;
import com.lawnmower.Main;
import com.lawnmower.enemies.EnemyDefinitions;
import com.lawnmower.enemies.EnemyView;
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
    private static final float UPGRADE_CARD_WIDTH = 350f;
    private static final float UPGRADE_CARD_HEIGHT = 500f;
    private static final float UPGRADE_TEXT_SCALE = 0.5f;
    // 娑撳孩婀囬崝鈥虫珤 GameManager::SceneConfig.move_speed (200f) 鐎靛綊缍堥敍宀勪缉閸忓秹顣╁ù?閺夊啫鈻夐柅鐔峰娑撳秳绔撮懛?
    private static final float PLAYER_SPEED = 200f;

    // 鏉堟挸鍙嗛崚鍡橆唽閺囧绮忛敍?0~33ms閿涘绱濋梽宥勭秵閺堝秴濮熺粩顖氱垻缁?

    private static final float MAX_COMMAND_DURATION = 0.025f;
    private static final float MIN_COMMAND_DURATION = 1f / 120f;
    private static final long SNAPSHOT_RETENTION_MS = 400L;
    private static final long MAX_EXTRAPOLATION_MS = 150L;
    private static final int PLACEHOLDER_ENEMY_ID = -1;
    private static final int DEFAULT_ENEMY_TYPE_ID = EnemyDefinitions.getDefaultTypeId();
    private static final long INTERP_DELAY_MIN_MS = 60L;
    private static final long INTERP_DELAY_MAX_MS = 180L;
    private static final int AUTO_ATTACK_TOGGLE_KEY = Input.Keys.C;
    private static final float AUTO_ATTACK_INTERVAL = 1f;
    private static final float AUTO_ATTACK_HOLD_TIME = 0.18f;
    private static final float TARGET_REFRESH_INTERVAL = 0.2f;
    private static final float PEA_PROJECTILE_SPEED = 200f;
    // 鏈嶅姟鍣ㄤ笅鍙戠殑 type_id 姣旈厤缃枃浠剁殑涓嬫爣澶?1锛岄渶瑕佸湪鍙栬创鍥炬椂鍑忓幓璇ュ亸绉?
    private static final int ITEM_TYPE_ID_OFFSET = 1;

    private static final int MAX_UNCONFIRMED_INPUTS = 240;
    private static final long MAX_UNCONFIRMED_INPUT_AGE_MS = 1500L;
    private static final long REMOTE_PLAYER_TIMEOUT_MS = 5000L;
    private static final long MIN_INPUT_SEND_INTERVAL_MS = 10L; // ~100Hz

    /*
     * 婢х偤鍣洪崥灞绢劄,閺嶅洩鐦戦張宥呭缁旑垯绱堕崗銉ф畱閸欐ê瀵查崐?闁插洨鏁ゆ担宥嗗负閻?閺囧瓨鏌熸笟鑳厴閻鍤弶銉╂付鐟曚椒绱堕崗銉ф畱閸婂吋妲搁崥锕€褰傞悽鐔剁啊閸欐ê瀵?濮ｆ柨顩ф担宥囩枂娣団剝浼?
     * 閼板奔绗栨潻娆庨嚋閺傜懓绱￠弰顖氱殺Message娑擃厾娈戠€涙顔岀紓鎾崇摠閹存劕褰夐柌?鐟欙綀鈧缚鍞惍?     */
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
    private final Map<Integer, Message.ItemState> itemStateCache = new HashMap<>();
    private final Map<Integer, ItemView> itemViews = new HashMap<>();
    private final Map<Integer, TextureRegion> itemTextureRegions = new HashMap<>();
    private final Map<Integer, Texture> itemTextureHandles = new HashMap<>();
    private final Vector2 itemPositionBuffer = new Vector2();
    private long lastItemSnapshotTick = -1L;
    private long lastItemDeltaTick = -1L;
    private long lastItemSyncServerTimeMs = -1L;

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
    private Texture itemFallbackTexture;
    private TextureRegion itemFallbackRegion;
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
    private static final long RECONNECT_OVERLAY_PULSE_MS = 1200L;
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
    private boolean reconnectHoldActive = false;
    private long reconnectHoldStartMs = 0L;

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
    // 30Hz 閻╊喗鐖ｉ崥灞绢劄闂傛挳娈х痪?33ms閿涘矂顣╃純顔荤娑擃亪娼潻鎴犳窗閺嶅洨娈戦崚婵嗏偓闂寸┒娴滃骸閽╁?
    private float smoothedSyncIntervalMs = 35f;
    private float smoothedSyncDeviationMs = 30f;
    private Message.C2S_PlayerInput pendingRateLimitedInput;
    private long lastInputSendMs = 0L;
    private String statusToastMessage = "";
    private float statusToastTimer = 0f;
    private static final float STATUS_TOAST_DURATION = 2.75f;
    private String lastProjectileDebugReason = "";
    private long lastProjectileDebugLogMs = 0L;
    private int currentRoomId = 0;
    private boolean upgradeBlocking = false;
    private Stage upgradeStage;
    private Table upgradeRoot;
    private Label upgradeTitleLabel;
    private Label upgradeHintLabel;
    private TextButton upgradeRefreshButton;
    private final Array<UpgradeCard> upgradeCards = new Array<>(3);
    private UpgradeSession upgradeSession;
    private UpgradeFlowState upgradeFlowState = UpgradeFlowState.IDLE;
    private Texture upgradeOverlayTexture;
    private final EnumMap<Message.UpgradeLevel, Texture> upgradeLevelTextures = new EnumMap<>(Message.UpgradeLevel.class);
    private InputMultiplexer upgradeInputMultiplexer;
    private InputProcessor previousInputProcessor;
    private int pendingUpgradeOptionIndex = -1;

    public GameScreen(Main game) {
        this.game = Objects.requireNonNull(game);
    }

    @Override
    public void show() {
        camera = new OrthographicCamera();
        viewport = new FitViewport(WORLD_WIDTH, WORLD_HEIGHT, camera);
        batch = new SpriteBatch();//缂佹ê鍩楁禍铏瑰⒖閻劎娈?
        loadingFont = new BitmapFont();//缂佹ê鍩楃€涙ぞ缍?
        loadingFont.getData().setScale(1.3f);//鐎涙ぞ缍嬮弨鎯с亣1.3閸?
        loadingLayout = new GlyphLayout();//鐢啫鐪弬鍥ㄦ拱,閺勵垯绔寸粔宥嗙槷鏉堝啴鐝痪褏娈戠敮鍐ㄧ湰

        //閼冲本娅?
        try {
            backgroundTexture = new Texture(Gdx.files.internal("background/roomListBackground.png"));
        } catch (Exception e) {
            Pixmap bgPixmap = new Pixmap((int) WORLD_WIDTH, (int) WORLD_HEIGHT, Pixmap.Format.RGBA8888);//鐠佸墽鐤嗙痪顖濆閸?
            bgPixmap.setColor(0.05f, 0.15f, 0.05f, 1f);
            bgPixmap.fill();
            backgroundTexture = new Texture(bgPixmap);
            bgPixmap.dispose();
        }

        //娴滆櫣澧?
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
        //閸旂姾娴囩挧鍕爱
        loadEnemyAssets();
        loadProjectileAssets();
        enemyViews.clear();
        enemyLastSeen.clear();
        projectileViews.clear();
        projectileImpacts.clear();
        clearItemState();
        resetTargetingState();
        spawnPlaceholderEnemy();

        //鐠佸墽鐤嗘禍铏瑰⒖閸掓繂顫愭担宥囩枂娑撳搫婀撮崶鍙ヨ厬婢?
        predictedPosition.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f);
        displayPosition.set(predictedPosition);
        resetInitialStateTracking();
        resetAutoAttackState();
        isSelfAlive = true;
        initUpgradeOverlay();
        resetUpgradeFlowState();
    }
    /**
     * 闁挸鍙块悩鑸碘偓浣告倱濮?
     * @param items
     */
    private void syncItemViews(Collection<Message.ItemState> items) {
        if (items == null) {
            clearItemState(false);
            return;
        }
        if (items.isEmpty()) {
            clearItemState(false);
            return;
        }
        Set<Integer> seenIds = new HashSet<>(items.size());
        for (Message.ItemState itemState : items) {
            if (itemState == null) {
                continue;
            }
            int itemId = (int) itemState.getItemId();
            seenIds.add(itemId);
            applyItemState(itemState);
        }
        Iterator<Map.Entry<Integer, ItemView>> iterator = itemViews.entrySet().iterator();
        while (iterator.hasNext()) {
            Map.Entry<Integer, ItemView> entry = iterator.next();
            if (!seenIds.contains(entry.getKey())) {
                iterator.remove();
                itemStateCache.remove(entry.getKey());
            }
        }
    }

    private void applyItemState(Message.ItemState itemState) {
        if (itemState == null) {
            return;
        }
        int itemId = (int) itemState.getItemId();
        if (itemState.getIsPicked()) {
            itemViews.remove(itemId);
            itemStateCache.remove(itemId);
            return;
        }
        Message.ItemState resolvedState = ensureItemPosition(itemState);
        itemStateCache.put(itemId, resolvedState);
        if (!resolvedState.hasPosition()) {
            return;
        }
        setVectorFromProto(resolvedState.getPosition(), itemPositionBuffer);
        clampPositionToMap(itemPositionBuffer);
        TextureRegion region = resolveItemTexture((int) resolvedState.getTypeId());
        if (region == null) {
            region = getItemFallbackRegion();
        }
        ItemView view = itemViews.computeIfAbsent(itemId, ItemView::new);
        view.update(region, itemPositionBuffer, (int) resolvedState.getTypeId(), resolvedState.getEffectType());
    }

    private Message.ItemState ensureItemPosition(Message.ItemState incoming) {
        if (incoming == null || incoming.hasPosition()) {
            return incoming;
        }
        Message.ItemState cached = itemStateCache.get((int) incoming.getItemId());
        if (cached == null || !cached.hasPosition()) {
            return incoming;
        }
        return incoming.toBuilder().setPosition(cached.getPosition()).build();
    }

    private boolean shouldApplyItemSnapshot(long incomingTick, long serverTimeMs) {
        if (incomingTick >= 0) {
            if (lastItemSnapshotTick >= 0 && Long.compareUnsigned(incomingTick, lastItemSnapshotTick) <= 0) {
                return false;
            }
            lastItemSnapshotTick = incomingTick;
            lastItemDeltaTick = incomingTick;
            lastItemSyncServerTimeMs = serverTimeMs;
            return true;
        }
        if (serverTimeMs <= lastItemSyncServerTimeMs) {
            return false;
        }
        lastItemSnapshotTick = -1L;
        lastItemDeltaTick = -1L;
        lastItemSyncServerTimeMs = serverTimeMs;
        return true;
    }

    private boolean shouldApplyItemDelta(long incomingTick, long serverTimeMs) {
        if (incomingTick >= 0) {
            if (lastItemSnapshotTick >= 0 && Long.compareUnsigned(incomingTick, lastItemSnapshotTick) < 0) {
                return false;
            }
            if (lastItemDeltaTick >= 0 && Long.compareUnsigned(incomingTick, lastItemDeltaTick) <= 0) {
                return false;
            }
            lastItemDeltaTick = incomingTick;
            if (serverTimeMs > lastItemSyncServerTimeMs) {
                lastItemSyncServerTimeMs = serverTimeMs;
            }
            return true;
        }
        if (serverTimeMs <= lastItemSyncServerTimeMs) {
            return false;
        }
        lastItemSyncServerTimeMs = serverTimeMs;
        return true;
    }

    private void applyItemDeltaStates(List<Message.ItemState> items, long incomingTick, long serverTimeMs) {
        if (items == null || items.isEmpty()) {
            return;
        }
        if (!shouldApplyItemDelta(incomingTick, serverTimeMs)) {
            return;
        }
        for (Message.ItemState itemState : items) {
            applyItemState(itemState);
        }
    }

    /**
     * 缁樺埗褰撳墠鍦烘櫙涓殑閬撳叿
     * @param delta
     */
    private void renderItems(float delta) {
        if (batch == null || itemViews.isEmpty()) {
            return;
        }
        for (ItemView view : itemViews.values()) {
            if (view == null) {
                continue;
            }
            TextureRegion region = view.region != null ? view.region : getItemFallbackRegion();
            if (region == null) {
                continue;
            }
            float width = region.getRegionWidth();
            float height = region.getRegionHeight();
            float drawX = view.position.x - width / 2f;
            float drawY = view.position.y - height / 2f;
            batch.draw(region, drawX, drawY, width, height);
        }
    }

    /**
    濞撳憡鍨欐稉璇叉儕閻?     */
    @Override
    public void render(float delta) {
        advanceLogicalClock(delta);//閺囧瓨鏌婃稉鈧稉顏喦旂€规氨娈戦弮鍫曟？鐠哄啿褰?
        if (!reconnectHoldActive) {
            pumpPendingNetworkInput();
        }

        /*
        hasReceivedInitialState:濞撳憡鍨欓崚婵嗩潗閻樿埖鈧?        playerTextureRegion:鐟欐帟澹婄痪鍦倞
         */
        if (!hasReceivedInitialState || playerTextureRegion == null) {
            maybeRequestInitialStateResync();
            if (reconnectHoldActive) {
                renderReconnectOverlay();
            } else {
                renderLoadingOverlay();
            }
            return;
        }
        /*
        闁插洭娉﹂張顒€婀存潏鎾冲弳
         */
        float renderDelta = getStableDelta(delta);
        Vector2 dir = getMovementInput();
        isLocallyMoving = dir.len2() > 0.0001f;//閸掋倖鏌囬弰顖氭儊閸︺劎些閸?
        boolean upgradeOverlayActive = isUpgradeOverlayActive();
        boolean suppressInput = upgradeOverlayActive || reconnectHoldActive;
        if (suppressInput) {
            dir.setZero();
            isLocallyMoving = false;
        }
        if (!reconnectHoldActive && Gdx.input.isKeyJustPressed(AUTO_ATTACK_TOGGLE_KEY)) {
            autoAttackToggle = !autoAttackToggle;
            if (autoAttackToggle) {
                resetAutoAttackState();
            } else {
                autoAttackAccumulator = 0f;
                autoAttackHoldTimer = 0f;
            }
            showStatusToast(autoAttackToggle ? "閼奉亜濮╅弨璇插毊瀹告彃绱戦崥?" : "濮╅弨璇插毊瀹告彃鍙ч梻?");
        }
        boolean attacking = reconnectHoldActive ? false : resolveAttackingState(renderDelta);
        if (upgradeOverlayActive) {
            attacking = false;
        }

        /*
        妫板嫭绁撮幙宥勭稊
         */
        simulateLocalStep(dir, renderDelta);
        if (reconnectHoldActive) {
            resetPendingInputAccumulator();
        } else {
            processInputChunk(dir, attacking, renderDelta);
        }

        /*
        濞撳懎鐫嗛崝鐘垫祲閺堥缚绐￠梾?         */
        Gdx.gl.glClearColor(0, 0, 0, 1);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);

        clampPositionToMap(predictedPosition);
        updatePlayerFacing(renderDelta);
        updateDisplayPosition(renderDelta);
        clampPositionToMap(displayPosition);
        camera.position.set(displayPosition.x, displayPosition.y, 0);
        camera.update();

        /*
        瀵偓婵瑕嗛弻?         */
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        batch.draw(backgroundTexture, 0, 0, WORLD_WIDTH, WORLD_HEIGHT);
        renderItems(renderDelta);
        /*
        閹绢厽鏂侀悳鈺侇啀缁屾椽妫介崝銊ф暰
         */
        playerAnimationTime += renderDelta;
        TextureRegion currentFrame = playerIdleAnimation != null
                ? playerIdleAnimation.getKeyFrame(playerAnimationTime, true)//瀵邦亞骞嗛幘顓熸杹
                : playerTextureRegion;
        if (currentFrame == null) {
            batch.end();
            return;
        }
        playerTextureRegion = currentFrame;
        /*
        娴兼壆鐣婚張宥呭閸ｃ劍妞傞梻?鐠侊紕鐣诲〒鍙夌厠瀵ゆ儼绻?
         */
        long estimatedServerTimeMs = estimateServerTimeMs();
        long renderServerTimeMs = estimatedServerTimeMs - computeRenderDelayMs();
        updateProjectiles(renderDelta, renderServerTimeMs);
        /*
        濞撳弶鐓嬮弫灞兼眽閸滃瞼甯虹€?         */
        renderEnemies(renderDelta, renderServerTimeMs);
        renderRemotePlayers(renderServerTimeMs, currentFrame, renderDelta);
        renderProjectiles();
        renderProjectileImpacts(renderDelta);
        /*
        濞撳弶鐓嬮張顒€婀撮悳鈺侇啀鐟欐帟澹?
         */
        if (isSelfAlive) {
            drawCharacterFrame(currentFrame, displayPosition.x, displayPosition.y, facingRight);
        }
        renderStatusToast(renderDelta);

        batch.end();
        renderUpgradeOverlay(renderDelta);
        if (reconnectHoldActive) {
            renderReconnectBanner();
        }
    }

    /**
     * 閺囧瓨鏌婃稉鈧稉顏喦旂€规氨娈戦弮鍫曟？鐠哄啿褰?
     * @param delta
     */
    private void advanceLogicalClock(float delta) {
        double total = delta * 1000.0 + logicalClockRemainderMs;
        long advance = (long) total;
        logicalClockRemainderMs = total - advance;
        logicalTimeMs += advance;
    }

    /**
     * 楠炶櫕绮﹂幙宥勭稊
     * @param rawDelta
     * @return
     */
    private float getStableDelta(float rawDelta) {
        float clamped = Math.min(rawDelta, MAX_FRAME_DELTA);//鐠佸墽鐤嗛張鈧径褍鎶氶梻鎾闂冨弶顒涢弸浣搞亣鐢?
        smoothedFrameDelta += (clamped - smoothedFrameDelta) * DELTA_SMOOTH_ALPHA;//閹稿洦鏆熼獮铏拨閹垮秳缍?閸掆晝鏁IR娴ｅ酣鈧碍鎶ゅ▔銏犳珤
        logFrameDeltaSpike(rawDelta, smoothedFrameDelta);//閺冦儱绻旀潏鎾冲毉
        return smoothedFrameDelta;
    }

    /**
     * 妫板嫭绁撮幙宥勭稊,閺堫剙婀撮惄瀛樺复閻劑顣╁ù瀣箻鐞涘瞼些閸?     * @param dir
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
     * 閹垫挸瀵橀崣鎴︹偓浣哄负鐎规儼绶崗?     * @param dir
     * @param attacking
     * @param delta
     */
    private void processInputChunk(Vector2 dir, boolean attacking, float delta) {
        boolean moving = dir.len2() > 0.0001f;//閸掋倖鏌囬弰顖氭儊閸︺劎些閸?        //閺堫亞些閸斻劍婀弨璇插毊
        if (!moving && !attacking) {
            if (hasPendingInputChunk) {
                flushPendingInput();
            }
            if (!idleAckSent) {
                sendIdleCommand(delta);
            }
            return;
        }
        //闁插秶鐤嗙粚娲＝閺嶅洤绻?
        idleAckSent = false;
        //妫ｆ牗顐奸幙宥勭稊
        if (!hasPendingInputChunk) {
            startPendingChunk(dir, attacking, delta);
            if (pendingAttack) {
                flushPendingInput();
            }
            return;
        }
        //閸掋倖鏌囬弰顖氭儊閸欘垰鎮庨獮?
        if (pendingMoveDir.epsilonEquals(dir, 0.001f) && pendingAttack == attacking) {
            pendingInputDuration += delta;//閸欘垯浜?瀵ゅ爼鏆遍幐浣虹敾閺冨爼妫?
        } else {
            flushPendingInput();//娑撳秴褰叉禒?缂佹挻娼弮褍娼?
            startPendingChunk(dir, attacking, delta);//瀵偓婵鏌婇崸?
        }
        //閺€璇插毊閸滃本瀵滈柨顔藉瘮缂侇厽妞傞梻纾嬬Т鏉╁洣绗傞梽?
            if (pendingAttack || pendingInputDuration >= MAX_COMMAND_DURATION) {
            flushPendingInput();
        }
    }

    /**
     * 閸掓繂顫愰崠鏍ㄥ闂団偓閻樿埖鈧?     * @param dir
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
     * 鐏忓棗顓归幋椋庮伂閻ㄥ嫯绶崗銉﹀ⅵ閸栧懎褰傜紒娆愭箛閸斺€虫珤
     */
    private void flushPendingInput() {
        //濡偓閺屻儲妲搁崥锔芥箒瀵板懎褰傞柅浣烘畱鏉堟挸鍙?
        if (!hasPendingInputChunk) {
            return;
        }
        float duration = resolvePendingDurationSeconds();
        PlayerInputCommand cmd = new PlayerInputCommand(inputSequence++, pendingMoveDir, pendingAttack, duration);
        unconfirmedInputs.put(cmd.seq, cmd);//鐎涙ê鍙嗛張顏嗏€樼拋銈堢翻閸忋儳娈戠紓鎾崇摠
        inputSendTimes.put(cmd.seq, logicalTimeMs);
        pruneUnconfirmedInputs();
        sendPlayerInputToServer(cmd);
        resetPendingInputAccumulator();
    }

    /**
     * 閸︺劍妫ゆ潏鎾冲弳閺冭泛鎮滈張宥呭閸ｃ劌褰傞柅浣哥妇鐠?     * @param delta
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
     * 濞撳懐鎮婇張顏嗏€樼拋銈夋Е閸?     */
    private void pruneUnconfirmedInputs() {
        if (unconfirmedInputs.isEmpty()) {
            return;
        }

        /*
        鐎瑰鍙忔潻顓濆敩閸掔娀娅庢潻鍥ㄦ埂閺夛紕娲?
         */
        long now = logicalTimeMs;
        Iterator<Map.Entry<Integer, PlayerInputCommand>> iterator = unconfirmedInputs.entrySet().iterator();
        boolean removed = false;
        while (iterator.hasNext()) {
            Map.Entry<Integer, PlayerInputCommand> entry = iterator.next();
            int seq = entry.getKey();
            long sentAt = inputSendTimes.getOrDefault(seq, entry.getValue().timestampMs);
            /*
            閸掋倖鏌囬弰顖氭儊閻喓娈戦棁鈧憰浣稿灩闂?             */
            boolean tooOld = (now - sentAt) > MAX_UNCONFIRMED_INPUT_AGE_MS;
            boolean overflow = unconfirmedInputs.size() > MAX_UNCONFIRMED_INPUTS;
            if (tooOld || overflow) {
                iterator.remove();
                inputSendTimes.remove(seq);
                removed = true;
                continue;
            }
            //閹绘劕澧犵紒鍫燁剾闁秴宸?
            if (!tooOld && !overflow) {
                break;
            }
        }

        /*
        閺冦儱绻旀稉搴ｂ敄闂傝尙濮搁幀浣割槱閻?         */
        if (removed) {
            Gdx.app.log(TAG, "Pruned stale inputs, remaining=" + unconfirmedInputs.size());
        }
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
    }

    /**
     * 濞撳弶鐓嬮崗鏈电铂閻溾晛顔?
     * @param renderServerTimeMs
     * @param frame
     * @param delta
     */
    private void renderRemotePlayers(long renderServerTimeMs, TextureRegion frame, float delta) {
        //缁岀儤顥呴弻?
        if (frame == null) {
            return;
        }
        //闁秴宸诲В蹇庨嚋閻溾晛顔嶉惃鍕彥閻?
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
            //閹绘帒鈧厧灏梻?
             ServerPlayerSnapshot prev = null;
            ServerPlayerSnapshot next = null;
            for (ServerPlayerSnapshot snap : snapshots) {
                if (snap.serverTimestampMs <= renderServerTimeMs) {
                    prev = snap;//閺堚偓閺傛壆娈戦弮褍鎻╅悡?
                    } else {
                    next = snap;//娑撳绔存稉顏勬彥閻?
                    break;
                }
            }

            Vector2 targetPos = renderBuffer;
            /*
             閹绘帒鈧壈顓哥粻妤冪摜閻?             */
            //閺堝绗傛稉鈧稉顏勬嫲娑撳绔存稉顏勬皑楠炶櫕绮﹂幓鎺嶈厬閸?
             if (prev != null && next != null && next.serverTimestampMs > prev.serverTimestampMs) {
                float t = (renderServerTimeMs - prev.serverTimestampMs) /
                        (float) (next.serverTimestampMs - prev.serverTimestampMs);
                t = MathUtils.clamp(t, 0f, 1f);
                targetPos.set(prev.position).lerp(next.position, t);
            }//閸欘亝婀乸rev鐏忛亶顣╁ù瀣╃娑擃亝鏌婇崐
             else if (prev != null) {
                long ahead = Math.max(0L, renderServerTimeMs - prev.serverTimestampMs);
                long clampedAhead = Math.min(ahead, MAX_EXTRAPOLATION_MS);
                float seconds = clampedAhead / 1000f;
                targetPos.set(prev.position).mulAdd(prev.velocity, seconds);
            }//閸欘亝婀侀弬鏉库偓鐓庢皑閻╁瓨甯撮悽?
             else {
                targetPos.set(next.position);
            }
            clampPositionToMap(targetPos);
            boolean remoteFacing = remoteFacingRight.getOrDefault(playerId, true);
            //閼惧嘲褰囬崚婵嗩潗閸栨牔缍呯純顔剧处鐎?
             Vector2 displayPos = remoteDisplayPositions
                    .computeIfAbsent(playerId, id -> new Vector2(targetPos));
            //楠炶櫕绮︽潻鍥у,鐏炵偘绨弰顖欑箽闂勨晝娈戞穱婵嬫珦,闂冨弶顒涢崶鐘插瘶鐠哄疇绌柅鐘冲灇閻ㄥ嫮鐛婇崣?
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
     * 濞撳弶鐓嬮弫灞兼眽
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
     * 閹靛綊鍣哄〒鍙夌厠閹舵洖鐨犻悧?     */
    private void renderProjectiles() {
        //閸撳秶鐤嗛弶鈥叉
        if (projectileViews.isEmpty() || batch == null) {
            logProjectileRenderState(projectileViews.isEmpty() ? "skip_empty" : "skip_batch_null");
            return;
        }
        //闁秴宸婚幎鏇炵殸閻?
        for (ProjectileView view : projectileViews.values()) {
            TextureRegion frame = resolveProjectileFrame(view);
            //閸斻劍鈧浇袙閺嬫劘鍒涢崶鎯ф姎
            if (frame == null) {
                logProjectileRenderState("skip_frame_null");
                continue;
            }
            //閼惧嘲褰囩亸鍝勵嚟缂佹ê鍩楁担宥囩枂
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
     * 濞撳弶鐓嬮幎鏇炵殸閻椻晛鎳℃稉顓犳窗閺嶅洦妞傞惃鍕仜闂傚澹掗弫?     * @param delta
     */
    private void renderProjectileImpacts(float delta) {
        //鐎瑰鍙忓Λ鈧弻?
        if (projectileImpacts.isEmpty() || batch == null) {
            return;
        }
        if (projectileImpactAnimation == null) {
            projectileImpacts.clear();
            return;
        }
        //閸欏秴鎮滈柆宥呭坊閺囧瓨鏌婇悽鐔锋嚒閸涖劍婀?
        for (int i = projectileImpacts.size - 1; i >= 0; i--) {
            ProjectileImpact impact = projectileImpacts.get(i);
            impact.elapsed += delta;
            //閼惧嘲褰囪ぐ鎾冲閸斻劎鏁剧敮?
            TextureRegion frame = projectileImpactAnimation.getKeyFrame(impact.elapsed, false);
            if (frame == null || projectileImpactAnimation.isAnimationFinished(impact.elapsed)) {
                projectileImpacts.removeIndex(i);
                continue;
            }
            //濞撳弶鐓?
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
     * 閹舵洖鐨犻悧鈺佸З閻㈣鎶氱憴锝嗙€介崳?     */
    private TextureRegion resolveProjectileFrame(ProjectileView view) {
        if (projectileAnimation != null) {
            return projectileAnimation.getKeyFrame(view.animationTime, true);
        }
        return projectileFallbackRegion;
    }

    /**
     * 閸掋倖鏌囬幎鏇炵殸閻椻晜妲搁崥锕傤棧閸戦缚绔熼悾?     * @param position
     * @return
     */
    private boolean isProjectileOutOfBounds(Vector2 position) {
        float margin = 32f;
        return position.x < -margin || position.x > WORLD_WIDTH + margin
                || position.y < -margin || position.y > WORLD_HEIGHT + margin;
    }

    /**
     * 閹稿洤鐣鹃崸鎰垼鐟欙箑褰傞崨鎴掕厬閻楄鏅?
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
     * ui鐠у嫭绨崣宥夘洯缂佸嫪娆?
     * @param delta
     */
    private void renderStatusToast(float delta) {
        //閺囧瓨鏌婇崐鎺曨吀閺?
        if (statusToastTimer > 0f) {
            statusToastTimer -= delta;
        }
        //婢舵岸鍣搁柅鈧崙鐑樻蒋娴?
        if (statusToastTimer <= 0f || loadingFont == null || statusToastMessage == null
                || statusToastMessage.isBlank() || batch == null) {
            return;
        }
        //閺傚洤鐡ф０婊嗗
        loadingFont.setColor(Color.WHITE);
        //鐏炲繐绠峰锔跨瑐鐟欐帞娈戞稉鏍櫕閸ф劖鐖?
        float padding = 20f;
        float drawX = camera.position.x - WORLD_WIDTH / 2f + padding;
        float drawY = camera.position.y + WORLD_HEIGHT / 2f - padding;
        loadingFont.draw(batch, statusToastMessage, drawX, drawY);
    }

    private void renderUpgradeOverlay(float delta) {
        if (!isUpgradeOverlayActive() || upgradeStage == null) {
            return;
        }
        upgradeStage.act(delta);
        upgradeStage.draw();
    }

    private void initUpgradeOverlay() {
        if (upgradeStage != null) {
            return;
        }
        Skin skin = game.getSkin();
        if (skin == null) {
            Gdx.app.log(TAG, "Skin is not initialized, skip upgrade overlay init");
            return;
        }
        upgradeStage = new Stage(new FitViewport(WORLD_WIDTH, WORLD_HEIGHT));
        upgradeRoot = new Table();
        upgradeRoot.setFillParent(true);
        upgradeRoot.setVisible(false);
        Drawable dimBackground = new TextureRegionDrawable(new TextureRegion(getUpgradeOverlayTexture()));
        upgradeRoot.setBackground(dimBackground);
        upgradeStage.addActor(upgradeRoot);

        Table panel = new Table(skin);
        panel.defaults().pad(8f);
        upgradeTitleLabel = new Label("", skin,"default_32");
        upgradeTitleLabel.setAlignment(Align.center);
        upgradeTitleLabel.setFontScale(UPGRADE_TEXT_SCALE);
        upgradeHintLabel = new Label("", skin,"default_32");
        upgradeHintLabel.setAlignment(Align.center);
        upgradeHintLabel.setFontScale(UPGRADE_TEXT_SCALE);
        upgradeHintLabel.setWrap(true);

        Table cardsRow = new Table();
        cardsRow.defaults()
                .pad(10f)
                .width(UPGRADE_CARD_WIDTH)
                .height(UPGRADE_CARD_HEIGHT);
        upgradeCards.clear();
        for (int i = 0; i < 3; i++) {
            UpgradeCard card = new UpgradeCard(skin);
            upgradeCards.add(card);
            card.addListener(new ClickListener() {
                @Override
                public void clicked(InputEvent event, float x, float y) {
                    onUpgradeCardClicked(card);
                }
            });
            cardsRow.add(card).size(UPGRADE_CARD_WIDTH, UPGRADE_CARD_HEIGHT);
        }

        upgradeRefreshButton = new TextButton("刷新", skin,"CreateButton");
        upgradeRefreshButton.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                requestUpgradeRefresh();
            }
        });
        upgradeRefreshButton.getLabel().setFontScale(UPGRADE_TEXT_SCALE);

        panel.add(upgradeTitleLabel).growX().row();
        panel.add(cardsRow).growX().row();
        panel.add(upgradeHintLabel).growX().padTop(6f).row();
        panel.add(upgradeRefreshButton).padTop(12f).width(220f).height(60f);
        upgradeRoot.add(panel).center().width(WORLD_WIDTH * 0.85f);
        upgradeStage.getViewport().update(Gdx.graphics.getWidth(), Gdx.graphics.getHeight(), true);
    }

    private void resetUpgradeFlowState() {
        upgradeFlowState = UpgradeFlowState.IDLE;
        upgradeBlocking = false;
        upgradeSession = null;
        pendingUpgradeOptionIndex = -1;
        if (upgradeRoot != null) {
            upgradeRoot.setVisible(false);
        }
    }

    private void enterUpgradeMode() {
        if (upgradeStage == null) {
            initUpgradeOverlay();
        }
        upgradeBlocking = true;
        if (upgradeRoot != null) {
            upgradeRoot.setVisible(true);
        }
        enableUpgradeInput();
        updateUpgradeOverlayContent();
    }

    private void exitUpgradeMode() {
        upgradeBlocking = false;
        if (upgradeRoot != null) {
            upgradeRoot.setVisible(false);
        }
        disableUpgradeInput();
    }

    private void enableUpgradeInput() {
        if (upgradeStage == null) {
            return;
        }
        previousInputProcessor = Gdx.input.getInputProcessor();
        upgradeInputMultiplexer = new InputMultiplexer(upgradeStage);
        Gdx.input.setInputProcessor(upgradeInputMultiplexer);
    }

    private void disableUpgradeInput() {
        if (Gdx.input.getInputProcessor() == upgradeInputMultiplexer) {
            Gdx.input.setInputProcessor(previousInputProcessor);
        }
        upgradeInputMultiplexer = null;
        previousInputProcessor = null;
    }

    private boolean isUpgradeOverlayActive() {
        return upgradeBlocking && upgradeSession != null;
    }

    private void updateUpgradeOverlayContent() {
        if (upgradeRoot == null) {
            return;
        }
        boolean visible = isUpgradeOverlayActive();
        upgradeRoot.setVisible(visible);
        if (!visible) {
            return;
        }
        if (upgradeTitleLabel != null) {
            upgradeTitleLabel.setText(buildUpgradeTitle());
        }
        if (upgradeHintLabel != null) {
            upgradeHintLabel.setText(buildUpgradeHint());
        }
        if (upgradeRefreshButton != null && upgradeSession != null) {
            boolean canRefresh = canRefreshOptions();
            upgradeRefreshButton.setDisabled(!canRefresh);
            upgradeRefreshButton.setText("刷新 (剩余 " + upgradeSession.refreshRemaining + ")");
        }
        populateUpgradeCards();
    }

    private void populateUpgradeCards() {
        if (upgradeCards == null || upgradeCards.isEmpty()) {
            return;
        }
        if (upgradeSession == null || upgradeFlowState == UpgradeFlowState.WAITING_OPTIONS) {
            for (UpgradeCard card : upgradeCards) {
                card.showPlaceholder("等待卡牌…");
            }
            return;
        }
        List<Message.UpgradeOption> options = upgradeSession.options;
        for (int i = 0; i < upgradeCards.size; i++) {
            UpgradeCard card = upgradeCards.get(i);
            if (options == null || i >= options.size()) {
                card.showPlaceholder("暂无选项");
                continue;
            }
            Message.UpgradeOption option = options.get(i);
            Message.UpgradeLevel level = resolveHighestLevel(option);
            Drawable drawable = resolveUpgradeDrawable(level);
            String title = formatOptionTitle(option);
            String description = formatEffects(option);
            boolean selectable = upgradeFlowState == UpgradeFlowState.CHOOSING;
            boolean confirming = upgradeFlowState == UpgradeFlowState.CONFIRMING
                    && option.getOptionIndex() == pendingUpgradeOptionIndex;
            String footer = confirming ? "等待确认…" : (selectable ? "点击选择" : "");
            card.bindOption(option, title, description, footer, drawable, selectable, confirming);
        }
    }

    private String buildUpgradeTitle() {
        if (upgradeSession == null) {
            return "升级选择";
        }
        return formatUpgradeReason(upgradeSession.reason);
    }

    private String buildUpgradeHint() {
        return switch (upgradeFlowState) {
            case WAITING_OPTIONS -> "正在生成卡牌，请稍候…";
            case CONFIRMING -> "已提交选择，等待服务器确认…";
            case CHOOSING -> upgradeSession != null
                    ? "剩余刷新次数：" + upgradeSession.refreshRemaining
                    : "请选择一项升级";
            default -> "";
        };
    }

    private Message.UpgradeLevel resolveHighestLevel(Message.UpgradeOption option) {
        Message.UpgradeLevel result = Message.UpgradeLevel.UPGRADE_LEVEL_UNKNOWN;
        if (option == null || option.getEffectsCount() == 0) {
            return result;
        }
        int best = Message.UpgradeLevel.UPGRADE_LEVEL_UNKNOWN.getNumber();
        for (Message.UpgradeEffect effect : option.getEffectsList()) {
            if (effect.getLevel().getNumber() > best) {
                best = effect.getLevel().getNumber();
                result = effect.getLevel();
            }
        }
        return result;
    }

    private String formatOptionTitle(Message.UpgradeOption option) {
        if (option == null || option.getEffectsCount() == 0) {
            return "未知升级";
        }
        Message.UpgradeEffect effect = option.getEffects(0);
        return formatUpgradeTypeName(effect.getType()) + " · " + formatUpgradeLevelName(effect.getLevel());
    }

    private String formatEffects(Message.UpgradeOption option) {
        if (option == null || option.getEffectsCount() == 0) {
            return "暂无效果描述";
        }
        StringBuilder builder = new StringBuilder();
        for (Message.UpgradeEffect effect : option.getEffectsList()) {
            builder.append("- ")
                    .append(formatUpgradeTypeName(effect.getType()))
                    .append(" +")
                    .append(effect.getValue())
                    .append(System.lineSeparator());
        }
        return builder.toString().trim();
    }

    private String formatUpgradeTypeName(Message.UpgradeType type) {
        return switch (type) {
            case UPGRADE_TYPE_MOVE_SPEED -> "移动速度";
            case UPGRADE_TYPE_ATTACK -> "攻击力";
            case UPGRADE_TYPE_ATTACK_SPEED -> "攻击速度";
            case UPGRADE_TYPE_MAX_HEALTH -> "生命上限";
            case UPGRADE_TYPE_CRITICAL_RATE -> "暴击率";
            default -> "属性强化";
        };
    }

    private String formatUpgradeLevelName(Message.UpgradeLevel level) {
        return switch (level) {
            case UPGRADE_LEVEL_LOW -> "低级";
            case UPGRADE_LEVEL_MEDIUM -> "中级";
            case UPGRADE_LEVEL_HIGH -> "高级";
            default -> "未知";
        };
    }

    private String formatUpgradeReason(Message.UpgradeReason reason) {
        return switch (reason) {
            case UPGRADE_REASON_LEVEL_UP -> "升级奖励";
            case UPGRADE_REASON_REFRESH -> "刷新结果";
            default -> "随机奖励";
        };
    }

    private Drawable resolveUpgradeDrawable(Message.UpgradeLevel level) {
        Texture texture = getUpgradeLevelTexture(level);
        if (texture == null) {
            return null;
        }
        return new TextureRegionDrawable(new TextureRegion(texture));
    }

    private Texture getUpgradeLevelTexture(Message.UpgradeLevel level) {
        if (level == null) {
            return null;
        }
        Texture cached = upgradeLevelTextures.get(level);
        if (cached != null) {
            return cached;
        }
        String path = resolveLevelTexturePath(level);
        try {
            Texture texture = new Texture(Gdx.files.internal(path));
            upgradeLevelTextures.put(level, texture);
            return texture;
        } catch (Exception e) {
            Gdx.app.log(TAG, "Failed to load upgrade background: " + path, e);
            return null;
        }
    }

    private String resolveLevelTexturePath(Message.UpgradeLevel level) {
        return switch (level) {
            case UPGRADE_LEVEL_LOW -> "background/low.png";
            case UPGRADE_LEVEL_MEDIUM -> "background/medium.png";
            case UPGRADE_LEVEL_HIGH -> "background/high.png";
            default -> "background/medium.png";
        };

    }

    private Texture getUpgradeOverlayTexture() {
        if (upgradeOverlayTexture == null) {
            Pixmap pixmap = new Pixmap(1, 1, Pixmap.Format.RGBA8888);
            pixmap.setColor(0f, 0f, 0f, 0.78f);
            pixmap.fill();
            upgradeOverlayTexture = new Texture(pixmap);
            pixmap.dispose();
        }
        return upgradeOverlayTexture;
    }

    private void requestUpgradeRefresh() {
        if (!canRefreshOptions()) {
            return;
        }
        upgradeFlowState = UpgradeFlowState.WAITING_OPTIONS;
        updateUpgradeOverlayContent();
        if (!game.sendUpgradeRefreshRequest(currentRoomId)) {
            showStatusToast("刷新请求发送失败，网络异常");
            upgradeFlowState = UpgradeFlowState.CHOOSING;
            updateUpgradeOverlayContent();
        }
    }

    private void onUpgradeCardClicked(UpgradeCard card) {
        if (card == null || !card.hasOption() || upgradeFlowState != UpgradeFlowState.CHOOSING) {
            return;
        }
        pendingUpgradeOptionIndex = card.getOptionIndex();
        upgradeFlowState = UpgradeFlowState.CONFIRMING;
        updateUpgradeOverlayContent();
        if (!game.sendUpgradeSelect(currentRoomId, pendingUpgradeOptionIndex)) {
            showStatusToast("发送升级选择失败");
            upgradeFlowState = UpgradeFlowState.CHOOSING;
            pendingUpgradeOptionIndex = -1;
            updateUpgradeOverlayContent();
        } else {
            showStatusToast("已提交升级选择");
        }
    }

    private boolean canRefreshOptions() {
        return upgradeSession != null
                && upgradeSession.refreshRemaining > 0
                && upgradeFlowState == UpgradeFlowState.CHOOSING;
    }

    /**
     * 閻樿埖鈧焦褰佺粈鐑樻煙濞?濮ｆ柨顩ф潻鐐村复閹存劕濮?閹垛偓閼宠棄鍑＄紒蹇毿掗柨?     * @param message
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
                    .append(" sampleRot=").append(sample.rotationDeg)
                    .append(" ttl(ms)=").append(sample.expireClientTimeMs - logicalTimeMs);
        }
        Gdx.app.log(TAG, builder.toString());
    }

    /**
     * 闁挸鍙块崶鎯у剼閻ㄥ嫯顫嬮崶鎯ь潗閸?
     */
    private enum UpgradeFlowState {
        IDLE,
        WAITING_OPTIONS,
        CHOOSING,
        CONFIRMING
    }

    private static final class UpgradeSession {
        int roomId;
        int playerId;
        Message.UpgradeReason reason = Message.UpgradeReason.UPGRADE_REASON_UNKNOWN;
        final List<Message.UpgradeOption> options = new ArrayList<>();
        int refreshRemaining;
    }

    private final class UpgradeCard extends Table {
        private final Label titleLabel;
        private final Label descLabel;
        private final Label footerLabel;
        private Message.UpgradeOption option;
        private String defaultFooter = "";
        private boolean selectable;

        UpgradeCard(Skin skin) {
            super(skin);
            setTransform(true);
            setOrigin(Align.center);
            defaults().pad(4f).growX();
            setSize(UPGRADE_CARD_WIDTH, UPGRADE_CARD_HEIGHT);
            setBounds(getX(), getY(), UPGRADE_CARD_WIDTH, UPGRADE_CARD_HEIGHT);
            titleLabel = new Label("", skin);
            titleLabel.setAlignment(Align.center);
            titleLabel.setFontScale(UPGRADE_TEXT_SCALE);
            descLabel = new Label("", skin);
            descLabel.setWrap(true);
            descLabel.setAlignment(Align.center);
            descLabel.setFontScale(UPGRADE_TEXT_SCALE);
            footerLabel = new Label("", skin);
            footerLabel.setAlignment(Align.center);
            footerLabel.setFontScale(UPGRADE_TEXT_SCALE);
            add(titleLabel).padBottom(6f).row();
            add(descLabel).padBottom(6f).growX().row();
            add(footerLabel);
            addListener(new InputListener() {
                @Override
                public void enter(InputEvent event, float x, float y, int pointer, Actor fromActor) {
                    if (selectable) {
                        addAction(Actions.scaleTo(1.04f, 1.04f, 0.08f));
                    }
                }

                @Override
                public void exit(InputEvent event, float x, float y, int pointer, Actor toActor) {
                    addAction(Actions.scaleTo(1f, 1f, 0.08f));
                }
            });
        }

        @Override
        public float getPrefWidth() {
            return UPGRADE_CARD_WIDTH;
        }

        @Override
        public float getPrefHeight() {
            return UPGRADE_CARD_HEIGHT;
        }

        void bindOption(Message.UpgradeOption option,
                        String title,
                        String description,
                        String footer,
                        Drawable background,
                        boolean selectable,
                        boolean confirming) {
            this.option = option;
            this.selectable = selectable && !confirming;
            defaultFooter = footer == null ? "" : footer;
            setBackground(background);
            titleLabel.setText(title == null ? "" : title);
            descLabel.setText(description == null ? "" : description);
            footerLabel.setText(confirming ? "等待确认…" : defaultFooter);
            setTouchable(this.selectable ? Touchable.enabled : Touchable.disabled);
            setColor(this.selectable ? Color.WHITE : Color.GRAY);
        }

        void showPlaceholder(String message) {
            option = null;
            selectable = false;
            setTouchable(Touchable.disabled);
            setBackground((Drawable) null);
            setColor(Color.DARK_GRAY);
            titleLabel.setText("等待");
            descLabel.setText(message);
            footerLabel.setText("");
        }

        boolean hasOption() {
            return option != null;
        }

        int getOptionIndex() {
            return option != null ? (int) option.getOptionIndex() : -1;
        }
    }

    private static final class ItemView {
        final int itemId;
        final Vector2 position = new Vector2();
        TextureRegion region;
        int typeId;
        Message.ItemEffectType effectType = Message.ItemEffectType.ITEM_EFFECT_NONE;

        ItemView(int itemId) {
            this.itemId = itemId;
        }

        void update(TextureRegion textureRegion, Vector2 sourcePosition, int typeId, Message.ItemEffectType effectType) {
            if (textureRegion != null) {
                this.region = textureRegion;
            }
            this.position.set(sourcePosition);
            this.typeId = typeId;
            this.effectType = effectType;
        }
    }

    /**
     * 閹舵洖鐨犻悧鈺佹躬鐎广垺鍩涚粩顖濅氦闁插繒娈戠憴鍡楁禈
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
     * 閹舵洖鐨犻悧鈺佹嚒娑擃厾娈戦惉顒勬？閻楄鏅?
     */
    private static final class ProjectileImpact {
        final Vector2 position = new Vector2();
        float elapsed;
    }

    /**
     * 閸掓稑缂撴稉鈧稉顏勫窗娴ｅ秵鏅禍?     */
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
     * 缁夊娅庨崡鐘辩秴閺佸奔姹?
     */
    private void removePlaceholderEnemy() {
        enemyViews.remove(PLACEHOLDER_ENEMY_ID);
        enemyLastSeen.remove(PLACEHOLDER_ENEMY_ID);
    }

    /**
     * 缁夊娅庡璁抽閺佸奔姹?
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
     * 濞撳懐鎮婇梹鎸庢闂傚瓨婀弴瀛樻煀閻ㄥ嫭鏅禍?     * @param serverTimeMs
     */
    /**
     * 鐏忎浇顥婃稉鈧稉顏勫灡瀵ょ瘝nemyView鐎电钖勯惃鍕煙濞?     * @param enemyState
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
     * 閸旂姾娴囬幍鈧張澶婂З閻㈡槒绁┃?     */
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
     * 閸╄桨绨崝銊︹偓渚€鍘ょ純顔煎鏉炲€熺カ濠?     * @param definition
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
     * 閺屻儲澹橀崝銊ф暰,閼宠姤澹橀崚鏉挎皑閻?閹靛彞绗夐崚鏉挎皑闂勫秶楠囨稉娲帛鐠併倕濮╅悽?     * @param typeId
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
     * 閹虫帒濮炴潪鎴掔娑擃亝鏅禍鐑樻綏鐠愩劎娈戦崡鐘辩秴闂冨弶顒涚挧鍕爱娑撱垹銇?
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

    private TextureRegion resolveItemTexture(int serverTypeId) {
        TextureRegion cached = itemTextureRegions.get(serverTypeId);
        if (cached != null) {
            return cached;
        }
        String texturePath = resolveItemTexturePath(serverTypeId);
        if (texturePath == null || texturePath.isEmpty()) {
            return getItemFallbackRegion();
        }
        try {
            Texture texture = new Texture(Gdx.files.internal(texturePath));
            TextureRegion region = new TextureRegion(texture);
            itemTextureRegions.put(serverTypeId, region);
            itemTextureHandles.put(serverTypeId, texture);
            return region;
        } catch (Exception e) {
            Gdx.app.log(TAG, "Failed to load item texture typeId=" + serverTypeId + " path=" + texturePath, e);
            return getItemFallbackRegion();
        }
    }

    private String resolveItemTexturePath(int serverTypeId) {
        if (Config.PROP_CONFIG == null || Config.PROP_CONFIG.isEmpty()) {
            return null;
        }
        int configIndex = serverTypeId - ITEM_TYPE_ID_OFFSET;
        if (configIndex < 0 || configIndex >= Config.PROP_CONFIG.size()) {
            Gdx.app.log(TAG, "Prop texture missing for typeId=" + serverTypeId + " index=" + configIndex);
            return null;
        }
        String path = Config.PROP_CONFIG.get(configIndex);
        if (path == null || path.isEmpty()) {
            Gdx.app.log(TAG, "Prop texture path empty for typeId=" + serverTypeId + " index=" + configIndex);
            return null;
        }
        return path;
    }

    private TextureRegion getItemFallbackRegion() {
        if (itemFallbackRegion == null) {
            Pixmap pixmap = new Pixmap(48, 48, Pixmap.Format.RGBA8888);
            pixmap.setColor(0.95f, 0.82f, 0.28f, 1f);
            pixmap.fillCircle(24, 24, 20);
            pixmap.setColor(0.99f, 0.95f, 0.72f, 1f);
            pixmap.drawCircle(24, 24, 20);
            itemFallbackTexture = new Texture(pixmap);
            itemFallbackRegion = new TextureRegion(itemFallbackTexture);
            pixmap.dispose();
        }
        return itemFallbackRegion;
    }
    /**
     * 濞撳懐鎮婇弫灞兼眽娣団剝浼?
     */
    private void disposeEnemyAtlases() {
        for (TextureAtlas atlas : enemyAtlasCache.values()) {
            atlas.dispose();
        }
        enemyAtlasCache.clear();
    }

    /**
     * 閸旂姾娴囬弨璇插毊鐠у嫭绨?
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

    private void disposeItemAssets() {
        for (Texture texture : itemTextureHandles.values()) {
            if (texture != null) {
                texture.dispose();
            }
        }
        itemTextureHandles.clear();
        itemTextureRegions.clear();
        if (itemFallbackTexture != null) {
            itemFallbackTexture.dispose();
            itemFallbackTexture = null;
            itemFallbackRegion = null;
        }
    }

    /**
     * 缂佹ê鍩楃憴鎺曞
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
     * 楠炲疇銆€缂冩垹绮堕幎鏍уЗ閸滃苯娆㈡潻?娴ｆ寧褰冮崐闂寸瑝鏉╁洤瀹冲鐐叉倵
     * @return
     */
    private long computeRenderDelayMs() {
        //鐠侊紕鐣籖TT瀵ゆ儼绻?
        float latencyComponent = smoothedRttMs * 0.25f + 12f;
        if (Float.isNaN(latencyComponent) || Float.isInfinite(latencyComponent)) {
            latencyComponent = 90f;
        }
        latencyComponent = MathUtils.clamp(latencyComponent, 45f, 150f);
        //閸氬本顒為幎鏍уЗ閻ㄥ嫮绱﹂崘鎻掑亶婢?
        float jitterReserve = Math.abs(smoothedSyncIntervalMs - 33f) * 0.5f
                + (smoothedSyncDeviationMs * 1.3f) + 18f;
        jitterReserve = MathUtils.clamp(jitterReserve, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        //绾喖鐣惧鎯扮箿
        float target = Math.max(latencyComponent, jitterReserve);
        target = MathUtils.clamp(target, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        //楠炶櫕绮︽潻鍥у閸掓壆娲伴弽鍥р偓?
        float delta = target - renderDelayMs;
        delta = MathUtils.clamp(delta, -MAX_RENDER_DELAY_STEP_MS, MAX_RENDER_DELAY_STEP_MS);
        renderDelayMs = MathUtils.clamp(renderDelayMs + delta * RENDER_DELAY_LERP,
                INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        return Math.round(renderDelayMs);
    }

    /**
     * 閹舵牕濮╅弮銉ョ箶
     * @param intervalMs
     * @param smoothInterval
     */
    private void logSyncIntervalSpike(float intervalMs, float smoothInterval) {
        //閸氬牏鎮婇惃鍕閸斻劋绗夋径鍕倞
        if (intervalMs < SYNC_INTERVAL_LOG_THRESHOLD_MS) {
            return;
        }
        long nowMs = TimeUtils.millis();
        if (nowMs - lastSyncLogMs < SYNC_INTERVAL_LOG_INTERVAL_MS) {
            return;
        }
        //閸欘亜顦╅悶鍡楃磽鐢憡濮堥崝?
        Gdx.app.log(TAG, "Sync interval spike=" + intervalMs + "ms smooth=" + smoothInterval
                + "ms renderDelay=" + renderDelayMs);
        lastSyncLogMs = nowMs;
    }

    /**
     * 闂冨弶顒涙径鍕倞鏉╁洦婀￠幋鏍偓鍛础鎼村繐瀵?
     * @param syncTime
     * @param serverTimeMs
     * @param arrivalMs
     * @return
     */
    private boolean shouldAcceptStatePacket(Message.Timestamp syncTime, long serverTimeMs, long arrivalMs) {
        //閺嶅洤鍣崠鏉ck
        long incomingTick = syncTime != null
                ? Integer.toUnsignedLong(syncTime.getTick())
                : -1L;
        //tick閺嶏繝鐛?
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
        //tick閺冪姵鏅ラ惃鍕礀闁偓
        if (lastAppliedServerTimeMs >= 0L && serverTimeMs <= lastAppliedServerTimeMs) {
            logDroppedSync("serverTime", serverTimeMs, serverTimeMs, arrivalMs);
            return false;
        }

        lastAppliedServerTimeMs = serverTimeMs;
        return true;
    }

    /**
     * 閸欘亜顦╅悶鍡樺瘹鐎规艾娆㈡潻鐔烘畱閺冦儱绻?
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
     * 鏉╂稖顢戠純鎴犵捕鐠囧﹥鏌?楠炶埖褰佹笟娑橀挬濠婃垵顦╅悶?     * @param arrivalMs
     */
    private void updateSyncArrivalStats(long arrivalMs) {
        //妫ｆ牗顐兼稉宥堢箻鐞涘苯顦╅悶?
        if (lastSyncArrivalMs != 0L) {
            //瑜版挸澧犻崠鍛存？闂呮梻瀹崇粵澶夌艾閺堝秴濮熼崳銊ф畱閸欐垿鈧線顣堕悳?
            float interval = arrivalMs - lastSyncArrivalMs;
            //閹稿洦鏆熼獮铏拨楠炲啿娼庨梻鎾---娴ｅ酣鈧碍鎶ゅ▔?
            smoothedSyncIntervalMs += (interval - smoothedSyncIntervalMs) * SYNC_INTERVAL_SMOOTH_ALPHA;
            //鐠侊紕鐣婚獮璺洪挬濠婃垶濮堥崝?
            float deviation = Math.abs(interval - smoothedSyncIntervalMs);
            smoothedSyncDeviationMs += (deviation - smoothedSyncDeviationMs) * SYNC_DEVIATION_SMOOTH_ALPHA;
            //鐎瑰鍙忔径鍕倞
            smoothedSyncDeviationMs = MathUtils.clamp(smoothedSyncDeviationMs, 0f, 220f);
            //閺冦儱绻旀径鍕倞
            logSyncIntervalSpike(interval, smoothedSyncIntervalMs);
        }
        //閺囧瓨鏌婇崚鎷屾彧閺冨爼妫?
        lastSyncArrivalMs = arrivalMs;
    }

    /**
     * 濮濓絿鈥橀惃鍕強缁犳绻欑粙瀣负鐎规湹绱堕崚鐗堟拱閸︾増婀囬崝鈥虫珤閻ㄥ嫭妞傞梻?鏉╂稖鈧奔鍙婄粻妤€鍤ぐ鎾冲閺堝秴濮熼崳銊︽闂?     * @param serverTimeMs
     */
    //TODO:閻劋绗夐悽銊ㄋ夐崑绺峊T
    private void sampleClockOffset(long serverTimeMs) {
        long arrivalLocalTimeMs = getMonotonicTimeMs();
        double offsetSample = serverTimeMs - arrivalLocalTimeMs;
        clockOffsetMs += (offsetSample - clockOffsetMs) * 0.1;
    }
    /**
     * 閺堫剙婀撮弮鍫曟？
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
     * 绾喕绻氱憴鎺曞娑撳秳绱扮挧鏉垮毉閸︽澘娴?
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
     * 閼惧嘲褰噖asd鏉堟挸鍙?
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
     * 閺堫剙婀村Ο鈩冨珯閻溾晛顔嶇粔璇插З
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
     * 閹垫挸瀵橀崣鎴︹偓?     * @param cmd
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
     * 鐎广垺鍩涚粩顖氬絺闁線鈧喓宸奸梽鎰煑
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
    閸欐垿鈧椒鍞惍?     */
    private void sendInputImmediately(Message.C2S_PlayerInput msg, long timestampMs) {
        if (game.trySendPlayerInput(msg)) {
            lastInputSendMs = timestampMs;
            pendingRateLimitedInput = null;
        } else {
            pendingRateLimitedInput = msg;
        }
    }

    /**
    婵″倹鐏夐張顒€婀存潏鎾冲弳鏉堟儳鍩屾禍鍡樻付鐏忓繐褰傞柅渚€妫块梾鏂挎皑閹垫挸瀵橀崣鎴濆毉閸?     */
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
     * 鐎广垺鍩涚粩顖氼槱閻炲棙婀囬崝鈥虫珤閸忋劑鍣哄〒鍛婂灆閸氬本顒為崠?     * @param sync
     */
    public void onGameStateReceived(Message.S2C_GameStateSync sync) {
        long arrivalMs = TimeUtils.millis();//閸栧懎鍩屾潏鍓ф畱閺冨爼妫?
        setCurrentRoomId((int) sync.getRoomId());
        //鐎瑰鍙忛張鍝勫煑
        Message.Timestamp syncTime = sync.hasSyncTime() ? sync.getSyncTime() : null;
        long incomingTick = syncTime != null ? Integer.toUnsignedLong(syncTime.getTick()) : -1L;
        if (!shouldAcceptStatePacket(syncTime, sync.getServerTimeMs(), arrivalMs)) {
            return;
        }
        if (syncTime != null) {
            game.updateServerTick(Integer.toUnsignedLong(syncTime.getTick()));
        }
        updateSyncArrivalStats(arrivalMs);
        sampleClockOffset(sync.getServerTimeMs());
        //婢跺嫮鎮婇弫灞兼眽閻樿埖鈧?
        enemyStateCache.clear();
        if (!sync.getEnemiesList().isEmpty()) {
            for (Message.EnemyState enemy : sync.getEnemiesList()) {
                enemyStateCache.put((int) enemy.getEnemyId(), enemy);
            }
        }
        syncEnemyViews(sync.getEnemiesList(), sync.getServerTimeMs(), true);
        handlePlayersFromServer(sync.getPlayersList(), sync.getServerTimeMs());
        if (shouldApplyItemSnapshot(incomingTick, sync.getServerTimeMs())) {
            syncItemViews(sync.getItemsList());
        }
        if (game.isAwaitingReconnectSnapshot()) {
            game.onReconnectSnapshotApplied();
        }
    }

    /**
     * 鐎广垺鍩涚粩顖氼槱閻炲棗顤冮柌蹇撴倱濮濄儳娈戦崗銉ュ經閸戣姤鏆?
     * @param delta
     */
    public void onGameStateDeltaReceived(Message.S2C_GameStateDeltaSync delta) {
        //閹恒儲鏁归崠鍛板箯閸欐牕鎮撳銉︽闂?
        long arrivalMs = TimeUtils.millis();
        setCurrentRoomId((int) delta.getRoomId());
        Message.Timestamp syncTime = delta.hasSyncTime() ? delta.getSyncTime() : null;
        long deltaTick = syncTime != null ? Integer.toUnsignedLong(syncTime.getTick()) : -1L;
        //閺堝鏅ラ幀褎顥呮?
        if (!shouldAcceptStatePacket(syncTime, delta.getServerTimeMs(), arrivalMs)) {
            return;
        }
        //閸氬牆鑻熼悳鈺侇啀婢х偤鍣洪崚鏉跨暚閺佸濮搁幀?
        List<Message.PlayerState> mergedPlayers = new ArrayList<>(delta.getPlayersCount());
        for (Message.PlayerStateDelta playerDelta : delta.getPlayersList()) {
            Message.PlayerState merged = mergePlayerDelta(playerDelta);
            if (merged != null) {
                mergedPlayers.add(merged);
            }
        }
        //閸氬牆鑻熼弫灞兼眽婢х偤鍣洪崚鏉跨暚閺佸濮搁幀?
        List<Message.EnemyState> updatedEnemies = new ArrayList<>(delta.getEnemiesCount());
        for (Message.EnemyStateDelta enemyDelta : delta.getEnemiesList()) {
            Message.EnemyState mergedEnemy = mergeEnemyDelta(enemyDelta);
            if (mergedEnemy != null) {
                updatedEnemies.add(mergedEnemy);
            }
        }
        //缂冩垹绮剁拹銊╁櫤+閺冨爼鎸撻弽鈥冲櫙
        updateSyncArrivalStats(arrivalMs);//閺囧瓨鏌婇崑蹇曅╅幐鍥ㄧ垼
        sampleClockOffset(delta.getServerTimeMs());//娴兼壆鐣荤€广垺鍩涚粩顖氬煂閺堝秴濮熼崳銊ф畱閸嬪繒些闁?
        // 閸掑棗褰傛径鍕倞閻樿埖鈧?
        handlePlayersFromServer(mergedPlayers, delta.getServerTimeMs());
        if (!updatedEnemies.isEmpty()) {
            syncEnemyViews(updatedEnemies, delta.getServerTimeMs(), false);
        }
        if (!delta.getItemsList().isEmpty()) {
            applyItemDeltaStates(delta.getItemsList(), deltaTick, delta.getServerTimeMs());
        }
        if (delta.hasSyncTime()) {
            game.updateServerTick(Integer.toUnsignedLong(delta.getSyncTime().getTick()));
        }
    }

    public void enterReconnectHold() {
        if (reconnectHoldActive) {
            return;
        }
        reconnectHoldActive = true;
        reconnectHoldStartMs = TimeUtils.millis();
        pendingRateLimitedInput = null;
        resetPendingInputAccumulator();
    }

    public void exitReconnectHold() {
        reconnectHoldActive = false;
        reconnectHoldStartMs = 0L;
    }

    public void resetWorldStateForFullSync() {
        serverPlayerStates.clear();
        enemyViews.clear();
        enemyStateCache.clear();
        enemyLastSeen.clear();
        projectileViews.clear();
        projectileImpacts.clear();
        clearItemState();
        removePlaceholderEnemy();
        spawnPlaceholderEnemy();
        resetTargetingState();
        resetAutoAttackState();
        resetInitialStateTracking();
        hasReceivedInitialState = false;
    }

    private UpgradeSession ensureUpgradeSession() {
        if (upgradeSession == null) {
            upgradeSession = new UpgradeSession();
        }
        return upgradeSession;
    }

    private void setCurrentRoomId(int roomId) {
        if (roomId > 0) {
            currentRoomId = roomId;
            game.updateActiveRoomId(roomId);
        }
    }

    public void onUpgradeRequest(Message.S2C_UpgradeRequest request) {
        if (request == null) {
            return;
        }
        setCurrentRoomId((int) request.getRoomId());
        if (request.getPlayerId() != game.getPlayerId()) {
            showStatusToast("玩家" + request.getPlayerId() + " 正在选择升级");
            return;
        }
        UpgradeSession session = ensureUpgradeSession();
        session.roomId = currentRoomId;
        session.playerId = (int) request.getPlayerId();
        session.reason = request.getReason();
        session.options.clear();
        session.refreshRemaining = 0;
        pendingUpgradeOptionIndex = -1;
        upgradeFlowState = UpgradeFlowState.WAITING_OPTIONS;
        enterUpgradeMode();
        if (!game.sendUpgradeRequestAck(currentRoomId)) {
            showStatusToast("确认升级请求失败，网络异常");
        }
    }

    public void onUpgradeOptions(Message.S2C_UpgradeOptions optionsMessage) {
        if (optionsMessage == null) {
            return;
        }
        setCurrentRoomId((int) optionsMessage.getRoomId());
        if (optionsMessage.getPlayerId() != game.getPlayerId()) {
            return;
        }
        UpgradeSession session = ensureUpgradeSession();
        session.roomId = currentRoomId;
        session.playerId = (int) optionsMessage.getPlayerId();
        session.reason = optionsMessage.getReason();
        session.options.clear();
        session.options.addAll(optionsMessage.getOptionsList());
        session.refreshRemaining = optionsMessage.getRefreshRemaining();
        upgradeFlowState = UpgradeFlowState.CHOOSING;
        pendingUpgradeOptionIndex = -1;
        enterUpgradeMode();
        if (!game.sendUpgradeOptionsAck(currentRoomId)) {
            showStatusToast("确认升级选项失败，网络异常");
        }
    }

    public void onUpgradeSelectAck(Message.S2C_UpgradeSelectAck ack) {
        if (ack == null || ack.getPlayerId() != game.getPlayerId()) {
            return;
        }
        showStatusToast("升级完成，继续战斗！");
        exitUpgradeMode();
        resetUpgradeFlowState();
    }

    /**
     * 閺堝秴濮熼崳銊ヮ槱閻炲棙婀伴崷鎵负鐎硅泛鎷版潻婊呪柤閻溾晛顔嶉悩鑸碘偓浣烘畱閺嶇绺鹃柅鏄忕帆
     * @param players
     * @param serverTimeMs
     */
    private void handlePlayersFromServer(Collection<Message.PlayerState> players, long serverTimeMs) {
        //閼惧嘲褰囬張顒€婀撮悳鈺侇啀id
        int myId = game.getPlayerId();
        Message.PlayerState selfStateFromServer = null;
        //闁秴宸婚幍鈧張澶屽负鐎瑰墎濮搁幀?
        if (players != null) {
            for (Message.PlayerState player : players) {
                int playerId = (int) player.getPlayerId();
                serverPlayerStates.put(playerId, player);
                //閺堫剙婀撮悳鈺侇啀,濞茶崵娼冮幍宥咁槱閻?
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
                //鏉╂粎鈻奸悳鈺侇啀--鐠佹澘缍嶈箛顐ゅ弾
                if (player.hasPosition()) {
                    Vector2 position = new Vector2(player.getPosition().getX(), player.getPosition().getY());
                    pushRemoteSnapshot(playerId, position, player.getRotation(), serverTimeMs);
                    remotePlayerLastSeen.put(playerId, serverTimeMs);
                }
            }
        }
        //濞撳懐鎮婃潻鍥ㄦ埂閻溾晛顔?
        purgeStaleRemotePlayers(serverTimeMs);
        //鎼存梻鏁ら張宥呭閸ｃ劌顕懛顏囬煩閻ㄥ嫮鐓?
        if (selfStateFromServer != null) {
            applySelfStateFromServer(selfStateFromServer);
        }
    }

    /**
     * 鐎广垺鍩涚粩顖濈箼缁嬪鎮撳銉︽櫕娴滆桨淇婇幁?     * @param enemies
     * @param serverTimeMs
     */
    private void syncEnemyViews(Collection<Message.EnemyState> enemies, long serverTimeMs, boolean replaceAll) {
        if (replaceAll) {
            enemyViews.clear();
            enemyLastSeen.clear();
            removePlaceholderEnemy();
        }
        if (enemies == null || enemies.isEmpty()) {
            return;
        }

        removePlaceholderEnemy();
        for (Message.EnemyState enemy : enemies) {
            if (enemy == null || !enemy.hasPosition()) {
                continue;
            }
            int enemyId = (int) enemy.getEnemyId();
            if (!enemy.getIsAlive()) {
                removeEnemy(enemyId);
                continue;
            }
            EnemyView view = ensureEnemyView(enemy);
            renderBuffer.set(enemy.getPosition().getX(), enemy.getPosition().getY());
            clampPositionToMap(renderBuffer);
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
            enemyLastSeen.put(enemyId, serverTimeMs);
        }
    }

    /**
     * 鐎广垺鍩涚粩顖氼嚠閺堫剙婀撮悳鈺侇啀鏉╂稖顢戦張宥呭閸ｃ劎濮搁幀浣虹叓濮?瀹割喕绗夋径姘皑閺勵垵绐￠張宥呭閸ｃ劌鎮撳銉ф畱鏉╁洨鈻?
     * @param selfStateFromServer
     */
    private void applySelfStateFromServer(Message.PlayerState selfStateFromServer) {
        //閼惧嘲褰囬張宥呭閸ｃ劋缍呯純?
        Vector2 serverPos = new Vector2(
                selfStateFromServer.getPosition().getX(),
                selfStateFromServer.getPosition().getY()
        );
        clampPositionToMap(serverPos);
        //閹绘劕褰囬崗鍐╂殶閹?婢跺嫮鎮婇張鈧崥搴ょ翻閸忋儳娈戞惔蹇撳娇
        int lastProcessedSeq = selfStateFromServer.getLastProcessedInputSeq();
        game.updateConfirmedInputSeq(lastProcessedSeq);
        //閸掓稑缂撶€涙ê鍋嶉悩鑸碘偓浣告彥閻?
        PlayerStateSnapshot snapshot = new PlayerStateSnapshot(
                serverPos,
                selfStateFromServer.getRotation(),
                lastProcessedSeq
        );
        snapshotHistory.offer(snapshot);
        while (snapshotHistory.size() > 10) {
            snapshotHistory.poll();
        }
        //閻樿埖鈧礁宕楃拫?
        reconcileWithServer(snapshot);
    }

    /**
     * 鐎广垺鍩涚粩顖涘复閺€鎯扮箼缁嬪甯虹€硅泛鎻╅悡褌绠ｉ崥搴㈢€鐑樻箛閸斺€虫珤韫囶偆鍙庨獮鍓佹樊閹躲倖绮﹂崝銊х崶閸欙絿绱︾€?     * @param playerId
     * @param position
     * @param rotation
     * @param serverTimeMs
     */
    private void pushRemoteSnapshot(int playerId, Vector2 position, float rotation, long serverTimeMs) {
        clampPositionToMap(position);
        //閼惧嘲褰囪箛顐ゅ弾闂冪喎鍨?
        Deque<ServerPlayerSnapshot> queue = remotePlayerServerSnapshots
                .computeIfAbsent(playerId, k -> new ArrayDeque<>());
        //闁喎瀹虫导鎵暬
        Vector2 velocity = new Vector2();
        ServerPlayerSnapshot previous = queue.peekLast();//閺堚偓閺傛澘鎻╅悡
        if (previous != null) {
            long deltaMs = serverTimeMs - previous.serverTimestampMs;
            if (deltaMs > 0) {
                velocity.set(position).sub(previous.position).scl(1000f / deltaMs);
            } else {
                velocity.set(previous.velocity);//閻劍妫柅鐔峰
            }
            //闁喎瀹虫稉濠囨闂勬劕鍩?
            float maxSpeed = PLAYER_SPEED * 1.5f;
            if (velocity.len2() > maxSpeed * maxSpeed) {
                velocity.clamp(0f, maxSpeed);
            }
        }
        //閺囧瓨鏌婇張婵嗘倻
        updateRemoteFacing(playerId, velocity, rotation);
        //閸掓稑缂撻獮璺虹摠閸忋儱鎻╅悡?
        ServerPlayerSnapshot snap = new ServerPlayerSnapshot(position, rotation, velocity, serverTimeMs);
        queue.addLast(snap);
        //濠婃垵濮╃粣妤€褰涘〒鍛倞
        while (queue.size() > 1 &&
                (serverTimeMs - queue.peekFirst().serverTimestampMs) > SNAPSHOT_RETENTION_MS) {
            queue.removeFirst();
        }
    }

    /**
     * 閺囧瓨鏌婇悳鈺侇啀閺堟繂鎮?鐠佹儳鐣鹃弰顖氬涧閼宠棄涔忛崣宕囩倳鏉?     * @param playerId
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
     * 濞撳懐鎮婇梹鎸庢闂傚瓨婀弨璺哄煂濞戝牊浼呴惃鍕箖閺堢喓甯虹€?     * @param currentServerTimeMs
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
     * 瀹革箑褰哥紙鏄忔祮閺嶇绺鹃柅鏄忕帆-绾喛顓荤憴鎺曞閺勵垰鎯佺拠銉︽篂閸氭垵褰告潏?     * @param rotation
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
     * 閺堝秴濮熼崳銊ょ閼峰瓨鈧呮畱閹垮秳缍?閸栧懏瀚弽鈩冾劀,楠炶櫕绮?闁插秵鏂?
     * @param serverSnapshot
     */
    private void reconcileWithServer(PlayerStateSnapshot serverSnapshot) {
        //RTT閸嬪繒些闁插繒娈戞导鎵暬
        PlayerInputCommand acknowledged = unconfirmedInputs.get(serverSnapshot.lastProcessedInputSeq);
        if (acknowledged != null) {
            Long sentLogical = inputSendTimes.remove(acknowledged.seq);
            float sample;
            if (sentLogical != null) {
                sample = logicalTimeMs - sentLogical;//濞撳憡鍨欓柅鏄忕帆閺冨爼妫?
            } else {
                sample = System.currentTimeMillis() - acknowledged.timestampMs;//閸ョ偤鈧偓閸掓壆閮寸紒鐔告闂?
                }
            if (sample > 0f) {
                smoothedRttMs = MathUtils.lerp(smoothedRttMs, sample, 0.2f);
            }
        }
        //鎼存梻鏁ら張宥呭閸ｃ劎濮搁幀浣告嫲閸掓繂顫愰崠鏍ь槱閻?
            float correctionDist = predictedPosition.dst(serverSnapshot.position);
        boolean wasInitialized = hasReceivedInitialState;
        predictedPosition.set(serverSnapshot.position);
        predictedRotation = serverSnapshot.rotation;
        facingRight = inferFacingFromRotation(predictedRotation);
        clampPositionToMap(predictedPosition);
        logServerCorrection(correctionDist, serverSnapshot.lastProcessedInputSeq);
        if (!wasInitialized) {
            clearInitialStateWait();
            displayPosition.set(predictedPosition);//妫ｆ牗顐奸崥灞绢劄閻╁瓨甯寸捄瀹犳祮
        }
        //闁插秵鏂侀張顏嗏€樼拋銈堢翻閸?
            for (PlayerInputCommand input : unconfirmedInputs.values()) {
            if (input.seq > serverSnapshot.lastProcessedInputSeq) {
                applyInputLocally(predictedPosition, predictedRotation, input, input.deltaSeconds);
            }
        }
        //濞撳懐鎮婂鑼€樼拋銈堢翻閸?
            unconfirmedInputs.entrySet().removeIf(entry -> {
            boolean applied = entry.getKey() <= serverSnapshot.lastProcessedInputSeq;
            if (applied) {
                inputSendTimes.remove(entry.getKey());
            }
            return applied;
        });
        pruneUnconfirmedInputs();
        //閻樿埖鈧焦鐖ｈ箛妤佹纯閺?
            hasReceivedInitialState = true;
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
        //濞撳弶鐓嬫担宥囩枂楠炶櫕绮︽潻鍥у
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= (DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE * 4f)) {
            displayPosition.set(predictedPosition);
        } else {
            displayPosition.lerp(predictedPosition, 0.35f);
        }
    }

    /**
     * 婢跺嫮鎮婇悳鈺侇啀婢х偤鍣洪崣妯哄
     * @param delta
     * @return
     */
    private Message.PlayerState mergePlayerDelta(Message.PlayerStateDelta delta) {
        //缁屽搫鈧吋顥呴弻?
        if (delta == null) {
            return null;
        }
        int playerId = (int) delta.getPlayerId();
        Message.PlayerState base = serverPlayerStates.get(playerId);
        //閻溾晛顔嶆＃鏍偧閸戣櫣骞囬幋鏍偓鍛嚒鏉╁洦婀?
        if (base == null) {
            requestDeltaResync("player_" + playerId);
            return null;
        }
        //閺囧瓨鏌?
        Message.PlayerState.Builder builder = base.toBuilder();
        //娴ｅ秵甯洪惍渚€鈹嶉崝銊︽纯閺?
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
        //閺囧瓨鏌婄紓鎾崇摠,鏉╂柨娲?
        Message.PlayerState updated = builder.build();
        serverPlayerStates.put(playerId, updated);
        return updated;
    }

    /**
     * 婢跺嫮鎮婇弫灞兼眽婢х偤鍣洪崣妯哄
     * @param delta
     * @return
     */
    private Message.EnemyState mergeEnemyDelta(Message.EnemyStateDelta delta) {
        //缁屽搫鈧吋顥呴弻?
        if (delta == null) {
            return null;
        }
        int enemyId = (int) delta.getEnemyId();
        Message.EnemyState base = enemyStateCache.get(enemyId);
        //閺佸奔姹夋＃鏍偧閸戣櫣骞囬幋鏍偓鍛Ц閹椒娑径?
        if (base == null) {
            requestDeltaResync("enemy_" + enemyId);
            return null;
        }
        //閺囧瓨鏌?
        Message.EnemyState.Builder builder = base.toBuilder();
        //娴ｅ秵甯洪惍渚€鈹嶉崝銊уЦ閹焦娲块弬?
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
        //閺囧瓨鏌婄紓鎾崇摠,鏉╂柨娲?
        Message.EnemyState updated = builder.build();
        enemyStateCache.put(enemyId, updated);
        return updated;
    }

    /**
     * 鏉╂稖顢戦崗銊╁櫤閸氬本顒?
     * @param reason
     */
    private void requestDeltaResync(String reason) {
        //game閺堝鏅ラ獮鏈电瑬娑撳秴婀崘宄板祱閺冨爼妫块崘?
        if (game == null) {
            return;
        }
        long now = TimeUtils.millis();
        if ((now - lastDeltaResyncRequestMs) < DELTA_RESYNC_COOLDOWN_MS) {
            return;
        }
        //閺囧瓨鏌婇弮鍫曟？楠炴湹绗栭崗銊╁櫤閸氬本顒?
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
            Vector2 spawnPosition = projectileTempVector;
            boolean hasSpawnPosition = state.hasPosition();
            if (hasSpawnPosition) {
                setVectorFromProto(state.getPosition(), spawnPosition);
            } else {
                spawnPosition.set(displayPosition);
            }
            Vector2 originPosition = projectileOriginBuffer;
            if (hasSpawnPosition) {
                // 浠ユ湇鍔＄涓嬪彂鐨勫嚭鐢熺偣涓烘潈濞佷綅缃?
                originPosition.set(spawnPosition);
            } else {
                resolveProjectileOrigin(state, spawnPosition, originPosition);
            }
            view.position.set(originPosition);
            float rotationDeg = state.getRotation();
            view.rotationDeg = rotationDeg;
            float serverSpeed = state.hasProjectile() ? state.getProjectile().getSpeed() : 0f;
            float appliedSpeed = PEA_PROJECTILE_SPEED > 0f ? PEA_PROJECTILE_SPEED : serverSpeed;
            Vector2 direction = projectileDirectionBuffer;
            // 鏂瑰悜浣跨敤鏈嶅姟绔绠楃殑 rotation锛岄伩鍏嶅鎴风浜屾鎺ㄧ畻甯︽潵鍋忓樊
            float dirX = MathUtils.cosDeg(rotationDeg);
            float dirY = MathUtils.sinDeg(rotationDeg);
            direction.set(dirX, dirY);
            if (direction.isZero(0.001f)) {
                direction.set(1f, 0f);
            } else {
                direction.nor();
            }
            view.velocity.set(direction).scl(appliedSpeed);
            long ttlMs = Math.max(50L, state.getTtlMs());
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
                    + " rot=" + rotationDeg
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
        if (ownerId == game.getPlayerId()) {
            outOrigin.set(displayPosition);
            return;
        }
        if (ownerId > 0) {
            Vector2 remoteDisplay = remoteDisplayPositions.get(ownerId);
            if (remoteDisplay != null) {
                outOrigin.set(remoteDisplay);
                return;
            }
            Message.PlayerState ownerState = serverPlayerStates.get(ownerId);
            if (ownerState != null && ownerState.hasPosition()) {
                outOrigin.set(ownerState.getPosition().getX(), ownerState.getPosition().getY());
                return;
            }
        }
        if (targetPosition != null) {
            outOrigin.set(targetPosition);
        } else {
            outOrigin.set(displayPosition);
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
                ? "浣犲彈鍒颁簡" + hurt.getDamage() + "鐐逛激瀹? 鍓╀綑琛€閲?" + hurt.getRemainingHealth()
                : "鐜╁ " + playerId + " 鍙楀埌浜?" + hurt.getDamage() + " 鐐逛激瀹?";
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
                ? "宸叉湁鏁屼汉琚?" + died.getKillerPlayerId() + "鍑昏触"
                : "鏁屼汉" + enemyId + " 琚秷鐏?";
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
                ? "浣犲凡鍗囩骇鍒?Lv." + levelUp.getNewLevel()
                : "鐜╁" + playerId + " 鍒拌揪浜哃v." + levelUp.getNewLevel();
        showStatusToast(toast);
    }

    private void handleDroppedItem(Message.S2C_DroppedItem droppedItem) {
        if (droppedItem == null || droppedItem.getItemsCount() == 0) {
            return;
        }
        showStatusToast("鎺夎惤浜?" + droppedItem.getItemsCount() + " 涓垬鍒╁搧");
        for (Message.ItemState itemState : droppedItem.getItemsList()) {
            if (itemState == null) {
                continue;
            }
            int itemId = (int) itemState.getItemId();
            Message.ItemState cached = itemStateCache.get(itemId);
            if (cached != null && !cached.getIsPicked()) {
                continue;
            }
            applyItemState(itemState);
        }
    }

    private void handleGameOver(Message.S2C_GameOver gameOver) {
        if (gameOver == null) {
            showStatusToast( "娓告垙缁撴潫");
        } else {
            showStatusToast(gameOver.getVictory() ? "鎴樻枟鑳滃埄锛?" : "鎴樻枟澶辫触");
        }
        projectileViews.clear();
        projectileImpacts.clear();
        resetTargetingState();
        resetAutoAttackState();
        clearItemState();
        if (!hasShownGameOver) {
            hasShownGameOver = true;
            game.setScreen(new GameOverScreen(game, gameOver));
        }
     else {
            showStatusToast(gameOver.getVictory() ? "鎭枩閫氬叧" : "涓嬫鍔姏");
        }
        projectileViews.clear();
        projectileImpacts.clear();
        resetTargetingState();
        resetAutoAttackState();
        clearItemState();
    }
    /**
     * 濞撳憡鍨欓悳顖氼暔閻樿埖鈧胶娈戦弸姘閸掋倖鏌?
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
        if (upgradeStage != null) {
            upgradeStage.getViewport().update(width, height, true);
        }
    }

    @Override
    public void pause() { }

    @Override
    public void resume() { }

    @Override
    public void hide() {
        disableUpgradeInput();
    }

    @Override
    public void dispose() {
        disableUpgradeInput();
        if (batch != null) batch.dispose();
        if (playerTexture != null) playerTexture.dispose();
        if (playerAtlas != null) playerAtlas.dispose();
        disposeEnemyAtlases();
        disposeProjectileAssets();
        disposeItemAssets();
        enemyAnimations.clear();
        if (enemyFallbackTexture != null) {
            enemyFallbackTexture.dispose();
            enemyFallbackTexture = null;
            enemyFallbackRegion = null;
        }
        enemyViews.clear();
        projectileViews.clear();
        projectileImpacts.clear();
        clearItemState();
        if (backgroundTexture != null) backgroundTexture.dispose();
        if (loadingFont != null) {
            loadingFont.dispose();
            loadingFont = null;
        }
        if (upgradeStage != null) {
            upgradeStage.dispose();
            upgradeStage = null;
        }
        if (upgradeOverlayTexture != null) {
            upgradeOverlayTexture.dispose();
            upgradeOverlayTexture = null;
        }
        for (Texture texture : upgradeLevelTextures.values()) {
            if (texture != null) {
                texture.dispose();
            }
        }
        upgradeLevelTextures.clear();
    }

    /**
     * 娴ｅ秶鐤嗘穱顔筋劀閸滃本妫╄箛妤勭翻閸?     * @param delta
     */
    private void updateDisplayPosition(float delta) {
        //閺堫亜鍨垫慨瀣閸掓瑨鐑︽潻?
        if (!hasReceivedInitialState) {
            return;
        }
        //闂堢偞婀伴崷鐗堝付閸掓儼顫楅懝鎻掓皑閻╁瓨甯撮崥灞绢劄
        if (!isLocallyMoving) {
            displayPosition.set(predictedPosition);
            return;
        }
        //鐏忓繐浜稿顔绘叏濮?
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE) {
            displayPosition.set(predictedPosition);
            return;
        }
        //婢堆冧焊瀹?閹躲儴顒?
        float distance = (float) Math.sqrt(distSq);
        if (distance > DISPLAY_DRIFT_LOG_THRESHOLD) {
            logDisplayDrift(distance);
        }
        //楠炶櫕绮﹂幓鎺戔偓?閸氭垿顣╁ù瀣╃秴缂冾噣娼幏?
        float alpha = MathUtils.clamp(delta * DISPLAY_LERP_RATE, 0f, 1f);
        displayPosition.lerp(predictedPosition, alpha);
    }

    /**
     * 缂佹瑤绨ｆ稉鈧稉顏喦旂€规氨娈戦弮銉ョ箶鏉堟挸鍤?
     * @param rawDelta
     * @param stableDelta
     */
    private void logFrameDeltaSpike(float rawDelta, float stableDelta) {
        //閸欘亜顦╅悶鍡氱Т鏉╁洩绉撮弮鍫曟鎼达妇娈戦幎銉╂晩
        if (Math.abs(rawDelta - stableDelta) < DELTA_SPIKE_THRESHOLD) {
            return;
        }
        //缂佹瑤绨ｆ稉鈧稉顏喦旂€规氨娈戦弮銉ョ箶鏉堟挸鍤０鎴犲芳
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDeltaSpikeLogMs < DELTA_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Frame delta spike raw=" + rawDelta + " stable=" + stableDelta
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDeltaSpikeLogMs = nowMs;
    }

    /**
     * 鏉堟挸鍤弮銉ョ箶,鐠佹澘缍嶉張宥呭閸ｃ劋缍呯純顔界墡濮濓絿娈戠拠濠冩焽閸戣姤鏆?
     * @param correctionDist
     * @param lastProcessedSeq
     */
    private void logServerCorrection(float correctionDist, int lastProcessedSeq) {
        //鏉╁洦鎶ゅ顔肩毈閻偅顒?
        if (correctionDist < POSITION_CORRECTION_LOG_THRESHOLD) {
            return;
        }
        //閼惧嘲褰囬弮鍫曟？,绾喕绻氶弮銉ョ箶閸欐垿鈧線顣堕悳?
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
     * 鏉堟挸鍤弮銉ョ箶
     * @param drift
     */
    private void logDisplayDrift(float drift) {
        //闂冩彃灏介幀褏绱粙?娴滃本顐奸弽锟犵崣濠曞倻些闂冨牆鈧?
        if (drift < DISPLAY_DRIFT_LOG_THRESHOLD) {
            return;
        }
        //閼惧嘲褰囬弮鍫曟？,绾喕绻氶弮銉ョ箶閸欐垿鈧線顣堕悳?
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDisplayDriftLogMs < DISPLAY_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Display drift=" + drift + " facingRight=" + facingRight
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDisplayDriftLogMs = nowMs;
    }

    /**
     * 鐠侊紕鐣婚張顒侇偧鏉堟挸鍙嗛惃鍕瘮缂侇厽妞傞梻?     * @return
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
     * 濞撳懐鎮婇悳鈺侇啀鏉堟挸鍙嗛獮璺哄絺闁胶绮伴張宥呭閸ｃ劌鎮楁担娆庣瑓閻ㄥ嫪澶嶉弮璺哄綁闁插繐鍣虹亸鎴ｇカ濠ф劖绉烽懓?     */
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

    private void clearItemState() {
        clearItemState(true);
    }

    private void clearItemState(boolean resetTracking) {
        itemViews.clear();
        itemStateCache.clear();
        if (resetTracking) {
            lastItemSnapshotTick = -1L;
            lastItemDeltaTick = -1L;
            lastItemSyncServerTimeMs = -1L;
        }
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
     * 闁插秶鐤嗙€广垺鍩涚粩顖滃Ц閹礁鑻熼崣鎴︹偓浣稿弿闁插繗顕Ч?     */
    private void resetInitialStateTracking() {
        initialStateStartMs = TimeUtils.millis();
        lastInitialStateRequestMs = 0L;
        initialStateWarningLogged = false;
        initialStateCriticalLogged = false;
        initialStateFailureLogged = false;
        initialStateRequestCount = 0;
        lastDeltaResyncRequestMs = 0L;
        resetTargetingState();
        clearItemState();
        maybeSendInitialStateRequest(initialStateStartMs, "initial_enter");
    }

    /**
     * 濞撳懐鎮婇悩鑸碘偓?娴犮儰绌堕柅鈧崙鍝勫灥婵鎮撳銉уЦ閹?     */
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
     * 閼惧嘲褰囬崗銊╁櫤娣団剝浼呴獮鍫曟鐢箒骞忛崣鏍︾瑝閸掓壆娈戦崝鐐寸《
     */
    private void maybeRequestInitialStateResync() {
        //閸愬秴鍨介弬顓濈娑撳浼╅崗宥堫嚖閸?婵″倹鐏夋潻娆庨嚋娑撳秵妲搁柇锝呮皑閺勵垵顫楅懝鑼睏閻炲棙鐥呴崝鐘烘祰婵?
        if (hasReceivedInitialState) {
            return;
        }
        //閺冨爼妫块幋?
        long now = TimeUtils.millis();
        //婵″倹鐏夌€广垺鍩涚粩顖氬灥婵瀵插鈧慨瀣闂傜绻曢弰?鐏忚精骞忛崣鏍︾濞嗏€冲灥婵濮搁幀?
        if (initialStateStartMs == 0L) {
            resetInitialStateTracking();
            return;
        }
        //閸ュ搫鐣鹃梻鎾闁插秷鐦?
        if ((now - lastInitialStateRequestMs) >= INITIAL_STATE_REQUEST_INTERVAL_MS) {
            maybeSendInitialStateRequest(now, "retry_interval");
        }
        //閸掑棛楠囬柌宥堢槸,娴犲孩顒滅敮鎼佸櫢鐠囨洖鍩岄棁鈧憰浣瑰鐠€锕€鎲￠崚棰佸紬闁插秷顒熼崨?
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

    /**鐠囬攱鐪扮€广垺鍩涚粩顖氬晙娑撯偓濞嗏€冲絺鐠у嘲鍙忛柌蹇撴倱濮?鐠佹澘缍嶇拠閿嬬湴濞嗏剝鏆熸俊鍌涚亯濞嗏剝鏆熸潻鍥ь樋娑旂喍绱伴崣鏍ㄧХ闁插秷鐦?
     *
     */
    private void maybeSendInitialStateRequest(long timestampMs, String reason) {
        initialStateRequestCount++;
        if (game != null) {
            String tag = reason != null ? reason : "unknown";
            game.requestFullGameStateSync("GameScreen:" + tag + "#"+ initialStateRequestCount);
        }
        lastInitialStateRequestMs = timestampMs;
    }

    /**
     * 閸︺劍鐖堕幋蹇撳灥婵绁┃鎰梾閸旂姾娴囬幋鎰閻ㄥ嫭妞傞崐娆忓鏉炴垝绔存稉顏勫灥婵鏅棃?     */
    private void renderLoadingOverlay() {
        renderTextOverlay(getLoadingMessage());
    }

    private void renderReconnectOverlay() {
        renderTextOverlay("连接异常，正在重连...");
    }

    private void renderReconnectBanner() {
        if (batch == null || loadingFont == null || loadingLayout == null || camera == null) {
            return;
        }
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        String message = "连接异常，正在重连...";
        loadingLayout.setText(loadingFont, message);
        float centerX = camera.position.x;
        float centerY = camera.position.y + WORLD_HEIGHT * 0.25f;
        float x = centerX - loadingLayout.width / 2f;
        float y = centerY + loadingLayout.height / 2f;
        float alpha = 0.65f;
        if (reconnectHoldStartMs > 0L) {
            float phase = (TimeUtils.timeSinceMillis(reconnectHoldStartMs) % RECONNECT_OVERLAY_PULSE_MS)
                    / (float) RECONNECT_OVERLAY_PULSE_MS;
            alpha = MathUtils.lerp(0.3f, 1f, phase);
        }
        loadingFont.setColor(1f, 1f, 1f, alpha);
        loadingFont.draw(batch, loadingLayout, x, y);
        batch.end();
        loadingFont.setColor(Color.WHITE);
    }

    private void renderTextOverlay(String message) {
        Gdx.gl.glClearColor(0.06f, 0.06f, 0.08f, 1f);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);
        if (camera == null || batch == null || loadingFont == null || loadingLayout == null) {
            return;
        }
        camera.position.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f, 0f);
        camera.update();
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
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
     * 濞撳憡鍨欓崝鐘烘祰闂傤噣顣芥径鍕倞
     * @return
     */
    private String getLoadingMessage() {
        if (initialStateStartMs == 0L) {
            return "閸旂姾娴囨稉?..";
        }
        long waitMs = TimeUtils.millis() - initialStateStartMs;
        if (waitMs >= INITIAL_STATE_FAILURE_HINT_MS) {
            return "缁涘绶熼張宥呭閸ｃ劌鎮撳銉ㄧТ閺冭绱濈拠閿嬵梾閺屻儳缍夌紒婊嗙箾閹恒儻绱欏鑼额嚞濮?" + initialStateRequestCount + " 濞嗏槄绱?";
        }
        if (waitMs >= INITIAL_STATE_CRITICAL_MS) {
            return "缁涘绶熼張宥呭閸ｃ劌鎮撳銉ㄧТ閺冭绱濆锝呮躬闁插秷鐦?..閿涘牏顑?" + initialStateRequestCount + " 濞嗏槄绱?";
        }
        if (waitMs >= INITIAL_STATE_WARNING_MS) {
            return "濮濓絽婀拠閿嬬湴閺堝秴濮熼崳銊ユ倱濮?..閿涘牏顑?" + initialStateRequestCount + " 濞嗏槄绱?";
        }
        return "閸旂姾娴囨稉?..閿涘牏顑?" + Math.max(1, initialStateRequestCount) + " 濞喡ゎ嚞濮瑰偊绱?";
    }
}





