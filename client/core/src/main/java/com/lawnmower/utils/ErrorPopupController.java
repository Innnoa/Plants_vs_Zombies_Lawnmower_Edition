package com.lawnmower.utils;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Input;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.Animation;
import com.badlogic.gdx.graphics.g2d.TextureAtlas;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import com.badlogic.gdx.scenes.scene2d.InputEvent;
import com.badlogic.gdx.scenes.scene2d.InputListener;
import com.badlogic.gdx.scenes.scene2d.Stage;
import com.badlogic.gdx.scenes.scene2d.ui.Image;
import com.badlogic.gdx.scenes.scene2d.ui.Label;
import com.badlogic.gdx.scenes.scene2d.ui.Skin;
import com.badlogic.gdx.scenes.scene2d.ui.Table;
import com.badlogic.gdx.scenes.scene2d.ui.Window;
import com.badlogic.gdx.scenes.scene2d.utils.ClickListener;
import com.badlogic.gdx.scenes.scene2d.utils.TextureRegionDrawable;
import com.badlogic.gdx.utils.Align;
import com.badlogic.gdx.utils.Array;

public class ErrorPopupController {
    private final Stage stage;
    private final Skin skin;

    private final TextureAtlas defInAtlas;
    private final TextureAtlas defStayAtlas;
    private final TextureAtlas defOutAtlas;
    private final Animation<TextureRegion> animIn;
    private final Animation<TextureRegion> animStay;
    private final Animation<TextureRegion> animOut;

    private final Texture dialogBackground;
    private final Image animImage;

    private Window errorWindow;
    private Label messageLabel;
    private InputListener globalClickListener;

    private int currentAnimPhase = -1;
    private float animStateTime = 0f;
    private boolean justClicked = false;

    public ErrorPopupController(Stage stage, Skin skin) {
        this.stage = stage;
        this.skin = skin;

        defInAtlas = new TextureAtlas("def_in/def_in.atlas");
        defStayAtlas = new TextureAtlas("def/def.atlas");
        defOutAtlas = new TextureAtlas("def_out/def_out.atlas");

        animIn = buildInAnimation();
        animStay = buildStayAnimation();
        animOut = buildOutAnimation();

        Texture bg;
        try {
            bg = new Texture(Gdx.files.internal("background/speakBackground2.png"));
        } catch (Exception e) {
            Gdx.app.error("UI", "Failed to load speak background", e);
            bg = null;
        }
        dialogBackground = bg;

        animImage = new Image();
        animImage.setVisible(false);
    }

    private Animation<TextureRegion> buildInAnimation() {
        Array<TextureRegion> frames = new Array<>();
        for (int i = 0; i < 6; i++) {
            frames.add(defInAtlas.findRegion("in_" + i));
        }
        return new Animation<>(0.1f, frames);
    }

    private Animation<TextureRegion> buildStayAnimation() {
        Array<TextureRegion> frames = new Array<>();
        for (int i = 0; i < 12; i++) {
            String name = String.format("frame_%02d_delay-0.13s", i);
            frames.add(defStayAtlas.findRegion(name));
        }
        return new Animation<>(0.13f, frames, Animation.PlayMode.LOOP);
    }

    private Animation<TextureRegion> buildOutAnimation() {
        Array<TextureRegion> frames = new Array<>();
        for (int i = 0; i < 6; i++) {
            frames.add(defOutAtlas.findRegion("out_" + i));
        }
        return new Animation<>(0.1f, frames);
    }

    public void showError(String message) {
        if (currentAnimPhase != -1) return;

        ensureErrorWindow();
        messageLabel.setText(message);
        errorWindow.pack();
        errorWindow.setPosition(150, 750);
        if (errorWindow.getStage() == null) {
            stage.addActor(errorWindow);
        }

        currentAnimPhase = 0;
        animStateTime = 0f;
        animImage.setSize(400, 800);
        animImage.setPosition(0, 0);
        animImage.setVisible(true);
        if (animImage.getStage() == null) {
            stage.addActor(animImage);
        }

        attachGlobalListener();
    }

