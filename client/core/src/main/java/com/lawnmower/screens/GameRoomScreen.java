package com.lawnmower.screens;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Screen;
import com.badlogic.gdx.graphics.GL20;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.scenes.scene2d.InputEvent;
import com.badlogic.gdx.scenes.scene2d.Stage;
import com.badlogic.gdx.scenes.scene2d.ui.Image;
import com.badlogic.gdx.scenes.scene2d.ui.Label;
import com.badlogic.gdx.scenes.scene2d.ui.Skin;
import com.badlogic.gdx.scenes.scene2d.ui.Table;
import com.badlogic.gdx.scenes.scene2d.ui.TextButton;
import com.badlogic.gdx.scenes.scene2d.utils.ClickListener;
import com.badlogic.gdx.utils.Scaling;
import com.badlogic.gdx.utils.viewport.StretchViewport;
import com.lawnmower.Main;
import com.lawnmower.utils.ErrorPopupController;
import lawnmower.Message;

import java.util.List;
import java.util.Objects;

public class GameRoomScreen implements Screen {
    private static final float DESIGN_WIDTH = 2560f;
    private static final float DESIGN_HEIGHT = 1440f;
    private static final int MAX_PLAYERS = 4;
    private static final int SLOT_WIDTH = 600;
    private static final int SLOT_HEIGHT = 700;

    private final Main game;
    private final Skin skin;

    private Stage stage;
    private TextButton readyButton;
    private TextButton startButton;
    private Table playerSlotTable;
    private ErrorPopupController errorController;

    private boolean isHost = false;
    private boolean amIReady = false;
    private boolean allPlayersReady = false;

    private Texture backgroundTexture;
    private Texture lockedSlotTex;
    private Texture unlockedSlotTex;

    public GameRoomScreen(Main game, Skin skin) {
        this.game = game;
        this.skin = skin;
    }

