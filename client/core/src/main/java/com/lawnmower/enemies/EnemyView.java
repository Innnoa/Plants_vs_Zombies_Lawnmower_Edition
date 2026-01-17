package com.lawnmower.enemies;

import com.badlogic.gdx.graphics.g2d.Animation;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.badlogic.gdx.math.MathUtils;
import com.badlogic.gdx.math.Vector2;
import com.badlogic.gdx.utils.TimeUtils;

import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Handles interpolation and rendering for a single enemy instance.
 */
public class EnemyView {

    private static final float DISPLAY_LERP_RATE = 12f;
    private static final float DISPLAY_SNAP_DISTANCE = 4f;
    private static final long SNAPSHOT_RETENTION_MS = 800L;
    private static final long MAX_EXTRAPOLATION_MS = 150L;
    private static final long DEFAULT_ATTACK_DURATION_MS = 600L;

    private final int enemyId;
    private final float worldWidth;
    private final float worldHeight;
    private final Vector2 targetPosition = new Vector2();
    private final Vector2 displayPosition = new Vector2();
    private final Vector2 sampleBuffer = new Vector2();
    private final Deque<EnemySnapshot> snapshots = new ArrayDeque<>();

    private Animation<TextureRegion> walkAnimation;
    private Animation<TextureRegion> attackAnimation;
    private TextureRegion fallbackFrame;
    private float walkAnimationTime = 0f;
    private float attackAnimationTime = 0f;
    private long lastServerUpdateMs = 0L;
    private boolean alive = true;
    private boolean facingRight = true;
    private boolean attacking = false;
    private long attackEndServerTime = 0L;
    private boolean attackStateSynced = false;
    private int typeId = 0;
    private int health = 0;
    private int maxHealth = 1;

    public EnemyView(int enemyId, float worldWidth, float worldHeight) {
        this.enemyId = enemyId;
        this.worldWidth = worldWidth;
        this.worldHeight = worldHeight;
    }

    public void setVisual(int typeId,
                          Animation<TextureRegion> walkAnimation,
                          Animation<TextureRegion> attackAnimation,
                          TextureRegion fallback) {
        this.typeId = typeId;
        if (walkAnimation != null) {
            if (this.walkAnimation != walkAnimation) {
                walkAnimationTime = 0f;
            }
            this.walkAnimation = walkAnimation;
        }
        if (attackAnimation != null) {
            if (this.attackAnimation != attackAnimation) {
                attackAnimationTime = 0f;
            }
            this.attackAnimation = attackAnimation;
        } else if (this.attackAnimation == null) {
            this.attackAnimation = this.walkAnimation;
        }
        if (fallback != null) {
            this.fallbackFrame = fallback;
        }
    }

    public void updateFromServer(int typeId,
                                 boolean isAlive,
                                 int health,
                                 int maxHealth,
                                 Vector2 serverPosition,
                                 long serverTimeMs,
                                 Animation<TextureRegion> walkAnimation,
                                 Animation<TextureRegion> attackAnimation,
                                 TextureRegion fallback) {
        setVisual(typeId, walkAnimation, attackAnimation, fallback);
        pushSnapshot(serverPosition, serverTimeMs);
        this.alive = isAlive;
        this.health = health;
        this.maxHealth = Math.max(1, maxHealth);
    }

    public int getTypeId() {
        return typeId;
    }

    public void snapTo(Vector2 position, long timestampMs) {
        if (position == null) {
            return;
        }
        targetPosition.set(position);
        displayPosition.set(position);
        snapshots.clear();
        snapshots.add(new EnemySnapshot(new Vector2(position), new Vector2(), timestampMs));
        lastServerUpdateMs = timestampMs;
    }

    public boolean isAlive() {
        return alive;
    }

    public Vector2 getDisplayPosition(Vector2 out) {
        if (out == null) {
            return new Vector2(displayPosition);
        }
        return out.set(displayPosition);
    }

    public long getLastServerUpdateMs() {
        return lastServerUpdateMs;
    }

