package com.lawnmower.screens;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Screen;
import com.badlogic.gdx.files.FileHandle;
import com.badlogic.gdx.graphics.Color;
import com.badlogic.gdx.graphics.GL20;
import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.Sprite;
import com.badlogic.gdx.math.Vector2;
import com.badlogic.gdx.scenes.scene2d.Actor;
import com.badlogic.gdx.scenes.scene2d.InputEvent;
import com.badlogic.gdx.scenes.scene2d.InputListener;
import com.badlogic.gdx.scenes.scene2d.Stage;
import com.badlogic.gdx.scenes.scene2d.ui.Image;
import com.badlogic.gdx.scenes.scene2d.ui.Label;
import com.badlogic.gdx.scenes.scene2d.ui.ScrollPane;
import com.badlogic.gdx.scenes.scene2d.ui.Skin;
import com.badlogic.gdx.scenes.scene2d.ui.Table;
import com.badlogic.gdx.scenes.scene2d.ui.TextButton;
import com.badlogic.gdx.scenes.scene2d.utils.ChangeListener;
import com.badlogic.gdx.scenes.scene2d.utils.SpriteDrawable;
import com.badlogic.gdx.utils.Align;
import com.badlogic.gdx.utils.Scaling;
import com.badlogic.gdx.utils.viewport.StretchViewport;
import com.lawnmower.Main;

import lawnmower.Message;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.Objects;

/**
 * Scene that displays the PvZ-themed game over page with ranking and return button.
 */
public class GameOverScreen implements Screen {
    private static final float DESIGN_WIDTH = 2560f;
    private static final float DESIGN_HEIGHT = 1440f;
    private static final String BACKGROUND_PATH = "background/GameOverBackground.png";
    private static final String VICTORY_ICON_PATH = "background/Vector.png";
    private static final String DEFEAT_ICON_PATH = "background/UnVector.png";
    private static final int MAX_NAME_CODEPOINTS = 4;

    private final Main game;
    private final Skin skin;
    private final Message.S2C_GameOver payload;

    private Stage stage;
    private Texture backgroundTexture;
    private Texture victoryIconTexture;
    private Texture defeatIconTexture;
    private Label tooltipLabel;
    private TextButton backButton;
    private boolean waitingForRoomUpdate = false;
    private final Vector2 tooltipStageCoords = new Vector2();

    public GameOverScreen(Main game, Message.S2C_GameOver payload) {
        this.game = Objects.requireNonNull(game, "game");
        this.skin = game.getSkin();
        this.payload = payload;
    }

    @Override
    public void show() {
        stage = new Stage(new StretchViewport(DESIGN_WIDTH, DESIGN_HEIGHT));
        Gdx.input.setInputProcessor(stage);

        backgroundTexture = loadTextureOrFallback(BACKGROUND_PATH, new Color(0.08f, 0.18f, 0.08f, 1f),
                (int) DESIGN_WIDTH, (int) DESIGN_HEIGHT);
        victoryIconTexture = loadTextureOrFallback(VICTORY_ICON_PATH, Color.GOLD, 220, 220);
        defeatIconTexture = loadTextureOrFallback(DEFEAT_ICON_PATH, Color.BROWN, 220, 220);
        tooltipLabel = createTooltipLabel();

        buildUi();
    }