    private void ensureErrorWindow() {
        if (errorWindow != null) return;

        errorWindow = new Window("", skin);
        if (dialogBackground != null) {
            errorWindow.setBackground(new TextureRegionDrawable(new TextureRegion(dialogBackground)));
        } else {
            errorWindow.setBackground(skin.newDrawable("default-select", 0.1f, 0.1f, 0.1f, 0.8f));
        }
        errorWindow.setModal(true);
        errorWindow.setMovable(false);
        errorWindow.pad(30);

        errorWindow.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                if (currentAnimPhase == 0 || currentAnimPhase == 1) {
                    playOutAnimation();
                }
            }

            @Override
            public boolean touchDown(InputEvent event, float x, float y, int pointer, int button) {
                return true;
            }
        });

        Table content = new Table();
        content.pad(40);

        messageLabel = new Label("", skin, "default_32");
        messageLabel.setWrap(true);
        messageLabel.setAlignment(Align.center);
        messageLabel.setWidth(500);

        Label hintLabel = new Label("      点击任意位置关闭...", skin, "default_32");
        hintLabel.setWrap(true);
        hintLabel.setAlignment(Align.center);
        hintLabel.setWidth(500);

        content.add(messageLabel).width(500).padBottom(20).row();
        content.add(hintLabel).width(500);

        errorWindow.add(content).expand().fill();
    }

    private void attachGlobalListener() {
        if (globalClickListener != null) return;

        globalClickListener = new InputListener() {
            @Override
            public boolean touchDown(InputEvent event, float x, float y, int pointer, int button) {
                if (currentAnimPhase == 0 || currentAnimPhase == 1) {
                    playOutAnimation();
                }
                return true;
            }
        };
        stage.addListener(globalClickListener);
    }

    public void update(float delta) {
        if (currentAnimPhase == -1) return;

        animStateTime += delta;
        TextureRegion currentFrame = null;
        switch (currentAnimPhase) {
            case 0:
                currentFrame = animIn.getKeyFrame(animStateTime, false);
                if (animIn.isAnimationFinished(animStateTime)) {
                    playStayPhase();
                }
                break;
            case 1:
                currentFrame = animStay.getKeyFrame(animStateTime, true);
                break;
            case 2:
                currentFrame = animOut.getKeyFrame(animStateTime, false);
                if (animOut.isAnimationFinished(animStateTime)) {
                    cleanupAnimation();
                }
                break;
            default:
                break;
        }

        if (currentFrame != null) {
            animImage.setDrawable(new TextureRegionDrawable(currentFrame));
        }

        if (currentAnimPhase == 1) {
            if (Gdx.input.isTouched() || Gdx.input.isKeyJustPressed(Input.Keys.SPACE)) {
                if (!justClicked) {
                    justClicked = true;
                    playOutAnimation();
                }
            } else {
                justClicked = false;
            }
        }
    }

    public void hide() {
        cleanupAnimation();
    }

    public void dispose() {
        hide();
        defInAtlas.dispose();
        defStayAtlas.dispose();
        defOutAtlas.dispose();
        if (dialogBackground != null) {
            dialogBackground.dispose();
        }
    }

    private void playStayPhase() {
        currentAnimPhase = 1;
        animStateTime = 0f;
    }

    private void playOutAnimation() {
        if (currentAnimPhase == 0 || currentAnimPhase == 1) {
            currentAnimPhase = 2;
            animStateTime = 0f;
        }
    }

    private void cleanupAnimation() {
        if (animImage.getStage() != null) {
            animImage.remove();
        }
        if (errorWindow != null && errorWindow.getStage() != null) {
            errorWindow.remove();
        }
        if (globalClickListener != null) {
            stage.removeListener(globalClickListener);
            globalClickListener = null;
        }
        animImage.setVisible(false);
        currentAnimPhase = -1;
        animStateTime = 0f;
        justClicked = false;
    }
}