    public void render(SpriteBatch batch, float delta, long renderServerTimeMs) {
        if (batch == null || (!alive && snapshots.isEmpty())) {
            return;
        }

        boolean playingAttack = isAttackActive(renderServerTimeMs);
        TextureRegion frame = resolveFrame(delta, playingAttack);
        if (frame == null) {
            return;
        }

        if (renderServerTimeMs > 0L && !snapshots.isEmpty()) {
            targetPosition.set(samplePosition(renderServerTimeMs));
        }

        float halfWidth = frame.getRegionWidth() / 2f;
        float halfHeight = frame.getRegionHeight() / 2f;
        clampToWorld(targetPosition, halfWidth, halfHeight);

        if (delta > 0f) {
            float distSq = displayPosition.dst2(targetPosition);
            if (distSq > DISPLAY_SNAP_DISTANCE * DISPLAY_SNAP_DISTANCE) {
                displayPosition.set(targetPosition);
            } else {
                float alpha = MathUtils.clamp(delta * DISPLAY_LERP_RATE, 0f, 1f);
                displayPosition.lerp(targetPosition, alpha);
            }
        }

        clampToWorld(displayPosition, halfWidth, halfHeight);

        float drawX = displayPosition.x - halfWidth;
        float drawY = displayPosition.y - halfHeight;
        float originX = halfWidth;
        float originY = halfHeight;
        // 素材默认朝向左侧，因此向右移动时需要翻转
        float scaleX = facingRight ? -1f : 1f;

        batch.draw(frame, drawX, drawY, originX, originY,
                frame.getRegionWidth(), frame.getRegionHeight(), scaleX, 1f, 0f);
    }

    private void pushSnapshot(Vector2 serverPosition, long serverTimeMs) {
        if (serverPosition == null) {
            return;
        }
        Vector2 posCopy = new Vector2(serverPosition);
        Vector2 velocity = new Vector2();
        if (!snapshots.isEmpty()) {
            EnemySnapshot last = snapshots.peekLast();
            long deltaMs = Math.max(1L, serverTimeMs - last.serverTimeMs);
            velocity.set(posCopy).sub(last.position).scl(1000f / deltaMs);
            if (Math.abs(velocity.x) > 0.001f) {
                facingRight = velocity.x >= 0f;
            }
        }
        snapshots.addLast(new EnemySnapshot(posCopy, velocity, serverTimeMs));
        lastServerUpdateMs = serverTimeMs;
        targetPosition.set(posCopy);
        if (snapshots.size() == 1) {
            displayPosition.set(posCopy);
        }
        trimSnapshots(serverTimeMs);
    }

    private void trimSnapshots(long serverTimeMs) {
        while (!snapshots.isEmpty() &&
                (serverTimeMs - snapshots.peekFirst().serverTimeMs) > SNAPSHOT_RETENTION_MS) {
            snapshots.removeFirst();
        }
    }

    public void triggerAttack(long serverTimeMs, long durationMs) {
        attackStateSynced = false;
        Animation<TextureRegion> animation = attackAnimation != null ? attackAnimation : walkAnimation;
        if (animation == null) {
            return;
        }
        long animDurationMs = durationMs > 0 ? durationMs
                : (long) (animation.getAnimationDuration() * 1000f);
        if (animDurationMs <= 0L) {
            animDurationMs = DEFAULT_ATTACK_DURATION_MS;
        }
        long referenceTime = serverTimeMs > 0L ? serverTimeMs : TimeUtils.millis();
        attackEndServerTime = referenceTime + animDurationMs;
        attacking = true;
        attackAnimationTime = 0f;
    }

    public void setAttacking(boolean active, long serverTimeMs, long expectedDurationMs) {
        Animation<TextureRegion> animation = attackAnimation != null ? attackAnimation : walkAnimation;
        if (animation == null) {
            attackStateSynced = false;
            attacking = false;
            return;
        }

        attackStateSynced = true;
        if (active && !attacking) {
            attackAnimationTime = 0f;
        } else if (!active && attacking) {
            attackAnimationTime = 0f;
        }
        attacking = active;

        long baseDuration = expectedDurationMs > 0L
                ? expectedDurationMs
                : (long) (animation.getAnimationDuration() * 1000f);
        if (baseDuration <= 0L) {
            baseDuration = DEFAULT_ATTACK_DURATION_MS;
        }
        long referenceTime = serverTimeMs > 0L ? serverTimeMs : TimeUtils.millis();
        attackEndServerTime = referenceTime + baseDuration;
    }