    private void buildUi() {
        Image background = new Image(backgroundTexture);
        background.setFillParent(true);
        background.setScaling(Scaling.stretch);
        stage.addActor(background);

        Image icon = new Image(isVictory() ? victoryIconTexture : defeatIconTexture);
        icon.setSize(220f, 300f);
        icon.setPosition(540f, 230f);
        stage.addActor(icon);


        Table root = new Table();
        root.setFillParent(true);
        root.padTop(230f).padBottom(230f).padLeft(780f).padRight(660f);
        root.center();

        stage.addActor(root);

        Label titleLabel = createResultLabel();
        root.add(titleLabel)
                .growX()
                .padTop(170f)
                .padRight(400f)
                .padBottom(80f)
                .center()
                .row();

        Table scoreTable = new Table();
        scoreTable.top().left();
        populateScoreRows(scoreTable);

        ScrollPane scrollPane = new ScrollPane(scoreTable, skin);
        scrollPane.setFadeScrollBars(false);
        scrollPane.setScrollingDisabled(true, false);
        scrollPane.getStyle().background = null;
        root.add(scrollPane).expand().fill().row();

        backButton = new TextButton("返回房间", skin, "CreateButton");
        backButton.addListener(new ChangeListener() {
            @Override
            public void changed(ChangeEvent event, Actor actor) {
                if (waitingForRoomUpdate) {
                    return;
                }
                waitingForRoomUpdate = true;
                backButton.setDisabled(true);
                backButton.setText("正在返回...");
                game.requestReturnToRoomFromGameOver();
            }
        });
        root.add(backButton).size(430f, 120f).center().padTop(-30f);

        stage.addActor(tooltipLabel);
    }

    private Label createResultLabel() {
        Label.LabelStyle titleStyle;
        if (skin != null && skin.has("default", Label.LabelStyle.class)) {
            titleStyle = new Label.LabelStyle(skin.get("default", Label.LabelStyle.class));
        } else {
            titleStyle = new Label.LabelStyle();
        }
        titleStyle.background = null;
        titleStyle.fontColor = isVictory() ? Color.valueOf("fff27c") : Color.valueOf("f88c8c");

        Label titleLabel = new Label(getResultTitle(), titleStyle);
        titleLabel.setAlignment(Align.center);
        titleLabel.setFontScale(1.5f);
        titleLabel.setWrap(true);
        return titleLabel;
    }

    private void populateScoreRows(Table table) {
        List<Message.PlayerScore> scores = getSortedScores();
        String surviveText = formatSurviveTime(payload != null ? payload.getSurviveTime() : 0);
        if (scores.isEmpty()) {
            Label empty = new Label("暂无信息", skin, "default_32");
            empty.setFontScale(1.5f);
            table.add(empty).left();
            return;
        }
        for (Message.PlayerScore score : scores) {
            Table row = new Table();
            row.left();
            row.defaults().padRight(36f).left();

            String fullName = score.getPlayerName();
            String shortName = abbreviateName(fullName);
            Label nameLabel = new Label(shortName + ": ", skin, "default_36");
            nameLabel.setFontScale(1.5f);
            attachTooltip(nameLabel, fullName);

            long scoreValue = Integer.toUnsignedLong(score.getDamageDealt());
            Label scoreLabel = new Label(" 得分: "+scoreValue, skin, "default_36");
            scoreLabel.setFontScale(1.5f);
            Label surviveLabel = new Label(" 生存时间: "+surviveText, skin, "default_36");
            surviveLabel.setFontScale(1.5f);

            row.add(nameLabel);
            row.add(scoreLabel);
            row.add(surviveLabel);
            table.add(row).left().growX().padBottom(16f).row();
        }
    }

    private List<Message.PlayerScore> getSortedScores() {
        List<Message.PlayerScore> sorted = new ArrayList<>();
        if (payload != null) {
            sorted.addAll(payload.getScoresList());
        }
        Collections.sort(sorted, new Comparator<Message.PlayerScore>() {
            @Override
            public int compare(Message.PlayerScore left, Message.PlayerScore right) {
                long leftDamage = Integer.toUnsignedLong(left.getDamageDealt());
                long rightDamage = Integer.toUnsignedLong(right.getDamageDealt());
                return Long.compare(rightDamage, leftDamage);
            }
        });
        return sorted;
    }


    private boolean isVictory() {
        return payload != null && payload.getVictory();
    }

    private String getResultTitle() {
        return isVictory() ? "恭喜通关" : "下次再战";
    }

    private String abbreviateName(String raw) {
        if (raw == null || raw.isBlank()) {
            return "未知";
        }
        String trimmed = raw.trim();
        int codePoints = trimmed.codePointCount(0, trimmed.length());
        if (codePoints <= MAX_NAME_CODEPOINTS) {
            return trimmed;
        }
        int cutIndex = trimmed.offsetByCodePoints(0, MAX_NAME_CODEPOINTS);
        return trimmed.substring(0, cutIndex) + "...";
    }

