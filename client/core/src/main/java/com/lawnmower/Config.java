package com.lawnmower;

import com.google.protobuf.ByteString;

public final class Config {

    // =============== 网络配置 ===============
    public static final String SERVER_HOST = "192.168.206.244";

    /** 服务器端口 */
    public static final int SERVER_PORT = 7777;

    // =============== 基础配置 ===============
    private static final String quit = "close_quit";
    public static final ByteString byteString = ByteString.copyFrom(quit.getBytes(java.nio.charset.StandardCharsets.UTF_8));
}