    public boolean isAttackStateSynced() {
        return attackStateSynced;
    }

    public long getAttackAnimationDurationMs() {
        if (attackAnimation != null) {
            return (long) (attackAnimation.getAnimationDuration() * 1000f);
        }
        if (walkAnimation != null) {
            return (long) (walkAnimation.getAnimationDuration() * 1000f);
        }
        return 0L;
    }

    private boolean isAttackActive(long renderServerTimeMs) {
        if (attackStateSynced) {
            return attacking;
        }
        if (!attacking) {
            return false;
        }
        if (attackAnimation == null) {
            attacking = false;
            return false;
        }
        long referenceTime = renderServerTimeMs > 0L ? renderServerTimeMs : TimeUtils.millis();
        if (referenceTime >= attackEndServerTime) {
            attacking = false;
            return false;
        }
        return true;
    }

    public void faceTowards(Vector2 target) {
        if (target == null) {
            return;
        }
        float deltaX = target.x - displayPosition.x;
        if (Math.abs(deltaX) <= 0.001f) {
            return;
        }
        facingRight = deltaX >= 0f;
    }

    private TextureRegion resolveFrame(float delta, boolean playingAttack) {
        float clampedDelta = Math.max(0f, delta);
        if (playingAttack && attackAnimation != null && attackAnimation.getKeyFrames().length > 0) {
            attackAnimationTime += clampedDelta;
            TextureRegion frame = attackAnimation.getKeyFrame(attackAnimationTime, true);
            if (frame != null) {
                return frame;
            }
        }
        if (walkAnimation != null && walkAnimation.getKeyFrames().length > 0) {
            walkAnimationTime += clampedDelta;
            TextureRegion frame = walkAnimation.getKeyFrame(walkAnimationTime, true);
            if (frame != null) {
                return frame;
            }
        }
        return fallbackFrame;
    }

    private Vector2 samplePosition(long renderServerTimeMs) {
        EnemySnapshot previous = null;
        EnemySnapshot next = null;

        for (EnemySnapshot snapshot : snapshots) {
            if (snapshot.serverTimeMs <= renderServerTimeMs) {
                previous = snapshot;
            } else {
                next = snapshot;
                break;
            }
        }

        if (previous != null && next != null) {
            long span = Math.max(1L, next.serverTimeMs - previous.serverTimeMs);
            float t = MathUtils.clamp((renderServerTimeMs - previous.serverTimeMs) / (float) span, 0f, 1f);
            return sampleBuffer.set(previous.position).lerp(next.position, t);
        }

        EnemySnapshot snapshot = previous != null ? previous : snapshots.peekFirst();
        if (snapshot == null) {
            return targetPosition;
        }
        long aheadMs = renderServerTimeMs - snapshot.serverTimeMs;
        float clampedAhead = MathUtils.clamp(aheadMs, -MAX_EXTRAPOLATION_MS, MAX_EXTRAPOLATION_MS);
        float seconds = clampedAhead / 1000f;
        return sampleBuffer.set(snapshot.position).mulAdd(snapshot.velocity, seconds);
    }

    private void clampToWorld(Vector2 position, float halfWidth, float halfHeight) {
        position.x = MathUtils.clamp(position.x, halfWidth, worldWidth - halfWidth);
        position.y = MathUtils.clamp(position.y, halfHeight, worldHeight - halfHeight);
    }

    private static final class EnemySnapshot {
        final Vector2 position;
        final Vector2 velocity;
        final long serverTimeMs;

        EnemySnapshot(Vector2 position, Vector2 velocity, long serverTimeMs) {
            this.position = position;
            this.velocity = velocity;
            this.serverTimeMs = serverTimeMs;
        }
    }
}
