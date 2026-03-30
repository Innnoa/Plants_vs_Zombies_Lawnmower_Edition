package com.lawnmower;

import com.google.protobuf.ByteString;

import java.util.ArrayList;
import java.util.Arrays;

public final class Config {
    private static final String SERVER_HOST_PROPERTY = "lawnmower.server.host";
    private static final String SERVER_PORT_PROPERTY = "lawnmower.server.tcpPort";
    private static final String SERVER_UDP_PORT_PROPERTY = "lawnmower.server.udpPort";
    private static final String SERVER_HOST_ENV = "LAWNMOWER_SERVER_HOST";
    private static final String SERVER_PORT_ENV = "LAWNMOWER_SERVER_TCP_PORT";
    private static final String SERVER_UDP_PORT_ENV = "LAWNMOWER_SERVER_UDP_PORT";
    private static final String DEFAULT_SERVER_HOST = "192.168.1.13";
    private static final int DEFAULT_SERVER_PORT = 7777;
    private static final int DEFAULT_SERVER_UDP_PORT = 7778;
    private static final NetworkTarget NETWORK_TARGET = loadNetworkTarget();

    // =============== 网络配置 ===============
    public static final String SERVER_HOST = NETWORK_TARGET.host;

    /** 服务器端口 */
    public static final int SERVER_PORT = NETWORK_TARGET.tcpPort;
    /**
     * UDP 服务端口（若服务端未单独开启则可以与 TCP 共用，默认保留独立端口便于调试）
     */
    public static final int SERVER_UDP_PORT = NETWORK_TARGET.udpPort;

    /** UDP 套接字缓冲区 */
    public static final int UDP_BUFFER_SIZE = 64 * 1024;
    /** UDP 握手重发间隔 */
    public static final long UDP_HELLO_RETRY_MS = 1000L;
    /** UDP 接收循环超时 */
    public static final int UDP_RECEIVE_TIMEOUT_MS = 500;

    // =============== 基础配置 ===============
    private static final String quit = "close_quit";
    public static final ByteString byteString = ByteString.copyFrom(quit.getBytes(java.nio.charset.StandardCharsets.UTF_8));

    //================ 道具类型 ===============
    public static final ArrayList<String> PROP_CONFIG = new ArrayList<>(Arrays.asList("background/Fertilizer.png"));

    //================ 升级配置 ===============
    public static final ArrayList<String> UPGRADE_LEVEL_CONFIG = new ArrayList<>(Arrays.asList("unknown","low","medium","high"));
    public static final ArrayList<String> UPGRADE_CONFIG = new ArrayList<>(Arrays.asList("unknown","move_speed","attack","attack_speed","max_health","critical_rate"));

    private Config() {
    }

    public static String describeNetworkTarget() {
        return SERVER_HOST + " (tcp=" + SERVER_PORT + ", udp=" + SERVER_UDP_PORT + ")";
    }

    private static NetworkTarget loadNetworkTarget() {
        String host = resolveStringSetting(
                SERVER_HOST_PROPERTY,
                SERVER_HOST_ENV,
                DEFAULT_SERVER_HOST);
        int tcpPort = resolvePortSetting(
                SERVER_PORT_PROPERTY,
                SERVER_PORT_ENV,
                DEFAULT_SERVER_PORT);
        int udpPort = resolvePortSetting(
                SERVER_UDP_PORT_PROPERTY,
                SERVER_UDP_PORT_ENV,
                DEFAULT_SERVER_UDP_PORT);
        return new NetworkTarget(host, tcpPort, udpPort);
    }

    private static String resolveStringSetting(String propertyName,
                                               String envName,
                                               String defaultValue) {
        String value = System.getProperty(propertyName);
        if (value != null && !value.isBlank()) {
            return value.trim();
        }

        value = System.getenv(envName);
        if (value != null && !value.isBlank()) {
            return value.trim();
        }

        return defaultValue;
    }

    private static int resolvePortSetting(String propertyName,
                                          String envName,
                                          int defaultValue) {
        String rawValue = System.getProperty(propertyName);
        String sourceName = propertyName;
        if (rawValue == null || rawValue.isBlank()) {
            rawValue = System.getenv(envName);
            sourceName = envName;
        }
        if (rawValue == null || rawValue.isBlank()) {
            return defaultValue;
        }

        try {
            int port = Integer.parseInt(rawValue.trim());
            if (port <= 0 || port > 65535) {
                warnInvalidPort(sourceName, rawValue, defaultValue);
                return defaultValue;
            }
            return port;
        } catch (NumberFormatException e) {
            warnInvalidPort(sourceName, rawValue, defaultValue);
            return defaultValue;
        }
    }

    private static void warnInvalidPort(String sourceName,
                                        String rawValue,
                                        int defaultValue) {
        System.err.println(
                "Invalid port from " + sourceName + ": " + rawValue
                        + ", fallback to " + defaultValue);
    }

    private static final class NetworkTarget {
        private final String host;
        private final int tcpPort;
        private final int udpPort;

        private NetworkTarget(String host, int tcpPort, int udpPort) {
            this.host = host;
            this.tcpPort = tcpPort;
            this.udpPort = udpPort;
        }
    }
}
