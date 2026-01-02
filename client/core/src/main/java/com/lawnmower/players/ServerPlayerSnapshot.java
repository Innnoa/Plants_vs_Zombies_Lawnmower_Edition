package com.lawnmower.players;

import com.badlogic.gdx.math.Vector2;

public class ServerPlayerSnapshot {
    public final Vector2 position;
    public final float rotation;
    public final Vector2 velocity;
    public final long serverTimestampMs;

    public ServerPlayerSnapshot(Vector2 position, float rotation, Vector2 velocity, long serverTimestampMs) {
        this.position = new Vector2(position);
        this.rotation = rotation;
        this.velocity = new Vector2(velocity);
        this.serverTimestampMs = serverTimestampMs;
    }
}
