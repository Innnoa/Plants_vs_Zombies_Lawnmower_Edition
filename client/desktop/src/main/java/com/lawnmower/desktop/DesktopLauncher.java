package com.lawnmower.desktop;

import com.badlogic.gdx.backends.lwjgl3.Lwjgl3Application;
import com.badlogic.gdx.backends.lwjgl3.Lwjgl3ApplicationConfiguration;

import com.lawnmower.Main;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class DesktopLauncher {
    private static final Logger log = LoggerFactory.getLogger(DesktopLauncher.class);

    public static void main(String[] args) {
        Lwjgl3ApplicationConfiguration config = new Lwjgl3ApplicationConfiguration();
        config.setTitle("LawnMower Game");
        config.setWindowedMode(1000, 563);
        config.setForegroundFPS(60);

        new Lwjgl3Application(new Main(), config);
    }
}
