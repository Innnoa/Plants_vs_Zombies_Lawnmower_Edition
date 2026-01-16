package com.lawnmower.enemies;

import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Client-side enemy type definitions (must stay in sync with server enemy_types.hpp).
 */
public final class EnemyDefinitions {

    public static final class Definition {
        private final int typeId;
        private final String name;
        private final String atlasPath;
        private final String regionPrefix;
        private final float frameDuration;
        private final String attackAtlasPath;
        private final String attackRegionPrefix;
        private final float attackFrameDuration;
        private final float attackIntervalSeconds;

        private Definition(int typeId,
                           String name,
                           String atlasPath,
                           String regionPrefix,
                           float frameDuration,
                           String attackAtlasPath,
                           String attackRegionPrefix,
                           float attackFrameDuration,
                           float attackIntervalSeconds) {
            this.typeId = typeId;
            this.name = name;
            this.atlasPath = atlasPath;
            this.regionPrefix = regionPrefix;
            this.frameDuration = frameDuration;
            this.attackAtlasPath = attackAtlasPath;
            this.attackRegionPrefix = attackRegionPrefix;
            this.attackFrameDuration = attackFrameDuration;
            this.attackIntervalSeconds = attackIntervalSeconds;
        }

        public int getTypeId() {
            return typeId;
        }

        public String getName() {
            return name;
        }

        public String getAtlasPath() {
            return atlasPath;
        }

        public String getRegionPrefix() {
            return regionPrefix;
        }

        public float getFrameDuration() {
            return frameDuration;
        }

        public String getAttackAtlasPath() {
            return attackAtlasPath;
        }

        public String getAttackRegionPrefix() {
            return attackRegionPrefix;
        }

        public float getAttackFrameDuration() {
            return attackFrameDuration;
        }

        public float getAttackIntervalSeconds() {
            return attackIntervalSeconds;
        }
    }

    private static final Map<Integer, Definition> DEFINITIONS = new LinkedHashMap<>();
    private static final int DEFAULT_TYPE_ID = 1;

    static {
        // 1: Normal zombie (baseline)
        register(new Definition(
                1,
                "Normal Zombie",
                "Zombie/NormalZombie/Walk/walk.atlas",
                "zombie",
                0.08f,
                "Zombie/NormalZombie/Attack/attack.atlas",
                "ZombieAttack",
                0.08f,
                0.8f
        ));

        // 2: Cone zombie (reuses normal assets for now)
        register(new Definition(
                2,
                "Cone Zombie",
                "Zombie/NormalZombie/Walk/walk.atlas",
                "zombie",
                0.08f,
                "Zombie/NormalZombie/Attack/attack.atlas",
                "ZombieAttack",
                0.08f,
                0.8f
        ));

        // 3: Bucket zombie (reuses normal assets for now)
        register(new Definition(
                3,
                "Bucket Zombie",
                "Zombie/NormalZombie/Walk/walk.atlas",
                "zombie",
                0.08f,
                "Zombie/NormalZombie/Attack/attack.atlas",
                "ZombieAttack",
                0.08f,
                0.8f
        ));

        // 4: Football zombie (reuses normal assets for now)
        register(new Definition(
                4,
                "Football Zombie",
                "Zombie/NormalZombie/Walk/walk.atlas",
                "zombie",
                0.08f,
                "Zombie/NormalZombie/Attack/attack.atlas",
                "ZombieAttack",
                0.08f,
                0.8f
        ));
    }

    private EnemyDefinitions() {
    }

    private static void register(Definition definition) {
        DEFINITIONS.put(definition.typeId, definition);
    }

    public static Definition get(int typeId) {
        return DEFINITIONS.getOrDefault(typeId, DEFINITIONS.get(DEFAULT_TYPE_ID));
    }

    public static Collection<Definition> all() {
        return Collections.unmodifiableCollection(DEFINITIONS.values());
    }

    public static int getDefaultTypeId() {
        return DEFAULT_TYPE_ID;
    }
}
