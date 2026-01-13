package com.lawnmower.screens;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.Screen;
import com.badlogic.gdx.graphics.GL20;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.scenes.scene2d.Actor;
import com.badlogic.gdx.scenes.scene2d.Group;
import com.badlogic.gdx.scenes.scene2d.InputEvent;
import com.badlogic.gdx.scenes.scene2d.Stage;
import com.badlogic.gdx.scenes.scene2d.ui.*;
import com.badlogic.gdx.scenes.scene2d.utils.ChangeListener;
import com.badlogic.gdx.scenes.scene2d.utils.ClickListener;
import com.badlogic.gdx.utils.Align;
import com.badlogic.gdx.utils.Scaling;
import com.badlogic.gdx.utils.viewport.StretchViewport;
import com.lawnmower.Main;
import com.lawnmower.ui.Drop.DropPopup;
import com.lawnmower.ui.slider.StepSlider;
import com.lawnmower.utils.ErrorPopupController;
import lawnmower.Message;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class RoomListScreen implements Screen {
    private final Main game;
    private final Skin skin;

    private Stage stage;
    private Table roomTable;
    private Texture backgroundTexture;
    private Image backgroundImage;
    private Label titleLabel;

    private static final float DESIGN_WIDTH = 2560f;
    private static final float DESIGN_HEIGHT = 1440f;

    private List<Message.RoomInfo> allRooms;
    private int currentPage = 0;
    private static final int ROOMS_PER_PAGE = 8;
    private TextButton prevPageBtn;
    private TextButton nextPageBtn;
    private Label pageInfoLabel;

    private ErrorPopupController errorPopup;

    public RoomListScreen(Main game, Skin skin) {
        this.game = game;
        this.skin = skin;
        this.allRooms = new ArrayList<>();
    }

    @Override
    public void show() {
        stage = new Stage(new StretchViewport(DESIGN_WIDTH, DESIGN_HEIGHT));
        Gdx.input.setInputProcessor(stage);
        errorPopup = new ErrorPopupController(stage, skin);

        backgroundTexture = new Texture(Gdx.files.internal("background/roomListBackground.png"));
        backgroundImage = new Image(backgroundTexture);
        backgroundImage.setSize(DESIGN_WIDTH, DESIGN_HEIGHT);
        backgroundImage.setScaling(Scaling.stretch);
        stage.addActor(backgroundImage);

        Table mainTable = new Table();
        stage.addActor(mainTable);

        TextButton.TextButtonStyle backButtonStyle = skin.get("RoomList", TextButton.TextButtonStyle.class);
        TextButton.TextButtonStyle defaultButtonStyle = skin.get("RoomList_def", TextButton.TextButtonStyle.class);

        TextButton backButton = new TextButton("返回", backButtonStyle);
        backButton.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                game.setScreen(new MainMenuScreen(game, skin));
            }
        });

        TextButton refreshBtn = new TextButton("刷新列表", backButtonStyle);
        refreshBtn.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                requestRoomListFromServer();
            }
        });

        TextButton createBtn = new TextButton("创建房间", backButtonStyle);
        createBtn.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                openCreateRoomDialog();
            }
        });

        roomTable = new Table();
        roomTable.top().center();
        roomTable.padTop(50);
        roomTable.defaults().padBottom(20);

        ScrollPane.ScrollPaneStyle scrollPaneStyle = new ScrollPane.ScrollPaneStyle(skin.get("default",
                ScrollPane.ScrollPaneStyle.class));
        scrollPaneStyle.background = null;
        ScrollPane scrollPane = new ScrollPane(roomTable, scrollPaneStyle);
        scrollPane.setSize(1650, 700);
        scrollPane.setPosition(470, 150);
        stage.addActor(scrollPane);

        prevPageBtn = new TextButton("<<", defaultButtonStyle);
        prevPageBtn.setSize(150, 70);
        prevPageBtn.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                if (currentPage > 0) {
                    currentPage--;
                    refreshRoomList();
                }
            }
        });

        nextPageBtn = new TextButton(">>", defaultButtonStyle);
        nextPageBtn.setSize(150, 70);
        nextPageBtn.addListener(new ClickListener() {
            @Override
            public void clicked(InputEvent event, float x, float y) {
                int totalPages = getTotalPages();
                if (currentPage < totalPages - 1) {
                    currentPage++;
                    refreshRoomList();
                }
            }
        });

        pageInfoLabel = new Label("第1页/共1页", skin, "default_32");
        pageInfoLabel.getStyle().font.getData().setScale(1.5f);
        pageInfoLabel.getStyle().background = null;

        titleLabel = new Label("房间列表", skin);
        titleLabel.getStyle().font.getData().setScale(2.5f);
        titleLabel.setPosition(1050, 840);
        stage.addActor(titleLabel);

        mainTable.clear();
        float btnWidth = 300;
        float btnHeight = 120;
        float pad = 15;

        mainTable.add(refreshBtn).width(btnWidth).height(btnHeight).pad(pad);
        mainTable.row();
        mainTable.add(createBtn).width(btnWidth).height(btnHeight).pad(pad);
        mainTable.row();
        mainTable.add(backButton).width(btnWidth).height(btnHeight).pad(pad);
        mainTable.row();
        mainTable.setPosition(2100, 1200);

        prevPageBtn.setPosition(900, 80);
        pageInfoLabel.setPosition(1150, 95);
        nextPageBtn.setPosition(1500, 80);

        stage.addActor(prevPageBtn);
        stage.addActor(pageInfoLabel);
        stage.addActor(nextPageBtn);

        updatePageButtonStatus();

        requestRoomListFromServer();
    }

    private int getTotalPages() {
        if (allRooms.isEmpty()) return 0;
        return (int) Math.ceil((double) allRooms.size() / ROOMS_PER_PAGE);
    }

    private void refreshRoomList() {
        roomTable.clearChildren();

        if (allRooms.isEmpty()) {
            roomTable.add(new Label("未找到房间", skin, "default_32")).center().top();
            updatePageButtonStatus();
            return;
        }

        int startIndex = currentPage * ROOMS_PER_PAGE;
        int endIndex = Math.min(startIndex + ROOMS_PER_PAGE, allRooms.size());

        roomTable.clear();
        roomTable.top();
        roomTable.padTop(10).defaults().padBottom(20);

        for (int i = startIndex; i < endIndex; i++) {
            Message.RoomInfo room = allRooms.get(i);
            String status = room.getIsPlaying() ? " [游戏中]" : "";
            String text = String.format("%s (%d/%d)%s",
                    room.getRoomName(),
                    room.getCurrentPlayers(),
                    room.getMaxPlayers(),
                    status);

            TextButton btn = new TextButton(text, skin, "PapperButton");
            float btnWidth = 800;
            float btnHeight = 70;
            btn.setWidth(btnWidth);
            btn.setHeight(btnHeight);

            btn.addListener(new ClickListener() {
                @Override
                public void clicked(InputEvent event, float x, float y) {
                    joinRoom(room.getRoomId());
                }
            });

            roomTable.add(btn).width(btnWidth).height(btnHeight).center();
            roomTable.row();
        }

        updatePageButtonStatus();
    }

    private void updatePageButtonStatus() {
        int totalPages = getTotalPages();
        if (totalPages == 0) {
            pageInfoLabel.setText("暂无房间");
        } else {
            pageInfoLabel.setText(String.format("第%d页/共%d页", currentPage + 1, totalPages));
        }

        prevPageBtn.setDisabled(currentPage == 0);
        nextPageBtn.setDisabled(totalPages <= 1 || currentPage >= totalPages - 1);
    }

    private void requestRoomListFromServer() {
        try {
            game.getTcpClient().sendGetRoomList();
        } catch (IOException e) {
            Gdx.app.error("NET", "请求房间列表失败", e);
            if (errorPopup != null) {
                errorPopup.showError("无法加载房间列表");
            }
        }
    }

    public void onRoomListReceived(List<Message.RoomInfo> rooms) {
        Gdx.app.postRunnable(() -> {
            this.allRooms = new ArrayList<>(rooms);
            this.currentPage = 0;
            refreshRoomList();
        });
    }

    public void onCreateRoomResult(Message.S2C_CreateRoomResult result) {
        Gdx.app.postRunnable(() -> {
            if (result.getSuccess()) {
                Gdx.app.log("RoomList", "创建房间成功，等待进入房间 (roomId=" + result.getRoomId() + ")");
                return;
            }
            if (errorPopup != null) {
                String message = result.getMessageCreate();
                if (message == null || message.isBlank()) {
                    message = "创建房间失败，请稍后重试";
                }
                errorPopup.showError(message);
            }
        });
    }

    private void openCreateRoomDialog() {
        float targetX = 750;
        float targetY = 400;
        DropPopup dropPopup = new DropPopup(skin, "background/createRoomLong.png", targetX, targetY);

        Texture bgTex = new Texture(Gdx.files.internal("background/createRoomLong.png"));
        dropPopup.setSize(bgTex.getWidth(), bgTex.getHeight());
        bgTex.dispose();

        Group contentGroup = new Group();
        contentGroup.setSize(dropPopup.getWidth(), dropPopup.getHeight());

        Label popupTitle = new Label("创建房间", skin, "default");
        popupTitle.setPosition(300, 500);
        contentGroup.addActor(popupTitle);

        Label nameLabel = new Label("房间名", skin, "default_36");
        nameLabel.setPosition(230, 400);
        contentGroup.addActor(nameLabel);

        TextField nameField = new TextField("我的房间", skin, "TextField");
        nameField.setAlignment(Align.center);
        nameField.setPosition(400, 400);
        nameField.setSize(400, 60);
        contentGroup.addActor(nameField);

        Table peopleRow = new Table();
        peopleRow.setPosition(260, 300);
        peopleRow.setSize(600, 60);
        Label peopleLabel = new Label("人数:", skin, "default_36");
        peopleRow.add(peopleLabel).left().padRight(10);
        StepSlider stepSliderPeople = new StepSlider(skin, "default_huipu", "1人", "2人", "3人", "4人");
        stepSliderPeople.setSize(400, 50);
        peopleRow.add(stepSliderPeople).width(400).height(50).padRight(10);
        Label peopleValueLabel = new Label("1人", skin, "default_huipu");
        peopleValueLabel.setWidth(80);
        peopleValueLabel.setAlignment(Align.center);
        peopleRow.add(peopleValueLabel).width(80).left().padLeft(10);
        contentGroup.addActor(peopleRow);

        stepSliderPeople.addListener(new ChangeListener() {
            @Override
            public void changed(ChangeEvent event, Actor actor) {
                peopleValueLabel.setText(stepSliderPeople.getCurrentLabel());
            }
        });

        Table difficultyRow = new Table();
        difficultyRow.setPosition(265, 200);
        difficultyRow.setSize(600, 60);
        Label diffLabel = new Label("难度:", skin, "default_36");
        difficultyRow.add(diffLabel).left().padRight(10);
        StepSlider difficultySlider = new StepSlider(skin, "default_huipu", "简单", "普通", "困难", "炼狱");
        difficultySlider.setSize(400, 50);
        difficultyRow.add(difficultySlider).width(400).height(50).padRight(10);
        Label diffValueLabel = new Label("简单", skin, "default_huipu");
        difficultyRow.add(diffValueLabel).left();
        contentGroup.addActor(difficultyRow);

        difficultySlider.addListener(new ChangeListener() {
            @Override
            public void changed(ChangeEvent event, Actor actor) {
                diffValueLabel.setText(difficultySlider.getCurrentLabel());
            }
        });

        TextButton createBtn = new TextButton("创建", skin, "CreateButton");
        TextButton cancelBtn = new TextButton("取消", skin, "CreateButton");
        createBtn.setPosition(200, 50);
        createBtn.setSize(300, 100);
        cancelBtn.setPosition(580, 50);
        cancelBtn.setSize(300, 100);

        createBtn.addListener(new ChangeListener() {
            @Override
            public void changed(ChangeEvent event, Actor actor) {
                String roomName = nameField.getText().trim();
                int maxPlayers = stepSliderPeople.getCurrentStep() + 1;
                if (roomName.isEmpty()) {
                    errorPopup.showError("房间名不能为空");
                    return;
                }
                try {
                    game.getTcpClient().sendCreateRoom(roomName, maxPlayers);
                } catch (IOException e) {
                    errorPopup.showError("网络错误，请稍后再试");
                }
                dropPopup.hide();
            }
        });

        cancelBtn.addListener(new ChangeListener() {
            @Override
            public void changed(ChangeEvent event, Actor actor) {
                dropPopup.hide();
            }
        });

        contentGroup.addActor(createBtn);
        contentGroup.addActor(cancelBtn);

        dropPopup.addActor(contentGroup);
        stage.addActor(dropPopup);
        dropPopup.show();
    }

    private void joinRoom(int roomId) {
        try {
            game.getTcpClient().sendJoinRoom(roomId);
        } catch (IOException e) {
            errorPopup.showError("加入房间失败");
        }
    }

    @Override
    public void render(float delta) {
        Gdx.gl.glClearColor(0, 0, 0, 1);
        Gdx.gl.glClear(GL20.GL_COLOR_BUFFER_BIT);

        if (errorPopup != null) {
            errorPopup.update(delta);
        }

        stage.act(delta);
        stage.draw();
    }

    @Override
    public void resize(int width, int height) {
        stage.getViewport().update(width, height, true);
    }

    @Override
    public void pause() {}

    @Override
    public void resume() {}

    @Override
    public void hide() {
        if (errorPopup != null) {
            errorPopup.hide();
        }
    }

    @Override
    public void dispose() {
        hide();
        if (errorPopup != null) {
            errorPopup.dispose();
            errorPopup = null;
        }
        if (stage != null) {
            stage.dispose();
        }
        if (backgroundTexture != null) {
            backgroundTexture.dispose();
        }
    }
}