    private void attachTooltip(Label label, String fullName) {
        if (fullName == null || fullName.isBlank()) {
            return;
        }
        label.addListener(new InputListener() {
            @Override
            public void enter(InputEvent event, float x, float y, int pointer, Actor fromActor) {
                tooltipLabel.setText(fullName);
                tooltipLabel.setVisible(true);
                updateTooltipPosition();
            }

            @Override
            public void exit(InputEvent event, float x, float y, int pointer, Actor toActor) {
                tooltipLabel.setVisible(false);
            }

            @Override
            public boolean mouseMoved(InputEvent event, float x, float y) {
                updateTooltipPosition();
                return false;
            }
        });
    }

    private String formatSurviveTime(int seconds) {
        if (seconds <= 0) {
            return "00:00";
        }
        int minutes = seconds / 60;
        int secs = seconds % 60;
        return String.format(Locale.ROOT, "%02d:%02d", minutes, secs);
    }

    private void updateTooltipPosition() {
        if (stage == null || tooltipLabel == null || !tooltipLabel.isVisible()) {
            return;
        }
        tooltipStageCoords.set(
                (stage.getWidth() - tooltipLabel.getWidth()) * 0.5f,
                (stage.getHeight() - tooltipLabel.getHeight()) * 0.5f);
        tooltipLabel.setPosition(tooltipStageCoords.x, tooltipStageCoords.y);
    }

    private Texture loadTextureOrFallback(String path, Color fallbackColor, int width, int height) {
        try {
            FileHandle handle = Gdx.files.internal(path);
            if (handle.exists()) {
                Texture texture = new Texture(handle);
                texture.setFilter(Texture.TextureFilter.Linear, Texture.TextureFilter.Linear);
                return texture;
            }
        } catch (Exception ignored) {
        }
        return createSolidTexture(fallbackColor, width, height);
    }

    private Texture createSolidTexture(Color color, int width, int height) {
        Pixmap pixmap = new Pixmap(Math.max(width, 8), Math.max(height, 8), Pixmap.Format.RGBA8888);
        pixmap.setColor(color);
        pixmap.fill();
        Texture texture = new Texture(pixmap);
        pixmap.dispose();
        return texture;
    }

    private Label createTooltipLabel() {
        Label.LabelStyle style;
        if (skin != null && skin.has("default_32", Label.LabelStyle.class)) {
            style = new Label.LabelStyle(skin.get("default_32", Label.LabelStyle.class));
        } else if (skin != null && skin.has("default", Label.LabelStyle.class)) {
            style = new Label.LabelStyle(skin.get("default", Label.LabelStyle.class));
        } else {
            style = new Label.LabelStyle();
        }
        style.background = null;
        Label tooltip = new Label("", style);
        tooltip.setSize(500f, 500f);
        tooltip.setAlignment(Align.center);
        tooltip.setVisible(false);
        return tooltip;
    }

    @Override
    public void render(float delta) {
        Gdx.gl.glClearColor(0f, 0f, 0f, 1f);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);
        if (stage == null) {
            return;
        }
        stage.act(delta);
        updateTooltipPosition();
        stage.draw();
    }

    @Override
    public void resize(int width, int height) {
        if (stage != null) {
            stage.getViewport().update(width, height, true);
        }
    }

    @Override
    public void pause() { }

    @Override
    public void resume() { }

    @Override
    public void hide() {
        if (Gdx.input.getInputProcessor() == stage) {
            Gdx.input.setInputProcessor(null);
        }
    }

    @Override
    public void dispose() {
        if (stage != null) {
            stage.dispose();
            stage = null;
        }
        disposeTexture(backgroundTexture);
        disposeTexture(victoryIconTexture);
        disposeTexture(defeatIconTexture);
    }

    private void disposeTexture(Texture texture) {
        if (texture != null) {
            texture.dispose();
        }
    }
}
