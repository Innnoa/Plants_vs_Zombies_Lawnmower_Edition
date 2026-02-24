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
import com.badlogic.gdx.math.Matrix4;
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
import com.lawnmower.ui.PlayerHudRenderer;
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
    // 瀵瑰簲 GameManager::SceneConfig.move_speed (200f) 鐜╁绉诲姩閫熷害
    private static final float PLAYER_SPEED = 200f;

    // 鏈€澶у懡浠ゆ寔缁椂闂?25ms (绾?40fps)锛屾渶灏?1/120s
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
    // 鐗╁搧绫诲瀷ID鍋忕Щ閲忥紝鐢ㄤ簬鍖哄垎閬撳叿鍜屾晫浜?type_id
    private static final int ITEM_TYPE_ID_OFFSET = 1;

    private static final int MAX_UNCONFIRMED_INPUTS = 240;
    private static final long MAX_UNCONFIRMED_INPUT_AGE_MS = 1500L;
    private static final long REMOTE_PLAYER_TIMEOUT_MS = 5000L;
    private static final long MIN_INPUT_SEND_INTERVAL_MS = 10L; // ~100Hz

    /*
     * 娑堟伅鎺╃爜瀹氫箟锛岀敤浜庡閲忓悓姝ユ椂鍒ゆ柇鍝簺瀛楁鍙戠敓浜嗗彉鍖?
     * 瀵瑰簲 protobuf 涓殑 PlayerDeltaMask, EnemyDeltaMask, ItemDeltaMask
     */
    private static final int PLAYER_DELTA_POSITION_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_POSITION_VALUE;
    private static final int PLAYER_DELTA_ROTATION_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_ROTATION_VALUE;
    private static final int PLAYER_DELTA_IS_ALIVE_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_IS_ALIVE_VALUE;
    private static final int PLAYER_DELTA_LAST_INPUT_MASK = Message.PlayerDeltaMask.PLAYER_DELTA_LAST_PROCESSED_INPUT_SEQ_VALUE;

    private static final int ENEMY_DELTA_POSITION_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_POSITION_VALUE;
    private static final int ENEMY_DELTA_HEALTH_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_HEALTH_VALUE;
    private static final int ENEMY_DELTA_IS_ALIVE_MASK = Message.EnemyDeltaMask.ENEMY_DELTA_IS_ALIVE_VALUE;

    private static final int ITEM_DELTA_POSITION_MASK = Message.ItemDeltaMask.ITEM_DELTA_POSITION_VALUE;
    private static final int ITEM_DELTA_IS_PICKED_MASK = Message.ItemDeltaMask.ITEM_DELTA_IS_PICKED_VALUE;
    private static final int ITEM_DELTA_TYPE_MASK = Message.ItemDeltaMask.ITEM_DELTA_TYPE_VALUE;

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
    private final Map<Integer, Message.ItemStateDelta> itemDeltaBuffer = new LinkedHashMap<>();
    private final Set<Integer> droppedItemDedupSet = new HashSet<>();
    private final Map<Integer, ItemView> itemViews = new HashMap<>();
    private final Map<Integer, TextureRegion> itemTextureRegions = new HashMap<>();
    private final Map<Integer, Texture> itemTextureHandles = new HashMap<>();
    private final Vector2 itemPositionBuffer = new Vector2();
    private long lastItemSnapshotTick = -1L;
    private long lastItemDeltaTick = -1L;
    private long lastItemSyncServerTimeMs = -1L;
    private long lastDroppedItemServerTimeMs = -1L;
    private long lastDroppedItemTick = -1L;

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
    private final Matrix4 hudMatrix = new Matrix4();
    private PlayerHudRenderer hud;
    private Message.PlayerState latestSelfState;

    private Vector2 predictedPosition = new Vector2();
    private float predictedRotation = 0f;
    private int inputSequence = 0;
    private boolean hasReceivedInitialState = false;
    private boolean awaitingFullWorldState = true;
    private String awaitingFullStateReason = "initial_state";
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
    private boolean serverPaused = false;
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
    // 30Hz 鍚屾闂撮殧骞虫粦鍊硷紝鐩爣绾?33ms
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
        hudMatrix.setToOrtho2D(0, 0, Gdx.graphics.getWidth(), Gdx.graphics.getHeight());
        batch = new SpriteBatch(); // 鍒濆鍖栫簿鐏垫壒澶勭悊
        loadingFont = new BitmapFont(); // 鍒濆鍖栧姞杞藉瓧浣?
        loadingFont.getData().setScale(1.3f); // 璁剧疆瀛椾綋澶у皬涓?1.3 鍊?
        loadingLayout = new GlyphLayout(); // 鍒濆鍖栨枃鏈竷灞€
        BitmapFont hudFont = null;
        Skin skin = game.getSkin();
        if (skin != null) {
            try {
                hudFont = skin.getFont("default_huipu");
            } catch (Exception ignored) { }
        }
        hud = new PlayerHudRenderer(hudFont);
        // 鍔犺浇鑳屾櫙
        try {
            backgroundTexture = new Texture(Gdx.files.internal("background/roomListBackground.png"));
        } catch (Exception e) {
            Pixmap bgPixmap = new Pixmap((int) WORLD_WIDTH, (int) WORLD_HEIGHT, Pixmap.Format.RGBA8888); // 鍒涘缓澶囩敤鑳屾櫙
            bgPixmap.setColor(0.05f, 0.15f, 0.05f, 1f);
            bgPixmap.fill();
            backgroundTexture = new Texture(bgPixmap);
            bgPixmap.dispose();
        }

        // 鍔犺浇鐜╁璧勬簮
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
        // 鍔犺浇鏁屼汉鍜屽瓙寮硅祫婧?
        loadEnemyAssets();
        loadProjectileAssets();
        enemyViews.clear();
        enemyLastSeen.clear();
        projectileViews.clear();
        projectileImpacts.clear();
        clearItemState();
        resetTargetingState();
        spawnPlaceholderEnemy();

        // 鍒濆鍖栫帺瀹朵綅缃?
        predictedPosition.set(WORLD_WIDTH / 2f, WORLD_HEIGHT / 2f);
        displayPosition.set(predictedPosition);
        resetInitialStateTracking();
        expectFullGameStateSync("show_initial_state");
        resetAutoAttackState();
        isSelfAlive = true;
        initUpgradeOverlay();
        resetUpgradeFlowState();
    }
    /**
     * 鍚屾鐗╁搧瑙嗗浘
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

    private void applyPartialItemStates(Collection<Message.ItemState> items, long incomingTick, long serverTimeMs) {
        if (items == null || items.isEmpty()) {
            return;
        }
        if (!shouldApplyItemDelta(incomingTick, serverTimeMs)) {
            return;
        }
        for (Message.ItemState itemState : items) {
            if (itemState == null) {
                continue;
            }
            applyItemState(itemState);
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
        view.update(region, itemPositionBuffer, (int) resolvedState.getTypeId());
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
            if (serverTimeMs > lastItemSyncServerTimeMs) {
                lastItemSyncServerTimeMs = serverTimeMs;
            }
            return true;
        }
        if (serverTimeMs > 0L && serverTimeMs <= lastItemSyncServerTimeMs) {
            return false;
        }
        lastItemSnapshotTick = -1L;
        lastItemDeltaTick = -1L;
        if (serverTimeMs > 0L) {
            lastItemSyncServerTimeMs = serverTimeMs;
        }
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
        if (serverTimeMs > 0L && serverTimeMs <= lastItemSyncServerTimeMs) {
            return false;
        }
        if (serverTimeMs > 0L) {
            lastItemSyncServerTimeMs = serverTimeMs;
        }
        return true;
    }

    private boolean shouldAcceptDroppedItems(long incomingTick, long serverTimeMs) {
        if (incomingTick >= 0L) {
            if (lastDroppedItemTick >= 0L && Long.compareUnsigned(incomingTick, lastDroppedItemTick) <= 0) {
                return false;
            }
            lastDroppedItemTick = incomingTick;
            if (serverTimeMs > lastDroppedItemServerTimeMs) {
                lastDroppedItemServerTimeMs = serverTimeMs;
            }
            return true;
        }
        if (serverTimeMs > 0L && lastDroppedItemServerTimeMs > 0L
                && serverTimeMs <= lastDroppedItemServerTimeMs) {
            return false;
        }
        if (serverTimeMs > 0L) {
            lastDroppedItemServerTimeMs = serverTimeMs;
        }
        return true;
    }

    private void applyItemDeltaStates(List<Message.ItemStateDelta> items, long incomingTick, long serverTimeMs) {
        if (items == null || items.isEmpty()) {
            return;
        }
        if (!shouldApplyItemDelta(incomingTick, serverTimeMs)) {
            return;
        }
        itemDeltaBuffer.clear();
        for (Message.ItemStateDelta itemDelta : items) {
            if (itemDelta == null) {
                continue;
            }
            int itemId = (int) itemDelta.getItemId();
            Message.ItemStateDelta existing = itemDeltaBuffer.get(itemId);
            if (existing == null) {
                itemDeltaBuffer.put(itemId, itemDelta);
            } else {
                itemDeltaBuffer.put(itemId, mergeItemDelta(existing, itemDelta));
            }
        }
        for (Message.ItemStateDelta dedupedState : itemDeltaBuffer.values()) {
            Message.ItemState resolved = materializeItemDelta(dedupedState);
            applyItemState(resolved);
        }
        itemDeltaBuffer.clear();
    }

    private Message.ItemStateDelta mergeItemDelta(Message.ItemStateDelta base, Message.ItemStateDelta incoming) {
        if (base == null) {
            return incoming;
        }
        Message.ItemStateDelta.Builder builder = base.toBuilder();
        builder.setChangedMask(base.getChangedMask() | incoming.getChangedMask());
        if (incoming.hasPosition()) {
            builder.setPosition(incoming.getPosition());
        }
        if (incoming.hasIsPicked()) {
            builder.setIsPicked(incoming.getIsPicked());
        }
        if (incoming.hasTypeId()) {
            builder.setTypeId(incoming.getTypeId());
        }
        return builder.build();
    }

    private Message.ItemState materializeItemDelta(Message.ItemStateDelta delta) {
        if (delta == null) {
            return null;
        }
        int itemId = (int) delta.getItemId();
        Message.ItemState base = itemStateCache.get(itemId);
        Message.ItemState.Builder builder = base != null
                ? base.toBuilder()
                : Message.ItemState.newBuilder().setItemId(delta.getItemId());
        int mask = delta.getChangedMask();
        if ((mask & ITEM_DELTA_POSITION_MASK) != 0 && delta.hasPosition()) {
            builder.setPosition(delta.getPosition());
        }
        if ((mask & ITEM_DELTA_IS_PICKED_MASK) != 0 && delta.hasIsPicked()) {
            builder.setIsPicked(delta.getIsPicked());
        }
        if ((mask & ITEM_DELTA_TYPE_MASK) != 0 && delta.hasTypeId()) {
            builder.setTypeId(delta.getTypeId());
        }
        return builder.build();
    }

    /**
     * 娓叉煋鐗╁搧
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
     * 涓绘覆鏌撳惊鐜?
     */
    @Override
    public void render(float delta) {
        advanceLogicalClock(delta); // 鎺ㄨ繘閫昏緫鏃堕挓
        if (!reconnectHoldActive && !serverPaused) {
            pumpPendingNetworkInput();
        }

        /*
        hasReceivedInitialState: 鏄惁宸叉敹鍒板垵濮嬬姸鎬?
        playerTextureRegion: 鐜╁绾圭悊鍖哄煙
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
         * 澶勭悊杈撳叆鍜屾洿鏂伴€昏緫
         */
        float renderDelta = getStableDelta(delta);
        Vector2 dir = getMovementInput();
        isLocallyMoving = dir.len2() > 0.0001f; // 鍒ゆ柇鏄惁鍦ㄧЩ鍔?
        boolean upgradeOverlayActive = isUpgradeOverlayActive();
        boolean suppressInput = upgradeOverlayActive || reconnectHoldActive || serverPaused;
        if (suppressInput) {
            dir.setZero();
            isLocallyMoving = false;
        }
        if (!reconnectHoldActive && !serverPaused && Gdx.input.isKeyJustPressed(AUTO_ATTACK_TOGGLE_KEY)) {
            autoAttackToggle = !autoAttackToggle;
            if (autoAttackToggle) {
                resetAutoAttackState();
            } else {
                autoAttackAccumulator = 0f;
                autoAttackHoldTimer = 0f;
            }
            showStatusToast(autoAttackToggle ? "鑷姩鏀诲嚮宸插紑鍚?" : "鑷姩鏀诲嚮宸插叧闂?");
        }
        boolean attacking = (reconnectHoldActive || serverPaused) ? false : resolveAttackingState(renderDelta);
        if (upgradeOverlayActive) {
            attacking = false;
        }

        /*
         * 妯℃嫙鏈湴姝ラ
         */
        simulateLocalStep(dir, renderDelta);
        if (reconnectHoldActive || serverPaused) {
            resetPendingInputAccumulator();
        } else {
            processInputChunk(dir, attacking, renderDelta);
        }

        /*
         * 娓呭睆
         */
        Gdx.gl.glClearColor(0, 0, 0, 1);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);

        clampPositionToMap(predictedPosition);
        updatePlayerFacing(renderDelta);
        updateDisplayPosition(renderDelta);
        clampPositionToMap(displayPosition);
        camera.position.set(displayPosition.x, displayPosition.y, 0);
        camera.update();

        /*
         * 寮€濮嬬粯鍒?
         */
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        batch.draw(backgroundTexture, 0, 0, WORLD_WIDTH, WORLD_HEIGHT);
        renderItems(renderDelta);
        /*
         * 鏇存柊鐜╁鍔ㄧ敾
         */
        playerAnimationTime += renderDelta;
        TextureRegion currentFrame = playerIdleAnimation != null
                ? playerIdleAnimation.getKeyFrame(playerAnimationTime, true) // 鑾峰彇褰撳墠甯?
                : playerTextureRegion;
        if (currentFrame == null) {
            batch.end();
            return;
        }
        playerTextureRegion = currentFrame;
        /*
         * 璁＄畻娓叉煋寤惰繜
         */
        long estimatedServerTimeMs = estimateServerTimeMs();
        long renderServerTimeMs = estimatedServerTimeMs - computeRenderDelayMs();
        updateProjectiles(renderDelta, renderServerTimeMs);
        /*
         * 娓叉煋鏁屼汉鍜岃繙绋嬬帺瀹?
         */
        renderEnemies(renderDelta, renderServerTimeMs);
        renderRemotePlayers(renderServerTimeMs, currentFrame, renderDelta);
        renderProjectiles();
        renderProjectileImpacts(renderDelta);
        /*
         * 缁樺埗鐜╁鑷韩
         */
        if (isSelfAlive) {
            drawCharacterFrame(currentFrame, displayPosition.x, displayPosition.y, facingRight);
        }
        renderStatusToast(renderDelta);

        batch.end();
        if (hud != null) {
            hud.update(renderDelta, latestSelfState, displayPosition, viewport, isSelfAlive);
            batch.setProjectionMatrix(hudMatrix);
            batch.begin();
            hud.render(batch);
            batch.end();
            batch.setProjectionMatrix(camera.combined);
        }
        renderUpgradeOverlay(renderDelta);
        if (reconnectHoldActive) {
            renderReconnectBanner();
        } else if (serverPaused) {
            renderServerPausedBanner();
        }
    }

    /**
     * 鎺ㄨ繘閫昏緫鏃堕挓
     * @param delta
     */
    private void advanceLogicalClock(float delta) {
        double total = delta * 1000.0 + logicalClockRemainderMs;
        long advance = (long) total;
        logicalClockRemainderMs = total - advance;
        logicalTimeMs += advance;
    }

    /**
     * 鑾峰彇绋冲畾鐨勫抚闂撮殧
     * @param rawDelta
     * @return
     */
    private float getStableDelta(float rawDelta) {
        float clamped = Math.min(rawDelta, MAX_FRAME_DELTA); // 闄愬埗鏈€澶у抚闂撮殧
        smoothedFrameDelta += (clamped - smoothedFrameDelta) * DELTA_SMOOTH_ALPHA; // 骞虫粦澶勭悊
        logFrameDeltaSpike(rawDelta, smoothedFrameDelta); // 璁板綍寮傚父 spike
        return smoothedFrameDelta;
    }

    /**
     * 妯℃嫙鏈湴绉诲姩
     * @param dir
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
     * 澶勭悊杈撳叆鍧?
     * @param dir
     * @param attacking
     * @param delta
     */
    private void processInputChunk(Vector2 dir, boolean attacking, float delta) {
        boolean moving = dir.len2() > 0.0001f; // 鍒ゆ柇鏄惁绉诲姩
        // 濡傛灉娌℃湁绉诲姩涔熸病鏈夋敾鍑?
        if (!moving && !attacking) {
            if (hasPendingInputChunk) {
                flushPendingInput();
            }
            if (!idleAckSent) {
                sendIdleCommand(delta);
            }
            return;
        }
        // 鏍囪闈炵┖闂?
        idleAckSent = false;
        // 濡傛灉娌℃湁寰呭鐞嗙殑杈撳叆鍧?
        if (!hasPendingInputChunk) {
            startPendingChunk(dir, attacking, delta);
            if (pendingAttack) {
                flushPendingInput();
            }
            return;
        }
        // 濡傛灉鏂瑰悜鐩稿悓涓旀敾鍑荤姸鎬佺浉鍚?
        if (pendingMoveDir.epsilonEquals(dir, 0.001f) && pendingAttack == attacking) {
            pendingInputDuration += delta; // 绱姞鎸佺画鏃堕棿
        } else {
            flushPendingInput(); // 鍒锋柊涔嬪墠鐨勮緭鍏?
            startPendingChunk(dir, attacking, delta); // 寮€濮嬫柊鐨勮緭鍏ュ潡
        }
        // 濡傛灉鏀诲嚮鎴栨寔缁椂闂磋揪鍒颁笂闄?
        if (pendingAttack || pendingInputDuration >= MAX_COMMAND_DURATION) {
            flushPendingInput();
        }
    }

    /**
     * 寮€濮嬫柊鐨勮緭鍏ュ潡
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
     * 鍒锋柊寰呭彂閫佺殑杈撳叆
     */
    private void flushPendingInput() {
        // 濡傛灉娌℃湁寰呭鐞嗚緭鍏ュ垯杩斿洖
        if (!hasPendingInputChunk) {
            return;
        }
        float duration = resolvePendingDurationSeconds();
        PlayerInputCommand cmd = new PlayerInputCommand(inputSequence++, pendingMoveDir, pendingAttack, duration);
        unconfirmedInputs.put(cmd.seq, cmd); // 瀛樺叆鏈‘璁よ緭鍏?
        inputSendTimes.put(cmd.seq, logicalTimeMs);
        pruneUnconfirmedInputs();
        sendPlayerInputToServer(cmd);
        resetPendingInputAccumulator();
    }

    /**
     * 鍙戦€佺┖闂插懡浠?
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
     * 淇壀杩囨湡鐨勬湭纭杈撳叆
     */
    private void pruneUnconfirmedInputs() {
        if (unconfirmedInputs.isEmpty()) {
            return;
        }

        /*
         * 妫€鏌ユ槸鍚﹁繃鏈熸垨婧㈠嚭
         */
        long now = logicalTimeMs;
        Iterator<Map.Entry<Integer, PlayerInputCommand>> iterator = unconfirmedInputs.entrySet().iterator();
        boolean removed = false;
        while (iterator.hasNext()) {
            Map.Entry<Integer, PlayerInputCommand> entry = iterator.next();
            int seq = entry.getKey();
            long sentAt = inputSendTimes.getOrDefault(seq, entry.getValue().timestampMs);
            /*
             * 鍒ゆ柇鏄惁澶棫鎴栨暟閲忚繃澶?
             */
            boolean tooOld = (now - sentAt) > MAX_UNCONFIRMED_INPUT_AGE_MS;
            boolean overflow = unconfirmedInputs.size() > MAX_UNCONFIRMED_INPUTS;
            if (tooOld || overflow) {
                iterator.remove();
                inputSendTimes.remove(seq);
                removed = true;
                continue;
            }
            // 濡傛灉娌℃湁杩囨湡涓旀病鏈夋孩鍑猴紝鍒欒烦鍑猴紙鍋囪鎸夋椂闂存帓搴忥級
            if (!tooOld && !overflow) {
                break;
            }
        }

        /*
         * 璁板綍鏃ュ織
         */
        if (removed) {
            Gdx.app.log(TAG, "Pruned stale inputs, remaining=" + unconfirmedInputs.size());
        }
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
    }

    /**
     * 娓叉煋杩滅▼鐜╁
     * @param renderServerTimeMs
     * @param frame
     * @param delta
     */
    private void renderRemotePlayers(long renderServerTimeMs, TextureRegion frame, float delta) {
        // 濡傛灉甯т负绌哄垯杩斿洖
        if (frame == null) {
            return;
        }
        // 閬嶅巻鎵€鏈夎繙绋嬬帺瀹跺揩鐓?
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
            // 鎻掑€艰绠椾綅缃?
            ServerPlayerSnapshot prev = null;
            ServerPlayerSnapshot next = null;
            for (ServerPlayerSnapshot snap : snapshots) {
                if (snap.serverTimestampMs <= renderServerTimeMs) {
                    prev = snap; // 鍓嶄竴涓揩鐓?
                } else {
                    next = snap; // 鍚庝竴涓揩鐓?
                    break;
                }
            }

            Vector2 targetPos = renderBuffer;
            /*
             * 鎻掑€兼垨澶栨帹浣嶇疆
             */
            // 濡傛灉鏈夊墠鍚庝袱涓揩鐓?
            if (prev != null && next != null && next.serverTimestampMs > prev.serverTimestampMs) {
                float t = (renderServerTimeMs - prev.serverTimestampMs) /
                        (float) (next.serverTimestampMs - prev.serverTimestampMs);
                t = MathUtils.clamp(t, 0f, 1f);
                targetPos.set(prev.position).lerp(next.position, t);
            } // 濡傛灉鍙湁鍓嶄竴涓揩鐓э紝杩涜澶栨帹
            else if (prev != null) {
                long ahead = Math.max(0L, renderServerTimeMs - prev.serverTimestampMs);
                long clampedAhead = Math.min(ahead, MAX_EXTRAPOLATION_MS);
                float seconds = clampedAhead / 1000f;
                targetPos.set(prev.position).mulAdd(prev.velocity, seconds);
            } // 濡傛灉鍙湁鍚庝竴涓揩鐓?
            else {
                targetPos.set(next.position);
            }
            clampPositionToMap(targetPos);
            boolean remoteFacing = remoteFacingRight.getOrDefault(playerId, true);
            // 鑾峰彇鎴栧垱寤烘樉绀轰綅缃?
            Vector2 displayPos = remoteDisplayPositions
                    .computeIfAbsent(playerId, id -> new Vector2(targetPos));
            // 骞虫粦绉诲姩鏄剧ず浣嶇疆
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
     * 娓叉煋瀛愬脊
     */
    private void renderProjectiles() {
        // 濡傛灉娌℃湁瀛愬脊鎴?batch 涓虹┖
        if (projectileViews.isEmpty() || batch == null) {
            logProjectileRenderState(projectileViews.isEmpty() ? "skip_empty" : "skip_batch_null");
            return;
        }
        // 閬嶅巻鎵€鏈夊瓙寮?
        for (ProjectileView view : projectileViews.values()) {
            TextureRegion frame = resolveProjectileFrame(view);
            // 濡傛灉甯т负绌?
            if (frame == null) {
                logProjectileRenderState("skip_frame_null");
                continue;
            }
            // 璁＄畻缁樺埗浣嶇疆
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
     * 娓叉煋瀛愬脊鍑讳腑鏁堟灉
     * @param delta
     */
    private void renderProjectileImpacts(float delta) {
        // 濡傛灉娌℃湁鍑讳腑鏁堟灉
        if (projectileImpacts.isEmpty() || batch == null) {
            return;
        }
        if (projectileImpactAnimation == null) {
            projectileImpacts.clear();
            return;
        }
        // 閬嶅巻鎵€鏈夊嚮涓晥鏋?
        for (int i = projectileImpacts.size - 1; i >= 0; i--) {
            ProjectileImpact impact = projectileImpacts.get(i);
            impact.elapsed += delta;
            // 鑾峰彇褰撳墠甯?
            TextureRegion frame = projectileImpactAnimation.getKeyFrame(impact.elapsed, false);
            if (frame == null || projectileImpactAnimation.isAnimationFinished(impact.elapsed)) {
                projectileImpacts.removeIndex(i);
                continue;
            }
            // 缁樺埗鏁堟灉
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
     * 瑙ｆ瀽瀛愬脊甯?
     */
    private TextureRegion resolveProjectileFrame(ProjectileView view) {
        if (projectileAnimation != null) {
            return projectileAnimation.getKeyFrame(view.animationTime, true);
        }
        return projectileFallbackRegion;
    }

    /**
     * 鍒ゆ柇瀛愬脊鏄惁鍑虹晫
     * @param position
     * @return
     */
    private boolean isProjectileOutOfBounds(Vector2 position) {
        float margin = 32f;
        return position.x < -margin || position.x > WORLD_WIDTH + margin
                || position.y < -margin || position.y > WORLD_HEIGHT + margin;
    }

    /**
     * 鐢熸垚鍑讳腑鏁堟灉
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
     * 娓叉煋鐘舵€佹彁绀?Toast
     * @param delta
     */
    private void renderStatusToast(float delta) {
        // 鍑忓皯璁℃椂鍣?
        if (statusToastTimer > 0f) {
            statusToastTimer -= delta;
        }
        // 濡傛灉鏃堕棿鍒版垨瀛椾綋涓虹┖
        if (statusToastTimer <= 0f || loadingFont == null || statusToastMessage == null
                || statusToastMessage.isBlank() || batch == null) {
            return;
        }
        // 璁剧疆棰滆壊
        loadingFont.setColor(Color.WHITE);
        // 璁＄畻缁樺埗浣嶇疆
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

        upgradeRefreshButton = new TextButton("鍒锋柊", skin,"CreateButton");
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
            upgradeRefreshButton.setText("鍒锋柊 (鍓╀綑 " + upgradeSession.refreshRemaining + ")");
        }
        populateUpgradeCards();
    }

    private void populateUpgradeCards() {
        if (upgradeCards == null || upgradeCards.isEmpty()) {
            return;
        }
        if (upgradeSession == null || upgradeFlowState == UpgradeFlowState.WAITING_OPTIONS) {
            for (UpgradeCard card : upgradeCards) {
                card.showPlaceholder("绛夊緟鍗＄墝...");
            }
            return;
        }
        List<Message.UpgradeOption> options = upgradeSession.options;
        for (int i = 0; i < upgradeCards.size; i++) {
            UpgradeCard card = upgradeCards.get(i);
            if (options == null || i >= options.size()) {
                card.showPlaceholder("鏆傛棤閫夐」");
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
            String footer = confirming ? "绛夊緟纭..." : (selectable ? "鐐瑰嚮閫夋嫨" : "");
            card.bindOption(option, title, description, footer, drawable, selectable, confirming);
        }
    }

    private String buildUpgradeTitle() {
        if (upgradeSession == null) {
            return "鍗囩骇閫夋嫨";
        }
        return formatUpgradeReason(upgradeSession.reason);
    }

    private String buildUpgradeHint() {
        return switch (upgradeFlowState) {
            case WAITING_OPTIONS -> "姝ｅ湪鐢熸垚鍗＄墝锛岃绋嶅€?..";
            case CONFIRMING -> "宸叉彁浜ら€夋嫨锛岀瓑寰呮湇鍔″櫒纭...";
            case CHOOSING -> upgradeSession != null
                    ? "鍓╀綑鍒锋柊娆℃暟锛?" + upgradeSession.refreshRemaining
                    : "璇烽€夋嫨涓€椤瑰崌绾?";
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
            return "鏈煡鍗囩骇";
        }
        Message.UpgradeEffect effect = option.getEffects(0);
        return formatUpgradeTypeName(effect.getType()) + " 路 " + formatUpgradeLevelName(effect.getLevel());
    }

    private String formatEffects(Message.UpgradeOption option) {
        if (option == null || option.getEffectsCount() == 0) {
            return "鏆傛棤鏁堟灉鎻忚堪";
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
            case UPGRADE_TYPE_MOVE_SPEED -> "绉诲姩閫熷害";
            case UPGRADE_TYPE_ATTACK -> "鏀诲嚮鍔?";
            case UPGRADE_TYPE_ATTACK_SPEED -> "鏀诲嚮閫熷害";
            case UPGRADE_TYPE_MAX_HEALTH -> "鐢熷懡涓婇檺";
            case UPGRADE_TYPE_CRITICAL_RATE -> "鏆村嚮鐜?";
            default -> "灞炴€у己鍖?";
        };
    }

    private String formatUpgradeLevelName(Message.UpgradeLevel level) {
        return switch (level) {
            case UPGRADE_LEVEL_LOW -> "浣庣骇";
            case UPGRADE_LEVEL_MEDIUM -> "涓骇";
            case UPGRADE_LEVEL_HIGH -> "楂樼骇";
            default -> "鏈煡";
        };
    }

    private String formatUpgradeReason(Message.UpgradeReason reason) {
        return switch (reason) {
            case UPGRADE_REASON_LEVEL_UP -> "鍗囩骇濂栧姳";
            case UPGRADE_REASON_REFRESH -> "鍒锋柊缁撴灉";
            default -> "闅忔満濂栧姳";
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
            showStatusToast("鍒锋柊璇锋眰鍙戦€佸け璐ワ紝缃戠粶寮傚父");
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
            showStatusToast("鍙戦€佸崌绾ч€夋嫨澶辫触");
            upgradeFlowState = UpgradeFlowState.CHOOSING;
            pendingUpgradeOptionIndex = -1;
            updateUpgradeOverlayContent();
        } else {
            showStatusToast("宸叉彁浜ゅ崌绾ч€夋嫨");
        }
    }

    private boolean canRefreshOptions() {
        return upgradeSession != null
                && upgradeSession.refreshRemaining > 0
                && upgradeFlowState == UpgradeFlowState.CHOOSING;
    }

    /**
     * 鏄剧ず鐘舵€佹彁绀?
     * @param message
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
     * 鍗囩骇娴佺▼鐘舵€佹灇涓?
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
            footerLabel.setText(confirming ? "绛夊緟纭..." : defaultFooter);
            setTouchable(this.selectable ? Touchable.enabled : Touchable.disabled);
            setColor(this.selectable ? Color.WHITE : Color.GRAY);
        }

        void showPlaceholder(String message) {
            option = null;
            selectable = false;
            setTouchable(Touchable.disabled);
            setBackground((Drawable) null);
            setColor(Color.DARK_GRAY);
            titleLabel.setText("绛夊緟");
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

        ItemView(int itemId) {
            this.itemId = itemId;
        }

        void update(TextureRegion textureRegion, Vector2 sourcePosition, int typeId) {
            if (textureRegion != null) {
                this.region = textureRegion;
            }
            this.position.set(sourcePosition);
            this.typeId = typeId;
        }
    }

    /**
     * 瀛愬脊瑙嗗浘鍐呴儴绫?
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
     * 瀛愬脊鍑讳腑鏁堟灉鍐呴儴绫?
     */
    private static final class ProjectileImpact {
        final Vector2 position = new Vector2();
        float elapsed;
    }

    /**
     * 鐢熸垚鍗犱綅绗︽晫浜?
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
     * 绉婚櫎鍗犱綅绗︽晫浜?
     */
    private void removePlaceholderEnemy() {
        enemyViews.remove(PLACEHOLDER_ENEMY_ID);
        enemyLastSeen.remove(PLACEHOLDER_ENEMY_ID);
    }

    /**
     * 绉婚櫎鏁屼汉
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
     * 纭繚鏁屼汉瑙嗗浘瀛樺湪
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
     * 鍔犺浇鏁屼汉璧勬簮
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
     * 鍒涘缓鏁屼汉鍔ㄧ敾
     * @param atlasPath
     * @param regionPrefix
     * @param frameDuration
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
     * 瑙ｆ瀽鏁屼汉琛岃蛋鍔ㄧ敾
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
     * 鑾峰彇鏁屼汉澶囩敤绾圭悊
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
     * 閲婃斁鏁屼汉鍥鹃泦璧勬簮
     */
    private void disposeEnemyAtlases() {
        for (TextureAtlas atlas : enemyAtlasCache.values()) {
            atlas.dispose();
        }
        enemyAtlasCache.clear();
    }

    /**
     * 鍔犺浇瀛愬脊璧勬簮
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
     * 缁樺埗瑙掕壊甯?
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
     * 璁＄畻娓叉煋寤惰繜
     * @return
     */
    private long computeRenderDelayMs() {
        // 寤惰繜鍒嗛噺璁＄畻
        float latencyComponent = smoothedRttMs * 0.25f + 12f;
        if (Float.isNaN(latencyComponent) || Float.isInfinite(latencyComponent)) {
            latencyComponent = 90f;
        }
        latencyComponent = MathUtils.clamp(latencyComponent, 45f, 150f);
        // 鎶栧姩鍌ㄥ璁＄畻
        float jitterReserve = Math.abs(smoothedSyncIntervalMs - 33f) * 0.5f
                + (smoothedSyncDeviationMs * 1.3f) + 18f;
        jitterReserve = MathUtils.clamp(jitterReserve, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        // 鐩爣寤惰繜
        float target = Math.max(latencyComponent, jitterReserve);
        target = MathUtils.clamp(target, INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        // 骞虫粦杩囨浮
        float delta = target - renderDelayMs;
        delta = MathUtils.clamp(delta, -MAX_RENDER_DELAY_STEP_MS, MAX_RENDER_DELAY_STEP_MS);
        renderDelayMs = MathUtils.clamp(renderDelayMs + delta * RENDER_DELAY_LERP,
                INTERP_DELAY_MIN_MS, INTERP_DELAY_MAX_MS);
        return Math.round(renderDelayMs);
    }

    /**
     * 璁板綍鍚屾闂撮殧灏栧嘲
     * @param intervalMs
     * @param smoothInterval
     */
    private void logSyncIntervalSpike(float intervalMs, float smoothInterval) {
        // 濡傛灉闂撮殧姝ｅ父鍒欒繑鍥?
        if (intervalMs < SYNC_INTERVAL_LOG_THRESHOLD_MS) {
            return;
        }
        long nowMs = TimeUtils.millis();
        if (nowMs - lastSyncLogMs < SYNC_INTERVAL_LOG_INTERVAL_MS) {
            return;
        }
        // 璁板綍鏃ュ織
        Gdx.app.log(TAG, "Sync interval spike=" + intervalMs + "ms smooth=" + smoothInterval
                + "ms renderDelay=" + renderDelayMs);
        lastSyncLogMs = nowMs;
    }

    /**
     * 鍒ゆ柇鏄惁鎺ュ彈鐘舵€佸寘
     * @param serverTimeMs
     * @param arrivalMs
     * @return
     */
    private boolean shouldAcceptStatePacket(long incomingTick, long serverTimeMs, long arrivalMs) {
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
        if (serverTimeMs > 0L && lastAppliedServerTimeMs >= 0L && serverTimeMs <= lastAppliedServerTimeMs) {
            logDroppedSync("serverTime", serverTimeMs, serverTimeMs, arrivalMs);
            return false;
        }
        if (serverTimeMs > 0L) {
            lastAppliedServerTimeMs = serverTimeMs;
        }
        return true;
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

    private long resolveServerTime(Message.Timestamp syncTime, long arrivalMs) {
        long serverTimeMs = extractServerTime(syncTime);
        if (serverTimeMs > 0L) {
            return serverTimeMs;
        }
        long estimate = estimateServerTimeMs();
        if (estimate > 0L) {
            return estimate;
        }
        return arrivalMs;
    }

    /**
     * 璁板綍涓㈠純鐨勫悓姝ュ寘
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
     * 鏇存柊鍚屾鍒拌揪缁熻
     * @param arrivalMs
     */
    private void updateSyncArrivalStats(long arrivalMs) {
        // 濡傛灉宸叉湁涓婃璁板綍
        if (lastSyncArrivalMs != 0L) {
            // 璁＄畻闂撮殧
            float interval = arrivalMs - lastSyncArrivalMs;
            // 骞虫粦闂撮殧
            smoothedSyncIntervalMs += (interval - smoothedSyncIntervalMs) * SYNC_INTERVAL_SMOOTH_ALPHA;
            // 璁＄畻鍋忓樊
            float deviation = Math.abs(interval - smoothedSyncIntervalMs);
            smoothedSyncDeviationMs += (deviation - smoothedSyncDeviationMs) * SYNC_DEVIATION_SMOOTH_ALPHA;
            // 闄愬埗鍋忓樊鑼冨洿
            smoothedSyncDeviationMs = MathUtils.clamp(smoothedSyncDeviationMs, 0f, 220f);
            // 璁板綍灏栧嘲
            logSyncIntervalSpike(interval, smoothedSyncIntervalMs);
        }
        // 鏇存柊涓婃鍒拌揪鏃堕棿
        lastSyncArrivalMs = arrivalMs;
    }

    /**
     * 閲囨牱鏃堕挓鍋忕Щ
     * @param serverTimeMs
     */
    //TODO: 浼樺寲鏃堕挓鍚屾绠楁硶
    private void sampleClockOffset(long serverTimeMs) {
        long arrivalLocalTimeMs = getMonotonicTimeMs();
        double offsetSample = serverTimeMs - arrivalLocalTimeMs;
        clockOffsetMs += (offsetSample - clockOffsetMs) * 0.1;
    }
    /**
     * 浼扮畻鏈嶅姟鍣ㄦ椂闂?
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
     * 闄愬埗浣嶇疆鍦ㄥ湴鍥捐寖鍥村唴
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
     * 鑾峰彇绉诲姩杈撳叆
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
     * 鏈湴搴旂敤杈撳叆
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
     * 鍙戦€佺帺瀹惰緭鍏ュ埌鏈嶅姟鍣?
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
     * 鎺掗槦绛夊緟鍙戦€佽緭鍏?
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
     * 绔嬪嵆鍙戦€佽緭鍏?
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
     * 澶勭悊寰呭彂閫佺殑缃戠粶杈撳叆
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
     * 鎺ユ敹娓告垙鐘舵€佸悓姝?
     * @param sync
     */
    public void onGameStateReceived(Message.S2C_GameStateSync sync) {
        long arrivalMs = TimeUtils.millis();
        setCurrentRoomId((int) sync.getRoomId());
        //
        Message.Timestamp syncTime = sync.hasSyncTime() ? sync.getSyncTime() : null;
        long incomingTick = extractSyncTick(syncTime);
        long serverTimeMs = resolveServerTime(syncTime, arrivalMs);
        if (!shouldAcceptStatePacket(incomingTick, serverTimeMs, arrivalMs)) {
            return;
        }
        if (incomingTick >= 0L) {
            game.updateServerTick(incomingTick);
        }
        boolean isFullSnapshot = shouldTreatSyncAsFullSnapshot(sync.getIsFullSnapshot());
        if (isFullSnapshot && Gdx.app != null) {
            String snapshotReason = sync.getIsFullSnapshot() ? "server_full" : awaitingFullStateReason;
            Gdx.app.log(TAG, "Applying full GameStateSync (" + snapshotReason + ")");
        }
        updateSyncArrivalStats(arrivalMs);
        if (serverTimeMs > 0L) {
            sampleClockOffset(serverTimeMs);
        }
        List<Message.EnemyState> enemies = sync.getEnemiesList();
        if (isFullSnapshot) {
            enemyStateCache.clear();
            if (!enemies.isEmpty()) {
                for (Message.EnemyState enemy : enemies) {
                    enemyStateCache.put((int) enemy.getEnemyId(), enemy);
                }
            }
            syncEnemyViews(enemies, serverTimeMs, true);
        } else if (!enemies.isEmpty()) {
            for (Message.EnemyState enemy : enemies) {
                enemyStateCache.put((int) enemy.getEnemyId(), enemy);
            }
            syncEnemyViews(enemies, serverTimeMs, false);
        }
        handlePlayersFromServer(sync.getPlayersList(), serverTimeMs);
        if (isFullSnapshot && shouldApplyItemSnapshot(incomingTick, serverTimeMs)) {
            syncItemViews(sync.getItemsList());
        } else {
            applyPartialItemStates(sync.getItemsList(), incomingTick, serverTimeMs);
        }
        if (game.isAwaitingReconnectSnapshot()) {
            game.onReconnectSnapshotApplied();
        }
    }

    /**
     * 鎺ユ敹娓告垙鐘舵€佸閲忓悓姝?
     * @param delta
     */
    public void onGameStateDeltaReceived(Message.S2C_GameStateDeltaSync delta) {
        //
        long arrivalMs = TimeUtils.millis();
        setCurrentRoomId((int) delta.getRoomId());
        Message.Timestamp syncTime = delta.hasSyncTime() ? delta.getSyncTime() : null;
        long deltaTick = extractSyncTick(syncTime);
        long serverTimeMs = resolveServerTime(syncTime, arrivalMs);
        //
        if (!shouldAcceptStatePacket(deltaTick, serverTimeMs, arrivalMs)) {
            return;
        }
        //
        List<Message.PlayerState> mergedPlayers = new ArrayList<>(delta.getPlayersCount());
        for (Message.PlayerStateDelta playerDelta : delta.getPlayersList()) {
            Message.PlayerState merged = mergePlayerDelta(playerDelta);
            if (merged != null) {
                mergedPlayers.add(merged);
            }
        }
        //
        List<Message.EnemyState> updatedEnemies = new ArrayList<>(delta.getEnemiesCount());
        for (Message.EnemyStateDelta enemyDelta : delta.getEnemiesList()) {
            Message.EnemyState mergedEnemy = mergeEnemyDelta(enemyDelta);
            if (mergedEnemy != null) {
                updatedEnemies.add(mergedEnemy);
            }
        }
        //
        updateSyncArrivalStats(arrivalMs);
        if (serverTimeMs > 0L) {
            sampleClockOffset(serverTimeMs);
        }
        //
        handlePlayersFromServer(mergedPlayers, serverTimeMs);
        if (!updatedEnemies.isEmpty()) {
            syncEnemyViews(updatedEnemies, serverTimeMs, false);
        }
        if (!delta.getItemsList().isEmpty()) {
            applyItemDeltaStates(delta.getItemsList(), deltaTick, serverTimeMs);
        }
        if (deltaTick >= 0L) {
            game.updateServerTick(deltaTick);
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

    public void setServerPaused(boolean paused) {
        if (this.serverPaused == paused) {
            return;
        }
        this.serverPaused = paused;
        if (serverPaused) {
            pendingRateLimitedInput = null;
            resetPendingInputAccumulator();
        }
    }

    public void resetWorldStateForFullSync(String reason) {
        String tag = (reason == null || reason.isBlank()) ? "world_reset" : reason;
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
        expectFullGameStateSync(tag);
        hasReceivedInitialState = false;
    }

    public void expectFullGameStateSync(String reason) {
        awaitingFullWorldState = true;
        awaitingFullStateReason = (reason == null || reason.isBlank()) ? "unknown" : reason;
    }

    private boolean shouldTreatSyncAsFullSnapshot(boolean serverRequestsFullSnapshot) {
        if (serverRequestsFullSnapshot) {
            awaitingFullWorldState = false;
            return true;
        }
        if (!hasReceivedInitialState) {
            awaitingFullWorldState = false;
            return true;
        }
        if (awaitingFullWorldState) {
            awaitingFullWorldState = false;
            return true;
        }
        return false;
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

    private boolean isMessageForCurrentRoom(long roomId) {
        return roomId <= 0 || currentRoomId <= 0 || roomId == currentRoomId;
    }

    public void onUpgradeRequest(Message.S2C_UpgradeRequest request) {
        if (request == null) {
            return;
        }
        setCurrentRoomId((int) request.getRoomId());
        if (request.getPlayerId() != game.getPlayerId()) {
            showStatusToast("鐜╁" + request.getPlayerId() + " 姝ｅ湪閫夋嫨鍗囩骇");
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
            showStatusToast("纭鍗囩骇璇锋眰澶辫触锛岀綉缁滃紓甯?");
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
            showStatusToast("纭鍗囩骇閫夐」澶辫触锛岀綉缁滃紓甯?");
        }
    }

    public void onUpgradeSelectAck(Message.S2C_UpgradeSelectAck ack) {
        if (ack == null || ack.getPlayerId() != game.getPlayerId()) {
            return;
        }
        showStatusToast("鍗囩骇瀹屾垚锛岀户缁垬鏂楋紒");
        exitUpgradeMode();
        resetUpgradeFlowState();
    }

    /**
     * 澶勭悊鏉ヨ嚜鏈嶅姟鍣ㄧ殑鐜╁鍒楄〃
     * @param players
     * @param serverTimeMs
     */
    private void handlePlayersFromServer(Collection<Message.PlayerState> players, long serverTimeMs) {
        // 鑾峰彇鎴戠殑 ID
        int myId = game.getPlayerId();
        Message.PlayerState selfStateFromServer = null;
        // 閬嶅巻鐜╁鍒楄〃
        if (players != null) {
            for (Message.PlayerState player : players) {
                int playerId = (int) player.getPlayerId();
                serverPlayerStates.put(playerId, player);
                // 濡傛灉鏄嚜宸?
                if (playerId == myId) {
                    isSelfAlive = player.getIsAlive();
                    game.updateConfirmedInputSeq(player.getLastProcessedInputSeq());
                    if (isSelfAlive) {
                        selfStateFromServer = player;
                        latestSelfState = player;
                    }
                    continue;
                }
                if (!player.getIsAlive()) {
                    removeRemotePlayerData(playerId);
                    continue;
                }
                // 鏇存柊杩滅▼鐜╁浣嶇疆
                if (player.hasPosition()) {
                    Vector2 position = new Vector2(player.getPosition().getX(), player.getPosition().getY());
                    pushRemoteSnapshot(playerId, position, player.getRotation(), serverTimeMs);
                    remotePlayerLastSeen.put(playerId, serverTimeMs);
                }
            }
        }
        // 娓呯悊杩囨湡杩滅▼鐜╁
        purgeStaleRemotePlayers(serverTimeMs);
        // 搴旂敤鑷韩鐘舵€?
        if (selfStateFromServer != null) {
            applySelfStateFromServer(selfStateFromServer);
        }
    }

    /**
     * 鍚屾鏁屼汉瑙嗗浘
     * @param enemies
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
     * 搴旂敤鏉ヨ嚜鏈嶅姟鍣ㄧ殑鑷韩鐘舵€?
     * @param selfStateFromServer
     */
    private void applySelfStateFromServer(Message.PlayerState selfStateFromServer) {
        // 鑾峰彇鏈嶅姟鍣ㄤ綅缃?
        Vector2 serverPos = new Vector2(
                selfStateFromServer.getPosition().getX(),
                selfStateFromServer.getPosition().getY()
        );
        clampPositionToMap(serverPos);
        // 鏇存柊宸茬‘璁よ緭鍏ュ簭鍒?
        int lastProcessedSeq = selfStateFromServer.getLastProcessedInputSeq();
        game.updateConfirmedInputSeq(lastProcessedSeq);
        // 鍒涘缓蹇収
        PlayerStateSnapshot snapshot = new PlayerStateSnapshot(
                serverPos,
                selfStateFromServer.getRotation(),
                lastProcessedSeq
        );
        latestSelfState = selfStateFromServer;
        snapshotHistory.offer(snapshot);
        while (snapshotHistory.size() > 10) {
            snapshotHistory.poll();
        }
        // 涓庢湇鍔″櫒鐘舵€佸崗璋?
        reconcileWithServer(snapshot);
    }

    /**
     * 鎺ㄩ€佽繙绋嬬帺瀹跺揩鐓?
     * @param playerId
     * @param position
     * @param rotation
     * @param serverTimeMs
     */
    private void pushRemoteSnapshot(int playerId, Vector2 position, float rotation, long serverTimeMs) {
        clampPositionToMap(position);
        // 鑾峰彇闃熷垪
        Deque<ServerPlayerSnapshot> queue = remotePlayerServerSnapshots
                .computeIfAbsent(playerId, k -> new ArrayDeque<>());
        // 璁＄畻閫熷害
        Vector2 velocity = new Vector2();
        ServerPlayerSnapshot previous = queue.peekLast();
        if (previous != null) {
            long deltaMs = serverTimeMs - previous.serverTimestampMs;
            if (deltaMs > 0) {
                velocity.set(position).sub(previous.position).scl(1000f / deltaMs);
            } else {
                velocity.set(previous.velocity);
            }
            // 闄愬埗鏈€澶ч€熷害
            float maxSpeed = PLAYER_SPEED * 1.5f;
            if (velocity.len2() > maxSpeed * maxSpeed) {
                velocity.clamp(0f, maxSpeed);
            }
        }
        // 鏇存柊鏈濆悜
        updateRemoteFacing(playerId, velocity, rotation);
        // 娣诲姞蹇収
        ServerPlayerSnapshot snap = new ServerPlayerSnapshot(position, rotation, velocity, serverTimeMs);
        queue.addLast(snap);
        // 绉婚櫎杩囨湡蹇収
        while (queue.size() > 1 &&
                (serverTimeMs - queue.peekFirst().serverTimestampMs) > SNAPSHOT_RETENTION_MS) {
            queue.removeFirst();
        }
    }

    /**
     * 鏇存柊杩滅▼鐜╁鏈濆悜
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

    private void removeRemotePlayerData(int playerId) {
        remotePlayerServerSnapshots.remove(playerId);
        remotePlayerLastSeen.remove(playerId);
        remoteFacingRight.remove(playerId);
        remoteDisplayPositions.remove(playerId);
    }

    /**
     * 娓呴櫎杩囨湡杩滅▼鐜╁
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
                removeRemotePlayerData(playerId);
                serverPlayerStates.remove(playerId);
            }
        }
    }

    /**
     * 浠庢棆杞帹鏂湞鍚?
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
     * 涓庢湇鍔″櫒鐘舵€佸崗璋?
     * @param serverSnapshot
     */
    private void reconcileWithServer(PlayerStateSnapshot serverSnapshot) {
        // 璁＄畻 RTT
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
        // 淇浣嶇疆
        float correctionDist = predictedPosition.dst(serverSnapshot.position);
        boolean wasInitialized = hasReceivedInitialState;
        predictedPosition.set(serverSnapshot.position);
        predictedRotation = serverSnapshot.rotation;
        facingRight = inferFacingFromRotation(predictedRotation);
        clampPositionToMap(predictedPosition);
        logServerCorrection(correctionDist, serverSnapshot.lastProcessedInputSeq);
        if (!wasInitialized) {
            clearInitialStateWait();
            displayPosition.set(predictedPosition);
        }
        // 閲嶆斁鏈‘璁よ緭鍏?
        for (PlayerInputCommand input : unconfirmedInputs.values()) {
            if (input.seq > serverSnapshot.lastProcessedInputSeq) {
                applyInputLocally(predictedPosition, predictedRotation, input, input.deltaSeconds);
            }
        }
        // 绉婚櫎宸茬‘璁よ緭鍏?
        unconfirmedInputs.entrySet().removeIf(entry -> {
            boolean applied = entry.getKey() <= serverSnapshot.lastProcessedInputSeq;
            if (applied) {
                inputSendTimes.remove(entry.getKey());
            }
            return applied;
        });
        pruneUnconfirmedInputs();
        // 鏍囪宸插垵濮嬪寲
        hasReceivedInitialState = true;
        if (unconfirmedInputs.isEmpty()) {
            idleAckSent = true;
        }
        // 骞虫粦鏄剧ず浣嶇疆
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= (DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE * 4f)) {
            displayPosition.set(predictedPosition);
        } else {
            displayPosition.lerp(predictedPosition, 0.35f);
        }
    }

    /**
     * 鍚堝苟鐜╁澧為噺
     * @param delta
     * @return
     */
    private Message.PlayerState mergePlayerDelta(Message.PlayerStateDelta delta) {
        // 濡傛灉涓虹┖
        if (delta == null) {
            return null;
        }
        int playerId = (int) delta.getPlayerId();
        Message.PlayerState base = serverPlayerStates.get(playerId);
        // 濡傛灉鍩虹鐘舵€佷笉瀛樺湪
        if (base == null) {
            requestDeltaResync("player_" + playerId);
            return null;
        }
        // 鏋勫缓鏂扮姸鎬?
        Message.PlayerState.Builder builder = base.toBuilder();
        // 搴旂敤鍙樺寲
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
            int confirmedSeq = delta.getLastProcessedInputSeq();
            builder.setLastProcessedInputSeq(confirmedSeq);
            if (playerId == game.getPlayerId()) {
                game.updateConfirmedInputSeq(confirmedSeq);
            }
        }
        // 鏇存柊缂撳瓨
        Message.PlayerState updated = builder.build();
        serverPlayerStates.put(playerId, updated);
        return updated;
    }

    /**
     * 鍚堝苟鏁屼汉澧為噺
     * @param delta
     * @return
     */
    private Message.EnemyState mergeEnemyDelta(Message.EnemyStateDelta delta) {
        // 濡傛灉涓虹┖
        if (delta == null) {
            return null;
        }
        int enemyId = (int) delta.getEnemyId();
        Message.EnemyState base = enemyStateCache.get(enemyId);
        // 濡傛灉鍩虹鐘舵€佷笉瀛樺湪
        if (base == null) {
            requestDeltaResync("enemy_" + enemyId);
            return null;
        }
        // 鏋勫缓鏂扮姸鎬?
        Message.EnemyState.Builder builder = base.toBuilder();
        // 搴旂敤鍙樺寲
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
        // 鏇存柊缂撳瓨
        Message.EnemyState updated = builder.build();
        enemyStateCache.put(enemyId, updated);
        return updated;
    }

    /**
     * 璇锋眰澧為噺閲嶅悓姝?
     * @param reason
     */
    private void requestDeltaResync(String reason) {
        // 濡傛灉 game 涓虹┖
        if (game == null) {
            return;
        }
        long now = TimeUtils.millis();
        if ((now - lastDeltaResyncRequestMs) < DELTA_RESYNC_COOLDOWN_MS) {
            return;
        }
        // 鍙戦€佽姹?
        lastDeltaResyncRequestMs = now;
        String deltaTag = (reason == null || reason.isBlank()) ? "unknown" : reason;
        String requestTag = "delta:" + deltaTag;
        expectFullGameStateSync(requestTag);
        game.requestFullGameStateSync(requestTag);
    }

    private void handleProjectileSpawnEvent(Message.S2C_ProjectileSpawn spawn) {
        if (spawn == null || spawn.getProjectilesCount() == 0) {
            return;
        }
        if (!isMessageForCurrentRoom(spawn.getRoomId())) {
            return;
        }
        long arrivalMs = TimeUtils.millis();
        Message.Timestamp syncTime = spawn.hasSyncTime() ? spawn.getSyncTime() : null;
        long serverTimeMs = resolveServerTime(syncTime, arrivalMs);
        if (serverTimeMs <= 0L) {
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
        if (!isMessageForCurrentRoom(despawn.getRoomId())) {
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
        int prevHealth = base != null ? base.getHealth() : hurt.getRemainingHealth();
        int maxHealth = base != null ? base.getMaxHealth() : hurt.getRemainingHealth();
        if (base != null) {
            Message.PlayerState.Builder builder = base.toBuilder();
            builder.setHealth(hurt.getRemainingHealth());
            Message.PlayerState updated = builder.build();
            serverPlayerStates.put(playerId, updated);
            if (playerId == game.getPlayerId()) {
                latestSelfState = updated;
            }
        }
        String toast = playerId == game.getPlayerId()
                ? "鍙楀埌浼ゅ " + hurt.getDamage() + " 鐐癸紝鍓╀綑鐢熷懡锛?" + hurt.getRemainingHealth()
                : "鐜╁ " + playerId + " 鍙楀埌浼ゅ " + hurt.getDamage() + " 鐐?";
        showStatusToast(toast);
        if (hud != null && playerId == game.getPlayerId()) {
            float lost = Math.max(0, prevHealth - hurt.getRemainingHealth());
            hud.onDamage(lost, Math.max(1f, maxHealth), displayPosition, viewport);
        }
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
        if (!isMessageForCurrentRoom(sync.getRoomId())) {
            return;
        }
        long arrivalMs = TimeUtils.millis();
        Message.Timestamp syncTime = sync.hasSyncTime() ? sync.getSyncTime() : null;
        long serverTimeMs = resolveServerTime(syncTime, arrivalMs);
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
                ? "鐜╁ " + died.getKillerPlayerId() + " 鍑昏触浜嗘晫浜?"
                : "鏁屼汉 " + enemyId + " 姝讳骸";
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
                ? "鐜╁鍗囩骇鑷?Lv." + levelUp.getNewLevel()
                : "鐜╁ " + playerId + " 鍗囩骇鑷?Lv." + levelUp.getNewLevel();
        showStatusToast(toast);
    }

    private void handleDroppedItem(Message.S2C_DroppedItem droppedItem) {
        if (droppedItem == null || droppedItem.getItemsCount() == 0) {
            return;
        }
        if (!isMessageForCurrentRoom(droppedItem.getRoomId())) {
            return;
        }
        long arrivalMs = TimeUtils.millis();
        Message.Timestamp syncTime = droppedItem.hasSyncTime() ? droppedItem.getSyncTime() : null;
        long incomingTick = extractSyncTick(syncTime);
        long serverTimeMs = resolveServerTime(syncTime, arrivalMs);
        if (!shouldAcceptDroppedItems(incomingTick, serverTimeMs)) {
            return;
        }
        showStatusToast("鎺夎惤浜?" + droppedItem.getItemsCount() + " 涓墿鍝?");
        droppedItemDedupSet.clear();
        for (Message.ItemState itemState : droppedItem.getItemsList()) {
            if (itemState == null) {
                continue;
            }
            int itemId = (int) itemState.getItemId();
            if (!droppedItemDedupSet.add(itemId)) {
                continue;
            }
            Message.ItemState cached = itemStateCache.get(itemId);
            if (cached != null && !cached.getIsPicked()) {
                continue;
            }
            applyItemState(itemState);
        }
        droppedItemDedupSet.clear();
    }

    private void handleGameOver(Message.S2C_GameOver gameOver) {
        if (gameOver == null) {
            showStatusToast( "娓告垙缁撴潫");
        } else {
            showStatusToast(gameOver.getVictory() ? "鑳滃埄锛?" : "澶辫触锛?");
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
            showStatusToast(gameOver.getVictory() ? "浣犺耽浜?" : "浣犺緭浜?");
        }
        projectileViews.clear();
        projectileImpacts.clear();
        resetTargetingState();
        resetAutoAttackState();
        clearItemState();
    }
    /**
     * 澶勭悊娓告垙浜嬩欢
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
        hudMatrix.setToOrtho2D(0, 0, width, height);
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
        if (hud != null) {
            hud.dispose();
            hud = null;
        }        if (upgradeStage != null) {
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
     * 鏇存柊鏄剧ず浣嶇疆
     * @param delta
     */
    private void updateDisplayPosition(float delta) {
        // 濡傛灉鏈垵濮嬪寲鍒欒繑鍥?
        if (!hasReceivedInitialState) {
            return;
        }
        // 濡傛灉娌℃湁绉诲姩
        if (!isLocallyMoving) {
            displayPosition.set(predictedPosition);
            return;
        }
        // 濡傛灉璺濈寰堣繎
        float distSq = displayPosition.dst2(predictedPosition);
        if (distSq <= DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE) {
            displayPosition.set(predictedPosition);
            return;
        }
        // 璁板綍婕傜Щ
        float distance = (float) Math.sqrt(distSq);
        if (distance > DISPLAY_DRIFT_LOG_THRESHOLD) {
            logDisplayDrift(distance);
        }
        // 骞虫粦鎻掑€?
        float alpha = MathUtils.clamp(delta * DISPLAY_LERP_RATE, 0f, 1f);
        displayPosition.lerp(predictedPosition, alpha);
    }

    /**
     * 璁板綍甯ч棿闅斿皷宄?
     * @param rawDelta
     * @param stableDelta
     */
    private void logFrameDeltaSpike(float rawDelta, float stableDelta) {
        // 濡傛灉宸紓寰堝皬
        if (Math.abs(rawDelta - stableDelta) < DELTA_SPIKE_THRESHOLD) {
            return;
        }
        // 璁板綍鏃ュ織
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDeltaSpikeLogMs < DELTA_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Frame delta spike raw=" + rawDelta + " stable=" + stableDelta
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDeltaSpikeLogMs = nowMs;
    }

    /**
     * 璁板綍鏈嶅姟鍣ㄤ慨姝?
     * @param correctionDist
     * @param lastProcessedSeq
     */
    private void logServerCorrection(float correctionDist, int lastProcessedSeq) {
        // 濡傛灉璺濈寰堝皬
        if (correctionDist < POSITION_CORRECTION_LOG_THRESHOLD) {
            return;
        }
        // 璁板綍鏃ュ織
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
     * 璁板綍鏄剧ず婕傜Щ
     * @param drift
     */
    private void logDisplayDrift(float drift) {
        // 濡傛灉婕傜Щ寰堝皬
        if (drift < DISPLAY_DRIFT_LOG_THRESHOLD) {
            return;
        }
        // 璁板綍鏃ュ織
        long nowMs = TimeUtils.millis();
        if (nowMs - lastDisplayDriftLogMs < DISPLAY_LOG_INTERVAL_MS) {
            return;
        }
        Gdx.app.log(TAG, "Display drift=" + drift + " facingRight=" + facingRight
                + " pendingInputs=" + unconfirmedInputs.size());
        lastDisplayDriftLogMs = nowMs;
    }

    /**
     * 瑙ｆ瀽寰呭鐞嗘寔缁椂闂?
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
     * 閲嶇疆寰呭鐞嗚緭鍏ョ疮鍔犲櫒
     */
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
            lastDroppedItemServerTimeMs = -1L;
            lastDroppedItemTick = -1L;
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
     * 閲嶇疆鍒濆鐘舵€佽窡韪?
     */
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
     * 娓呴櫎鍒濆鐘舵€佺瓑寰?
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
     * 鍙兘璇锋眰鍒濆鐘舵€侀噸鍚屾
     */
    private void maybeRequestInitialStateResync() {
        // 濡傛灉宸叉敹鍒板垵濮嬬姸鎬?
        if (hasReceivedInitialState) {
            return;
        }
        // 濡傛灉鏈紑濮?
        long now = TimeUtils.millis();
        // 閲嶇疆璺熻釜
        if (initialStateStartMs == 0L) {
            resetInitialStateTracking();
            return;
        }
        // 瀹氭湡閲嶈瘯
        if ((now - lastInitialStateRequestMs) >= INITIAL_STATE_REQUEST_INTERVAL_MS) {
            maybeSendInitialStateRequest(now, "retry_interval");
        }
        // 璁板綍璀﹀憡
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

    /**
     * 鍙兘鍙戦€佸垵濮嬬姸鎬佽姹?
     */
    private void maybeSendInitialStateRequest(long timestampMs, String reason) {
        initialStateRequestCount++;
        if (game != null) {
            String tag = reason != null ? reason : "unknown";
            String requestTag = "GameScreen:" + tag + "#" + initialStateRequestCount;
            expectFullGameStateSync("initial_request:" + requestTag);
            game.requestFullGameStateSync(requestTag);
        }
        lastInitialStateRequestMs = timestampMs;
    }

    /**
     * 娓叉煋鍔犺浇瑕嗙洊灞?
     */
    private void renderLoadingOverlay() {
        renderTextOverlay(getLoadingMessage());
    }

    private void renderReconnectOverlay() {
        renderTextOverlay("杩炴帴寮傚父锛屾鍦ㄩ噸杩?..");
    }

    private void renderReconnectBanner() {
        if (batch == null || loadingFont == null || loadingLayout == null || camera == null) {
            return;
        }
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        String message = "杩炴帴寮傚父锛屾鍦ㄩ噸杩?..";
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

    private void renderServerPausedBanner() {
        if (batch == null || loadingFont == null || loadingLayout == null || camera == null) {
            return;
        }
        batch.setProjectionMatrix(camera.combined);
        batch.begin();
        String message =  "鏈嶅姟鍣ㄦ暣澶囦腑锛岃绋嶇瓑鐗囧埢锛?";
        loadingLayout.setText(loadingFont, message);
        float centerX = camera.position.x;
        float centerY = camera.position.y + WORLD_HEIGHT * 0.25f;
        float x = centerX - loadingLayout.width / 2f;
        float y = centerY + loadingLayout.height / 2f;
        loadingFont.setColor(1f, 1f, 1f, 0.8f);
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
     * 鑾峰彇鍔犺浇娑堟伅
     * @return
     */
    private String getLoadingMessage() {
        if (initialStateStartMs == 0L) {
            return "鍔犺浇涓?..";
        }
        long waitMs = TimeUtils.millis() - initialStateStartMs;
        if (waitMs >= INITIAL_STATE_FAILURE_HINT_MS) {
            return "鍔犺浇瓒呮椂锛屾鍦ㄩ噸璇?.. 绗?" + initialStateRequestCount + " 娆?";
        }
        if (waitMs >= INITIAL_STATE_CRITICAL_MS) {
            return "鍔犺浇缂撴參锛岃鑰愬績绛夊緟... 绗?" + initialStateRequestCount + " 娆?";
        }
        if (waitMs >= INITIAL_STATE_WARNING_MS) {
            return "姝ｅ湪杩炴帴鏈嶅姟鍣?.. 绗?" + initialStateRequestCount + " 娆?";
        }
        return "鍔犺浇涓?.. 绗?" + Math.max(1, initialStateRequestCount) + " 娆?";
    }
}










