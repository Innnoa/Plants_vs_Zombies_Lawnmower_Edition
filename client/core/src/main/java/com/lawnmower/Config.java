package com.lawnmower;

import com.google.protobuf.ByteString;

public final class Config {

    // =============== 网络配置 ===============
    public static final String SERVER_HOST = "192.168.1.11";

    /** 服务器端口 */
    public static final int SERVER_PORT = 7777;
    /**
     * UDP 服务端口（若服务端未单独开启则可以与 TCP 共用，默认保留独立端口便于调试）
     */
    public static final int SERVER_UDP_PORT = 7778;

    /** UDP 套接字缓冲区 */
    public static final int UDP_BUFFER_SIZE = 64 * 1024;
    /** UDP 握手重发间隔 */
    public static final long UDP_HELLO_RETRY_MS = 1000L;
    /** UDP 接收循环超时 */
    public static final int UDP_RECEIVE_TIMEOUT_MS = 500;

    // =============== 基础配置 ===============
    private static final String quit = "close_quit";
    public static final ByteString byteString = ByteString.copyFrom(quit.getBytes(java.nio.charset.StandardCharsets.UTF_8));
}
