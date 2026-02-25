package com.lawnmower.ui;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.GlyphLayout;
import com.badlogic.gdx.graphics.g2d.SpriteBatch;
import com.badlogic.gdx.math.MathUtils;
import com.badlogic.gdx.math.Vector2;
import com.badlogic.gdx.math.Vector3;
import com.badlogic.gdx.utils.Array;
import com.badlogic.gdx.utils.Disposable;
import com.badlogic.gdx.utils.viewport.Viewport;
import lawnmower.Message;

/**
 * Heads-up display renderer for player HP/EXP bars, damage flash, floating text, and low-HP vignette.
 * Pure rendering utility; state is fed from GameScreen each frame.
 */
public class PlayerHudRenderer implements Disposable {
    private final Texture border;
    private final Texture hpFill;
    private final Texture hpFlash;
    private final Texture xpFill;
    private final Texture iconBlood;
    private final Texture lowOverlay;
    private final Texture avatar;
    private final BitmapFont font;
    private final GlyphLayout layout = new GlyphLayout();
    private final Vector2 anchor = new Vector2();
    private final Vector3 projectTmp = new Vector3();
    private final Array<FloatingText> floatingTexts = new Array<>();

    private float hpRatio = 1f;
    private float targetHpRatio = 1f;
    private int hp = 0;
    private int maxHp = 1;

    private float xpRatio = 0f;
    private float targetXpRatio = 0f;
    private int xp = 0;
    private int xpToNext = 1;
    private int level = 1;

    private float flashWidth = 0f;
    private float flashAlpha = 0f;
    private float flashTimer = 0f;
    private static final float FLASH_DURATION = 0.22f;

    private float lowAlpha = 0f;
    private float screenW = 1280f;
    private float screenH = 720f;

    public PlayerHudRenderer(BitmapFont font) {
        this.font = font != null ? font : new BitmapFont();
        border = new Texture(Gdx.files.internal("ui/blood/blood_boder.png"));
        hpFill = new Texture(Gdx.files.internal("ui/blood/blood_red.png"));
        hpFlash = new Texture(Gdx.files.internal("ui/blood/blood_white.png"));
        xpFill = new Texture(Gdx.files.internal("ui/blood/blood_green.png"));
        iconBlood = new Texture(Gdx.files.internal("ui/blood/icon_blood.png"));
        lowOverlay = new Texture(Gdx.files.internal("ui/blood/blood_low.png"));
        Texture tmpAvatar;
        try {
            tmpAvatar = new Texture(Gdx.files.internal("layer/icon/role_avatar_peashooter.png"));
        } catch (Exception e) {
            tmpAvatar = null;
        }
        avatar = tmpAvatar;
    }

    public void update(float delta,
                       Message.PlayerState selfState,
                       Vector2 worldPos,
                       Viewport viewport,
                       boolean selfAlive) {
        screenW = Gdx.graphics.getWidth();
        screenH = Gdx.graphics.getHeight();
        if (selfState != null) {
            maxHp = Math.max(1, selfState.getMaxHealth());
            hp = MathUtils.clamp(selfState.getHealth(), 0, maxHp);
            float newTargetHp = MathUtils.clamp(hp / (float) maxHp, 0f, 1f);
            if (newTargetHp < targetHpRatio - 0.0001f) {
                triggerDamageFlash(targetHpRatio - newTargetHp);
            }
            targetHpRatio = newTargetHp;

            xpToNext = Math.max(1, selfState.getExpToNext());
            xp = MathUtils.clamp(selfState.getExp(), 0, xpToNext);
            targetXpRatio = MathUtils.clamp(xp / (float) xpToNext, 0f, 1f);
            level = Math.max(1, selfState.getLevel());
        }

        // Smooth ratios
        float lerpSpeed = 10f;
        hpRatio += (targetHpRatio - hpRatio) * Math.min(1f, delta * lerpSpeed);
        xpRatio += (targetXpRatio - xpRatio) * Math.min(1f, delta * lerpSpeed);

        // Flash decay
        if (flashAlpha > 0f) {
            flashTimer += delta;
            float t = flashTimer / FLASH_DURATION;
            flashAlpha = Math.max(0f, 1f - t);
            flashWidth = Math.max(0f, flashWidth * (1f - delta * 6f));
        }

        // Low HP overlay
        lowAlpha = selfAlive ? MathUtils.clamp((0.30f - hpRatio) / 0.30f, 0f, 1f) * 0.45f : 0f;

        // Anchor for floating text (project world -> screen)
        if (worldPos != null && viewport != null) {
            projectTmp.set(worldPos.x, worldPos.y + 48f, 0f);
            viewport.project(projectTmp);
            anchor.set(projectTmp.x, projectTmp.y);
        }
        updateFloatingTexts(delta);
    }

    public void onDamage(float lostHp, float maxHealth, Vector2 worldPos, Viewport viewport) {
        if (maxHealth <= 0 || lostHp <= 0) {
            return;
        }
        float lostRatio = MathUtils.clamp(lostHp / maxHealth, 0f, 1f);
        targetHpRatio = Math.max(0f, targetHpRatio - lostRatio);
        triggerDamageFlash(lostRatio);
        spawnFloatingText("-" + (int) lostHp, worldPos, viewport);
    }

