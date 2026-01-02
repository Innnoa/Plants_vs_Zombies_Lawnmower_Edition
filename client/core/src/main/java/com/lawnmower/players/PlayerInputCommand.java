package com.lawnmower.players;

import com.badlogic.gdx.math.Vector2;

public class PlayerInputCommand {
    public final int seq;
    public final Vector2 moveDir;
    public final boolean isAttacking;
    public final long timestampMs;
    public final float deltaSeconds;

    public PlayerInputCommand(int seq, Vector2 moveDir, boolean isAttacking, float deltaSeconds) {
        this.seq = seq;
        this.moveDir = new Vector2(moveDir);
        this.isAttacking = isAttacking;
        this.deltaSeconds = Math.max(deltaSeconds, 1f / 120f);
        this.timestampMs = System.currentTimeMillis();
    }

    public int getDeltaMs() {
        return Math.max(1, Math.round(deltaSeconds * 1000f));
    }
}