    @Override
    public void show() {
        stage = new Stage(new StretchViewport(DESIGN_WIDTH, DESIGN_HEIGHT));
        Gdx.input.setInputProcessor(stage);
        errorController = new ErrorPopupController(stage, skin);

        backgroundTexture = new Texture(Gdx.files.internal("background/gameReadyRoom.png"));
        Image bg = new Image(backgroundTexture);
        bg.setSize(DESIGN_WIDTH, DESIGN_HEIGHT);
        bg.setScaling(Scaling.stretch);
        stage.addActor(bg);

        lockedSlotTex = new Texture(Gdx.files.internal("background/LockedRoom.png"));
        unlockedSlotTex = new Texture(Gdx.files.internal("background/unLockedRoom.png"));

        TextButton backButton = new TextButton("离开房间", skin, "CreateButton");
        backButton.setSize(200, 60);
        backButton.setPosition(50, DESIGN_HEIGHT - 270);
        backButton.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                leaveRoom();
            }
        });
        stage.addActor(backButton);

        Label titleLabel = new Label("等待中...", skin);
        titleLabel.setFontScale(1.3f);
        titleLabel.pack();
        titleLabel.setPosition((DESIGN_WIDTH - titleLabel.getWidth()) / 2f, DESIGN_HEIGHT - 180);
        stage.addActor(titleLabel);

        playerSlotTable = new Table();
        playerSlotTable.pad(10).defaults().padLeft(10).padRight(10);
        stage.addActor(playerSlotTable);

        readyButton = new TextButton("准备", skin, "CreateButton");
        readyButton.setSize(200, 60);
        readyButton.setPosition((DESIGN_WIDTH - readyButton.getWidth()) / 2f, 120);
        readyButton.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                toggleReady();
            }
        });
        stage.addActor(readyButton);

        startButton = new TextButton("开始游戏", skin, "CreateButton");
        startButton.setSize(200, 60);
        startButton.setPosition((DESIGN_WIDTH - startButton.getWidth()) / 2f, 200);
        startButton.setVisible(false);
        startButton.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                startGame();
            }
        });
        stage.addActor(startButton);
    }

    public void onRoomUpdate(int roomId, List<Message.PlayerInfo> players) {
        final List<Message.PlayerInfo> safePlayers = Objects.requireNonNullElse(players, List.of());

        Gdx.app.postRunnable(() -> {
            playerSlotTable.clearChildren();

            boolean[] occupied = new boolean[MAX_PLAYERS];
            int occupiedCount = Math.min(safePlayers.size(), MAX_PLAYERS);
            for (int i = 0; i < occupiedCount; i++) {
                occupied[i] = true;
            }

            for (int i = 0; i < MAX_PLAYERS; i++) {
                Texture tex = occupied[i] ? unlockedSlotTex : lockedSlotTex;
                Image slot = new Image(tex);
                slot.setSize(SLOT_WIDTH, SLOT_HEIGHT);

                if (occupied[i]) {
                    playerSlotTable.add(slot).size(SLOT_WIDTH, SLOT_HEIGHT);
                } else {
                    playerSlotTable.add(slot).size(SLOT_WIDTH - 70, SLOT_HEIGHT - 10);
                }
            }

            updatePlayerSlotPosition();

            isHost = false;
            amIReady = false;
            boolean everyoneReady = !safePlayers.isEmpty();
            for (Message.PlayerInfo p : safePlayers) {
                if (p.getPlayerId() == game.getPlayerId()) {
                    amIReady = p.getIsReady();
                    readyButton.setText(amIReady ? "取消准备" : "准备");
                }
                if (p.getIsHost() && p.getPlayerId() == game.getPlayerId()) {
                    isHost = true;
                }
                if (!p.getIsReady()) {
                    everyoneReady = false;
                }
            }
            allPlayersReady = everyoneReady;
            startButton.setVisible(isHost);
            startButton.setDisabled(!isHost || !allPlayersReady);
        });
    }

    public void onSetReadyResult(Message.S2C_SetReadyResult result) {
        Gdx.app.postRunnable(() -> {
            if (!result.getSuccess()) {
                if (errorController != null) {
                    String message = result.getMessageReady();
                    if (message == null || message.isBlank()) {
                        message = "准备操作失败";
                    }
                    errorController.showError(message);
                }
                return;
            }
            amIReady = result.getIsReady();
            if (readyButton != null) {
                readyButton.setText(amIReady ? "取消准备" : "准备");
            }
        });
    }

    private void updatePlayerSlotPosition() {
        playerSlotTable.validate();
        playerSlotTable.pack();
        float x = (DESIGN_WIDTH - playerSlotTable.getWidth()) / 2f;
        float y = DESIGN_HEIGHT * 0.2f;
        playerSlotTable.setPosition(x, y);
    }

    private void toggleReady() {
        try {
            game.getTcpClient().sendSetReady(!amIReady);
        } catch (Exception e) {
            if (errorController != null) {
                errorController.showError("准备操作失败");
            }
        }
    }

    private void startGame() {
        if (!isHost) {
            return;
        }
        if (!allPlayersReady) {
            if (errorController != null) {
                errorController.showError("还有玩家未准备完毕，无法开始");
            }
            return;
        }
        try {
            game.getTcpClient().sendStartGame();
        } catch (Exception e) {
            if (errorController != null) {
                errorController.showError("无法开始游戏");
            }
        }
    }

    private void leaveRoom() {
        try {
            game.getTcpClient().sendLeaveRoom();
        } catch (Exception ignored) {
        }
        game.setScreen(new RoomListScreen(game, skin));
    }

    @Override
    public void render(float delta) {
        Gdx.gl.glClearColor(0.1f, 0.15f, 0.1f, 1);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);
        if (errorController != null) {
            errorController.update(delta);
        }
        stage.act(delta);
        stage.draw();
    }

    @Override
    public void resize(int width, int height) {
        stage.getViewport().update(width, height, true);
    }

    @Override
    public void pause() { }

    @Override
    public void resume() { }

    @Override
    public void hide() {
        if (errorController != null) {
            errorController.hide();
        }
    }

    @Override
    public void dispose() {
        if (errorController != null) {
            errorController.dispose();
            errorController = null;
        }
        if (backgroundTexture != null) {
            backgroundTexture.dispose();
        }
        if (lockedSlotTex != null) {
            lockedSlotTex.dispose();
        }
        if (unlockedSlotTex != null) {
            unlockedSlotTex.dispose();
        }
        if (stage != null) {
            stage.dispose();
        }
    }
}