    private void triggerDamageFlash(float lostRatio) {
        flashWidth = lostRatio;
        flashAlpha = 1f;
        flashTimer = 0f;
    }

    private void spawnFloatingText(String text, Vector2 worldPos, Viewport viewport) {
        if (worldPos != null && viewport != null) {
            projectTmp.set(worldPos.x, worldPos.y + 48f, 0f);
            viewport.project(projectTmp);
            anchor.set(projectTmp.x, projectTmp.y);
        }
        FloatingText ft = new FloatingText();
        ft.text = text;
        ft.x = anchor.x + MathUtils.random(-6f, 6f);
        ft.y = anchor.y + MathUtils.random(8f, 14f);
        ft.vx = MathUtils.random(-10f, 10f);
        ft.vy = MathUtils.random(55f, 75f);
        ft.gravity = -140f;
        ft.life = 1.1f;
        ft.maxLife = ft.life;
        ft.color = new Color(1f, 0.9f, 0.9f, 1f);
        ft.active = true;
        floatingTexts.add(ft);
    }

    private void updateFloatingTexts(float delta) {
        for (int i = floatingTexts.size - 1; i >= 0; i--) {
            FloatingText ft = floatingTexts.get(i);
            if (!ft.active) {
                floatingTexts.removeIndex(i);
                continue;
            }
            ft.life -= delta;
            if (ft.life <= 0f) {
                floatingTexts.removeIndex(i);
                continue;
            }
            ft.x += ft.vx * delta;
            ft.y += ft.vy * delta;
            ft.vy += ft.gravity * delta;
            float alpha = MathUtils.clamp(ft.life / ft.maxLife, 0f, 1f);
            ft.color.a = alpha;
        }
    }

    public void render(SpriteBatch batch) {
        float hpBarWidth = 320f;
        float hpBarHeight = 24f;
        float xpBarWidth = 320f;
        float xpBarHeight = 16f;

        float hpX = 120f;
        float hpY = screenH - 56f;
        float xpX = hpX;
        float xpY = hpY - 22f;

        if (avatar != null) {
            batch.draw(avatar, hpX - 92f, hpY - 6f, 44f, 44f);
        }
        if (iconBlood != null) {
            batch.draw(iconBlood, hpX - 46f, hpY - 4f, 36f, 36f);
        }

        // HP bar
        if (hpFill != null) {
            batch.draw(hpFill, hpX, hpY, hpBarWidth * hpRatio, hpBarHeight);
        }
        if (hpFlash != null && flashAlpha > 0f && flashWidth > 0f) {
            Color old = batch.getColor();
            batch.setColor(1f, 1f, 1f, flashAlpha);
            batch.draw(hpFlash, hpX + hpBarWidth * hpRatio, hpY, hpBarWidth * flashWidth, hpBarHeight);
            batch.setColor(old);
        }
        if (border != null) {
            batch.draw(border, hpX, hpY, hpBarWidth, hpBarHeight);
        }

        // HP text
        String hpText = hp + "/" + maxHp;
        layout.setText(font, hpText);
        font.setColor(Color.WHITE);
        font.draw(batch, hpText, hpX + hpBarWidth / 2f - layout.width / 2f, hpY + hpBarHeight - 4f);
        String hpPct = Math.round(hpRatio * 100f) + "%";
        layout.setText(font, hpPct);
        font.draw(batch, hpPct, hpX + hpBarWidth - layout.width - 6f, hpY + hpBarHeight - 4f);

        // XP bar
        if (xpFill != null) {
            batch.draw(xpFill, xpX, xpY, xpBarWidth * xpRatio, xpBarHeight);
        }
        if (border != null) {
            batch.draw(border, xpX, xpY, xpBarWidth, xpBarHeight);
        }
        String xpText = "Lv." + level + " " + xp + "/" + xpToNext;
        layout.setText(font, xpText);
        font.draw(batch, xpText, xpX + 6f, xpY + xpBarHeight - 2f);

        // Floating damage texts
        for (FloatingText ft : floatingTexts) {
            font.setColor(ft.color);
            font.draw(batch, ft.text, ft.x, ft.y);
        }
        font.setColor(Color.WHITE);

        // Low HP vignette
        if (lowOverlay != null && lowAlpha > 0f) {
            Color old = batch.getColor();
            batch.setColor(1f, 1f, 1f, lowAlpha);
            batch.draw(lowOverlay, 0f, 0f, screenW, screenH);
            batch.setColor(old);
        }
    }

    @Override
    public void dispose() {
        border.dispose();
        hpFill.dispose();
        hpFlash.dispose();
        xpFill.dispose();
        iconBlood.dispose();
        lowOverlay.dispose();
        if (avatar != null) {
            avatar.dispose();
        }
    }

    private static class FloatingText {
        String text;
        float x;
        float y;
        float vx;
        float vy;
        float gravity;
        float life;
        float maxLife;
        Color color;
        boolean active;
    }
}
