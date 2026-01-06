package com.lawnmower.network;

import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.MessageLite;
import com.lawnmower.Config;
import lawnmower.Message;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.util.Arrays;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

/**
 * 负责处理客户端到服务器的 UDP 通信，直接发送/接收 protobuf Packet。
 */
public class UdpClient {
    private static final Logger log = LoggerFactory.getLogger(UdpClient.class);

    private final Object sendLock = new Object();
    private final AtomicBoolean running = new AtomicBoolean(false);

    private DatagramSocket socket;
    private InetSocketAddress serverAddress;
    private Thread receiveThread;
    private Consumer<Message.Packet> packetConsumer = packet -> {};
    private Consumer<Throwable> errorConsumer = err -> {};

    /**
     * 初始化 UDP socket 并启动接收线程。
     */
    public synchronized void start(String host,
                                   int port,
                                   Consumer<Message.Packet> consumer) throws IOException {
        if (running.get()) {
            return;
        }
        Objects.requireNonNull(consumer, "packetConsumer");
        this.packetConsumer = consumer;
        this.serverAddress = new InetSocketAddress(host, port);
        this.socket = new DatagramSocket();
        this.socket.connect(serverAddress);
        this.socket.setSoTimeout(Config.UDP_RECEIVE_TIMEOUT_MS);
        running.set(true);

        receiveThread = new Thread(this::receiveLoop, "udp-recv");
        receiveThread.setDaemon(true);
        receiveThread.start();
        log.info("UDP socket bound to {} using remote {}", socket.getLocalPort(), serverAddress);
    }

    public synchronized void stop() {
        running.set(false);
        if (socket != null) {
            socket.close();
            socket = null;
        }
        if (receiveThread != null) {
            receiveThread.interrupt();
            receiveThread = null;
        }
    }

    public boolean isRunning() {
        return running.get();
    }

    public void setErrorConsumer(Consumer<Throwable> consumer) {
        this.errorConsumer = consumer != null ? consumer : err -> {};
    }

    public boolean sendPlayerInput(Message.C2S_PlayerInput input) {
        Message.Packet packet = Message.Packet.newBuilder()
                .setMsgType(Message.MessageType.MSG_C2S_PLAYER_INPUT)
                .setPayload(input.toByteString())
                .build();
        return sendPacket(packet);
    }

    public boolean sendPacket(Message.Packet packet) {
        if (packet == null || !running.get()) {
            return false;
        }
        byte[] payload = packet.toByteArray();
        DatagramPacket datagram = new DatagramPacket(payload, payload.length, serverAddress);
        synchronized (sendLock) {
            try {
                socket.send(datagram);
                return true;
            } catch (IOException e) {
                log.error("Failed to send UDP packet", e);
                errorConsumer.accept(e);
                return false;
            }
        }
    }

    private void receiveLoop() {
        byte[] buffer = new byte[Config.UDP_BUFFER_SIZE];
        DatagramPacket datagram = new DatagramPacket(buffer, buffer.length);
        while (running.get()) {
            try {
                socket.receive(datagram);
                handlePacket(Arrays.copyOf(datagram.getData(), datagram.getLength()));
            } catch (SocketTimeoutException timeout) {
                // just loop to keep the socket alive
            } catch (IOException e) {
                if (running.get()) {
                    log.warn("UDP receive error: {}", e.getMessage());
                    errorConsumer.accept(e);
                }
                break;
            }
        }
        running.set(false);
    }

    private void handlePacket(byte[] data) {
        if (data == null || data.length == 0) {
            return;
        }
        try {
            Message.Packet packet = Message.Packet.parseFrom(data);
            packetConsumer.accept(packet);
        } catch (InvalidProtocolBufferException e) {
            log.warn("Failed to parse UDP payload: {}", e.getMessage());
        }
    }

    /**
     * 将任意 payload 封装为 {@link Message.Packet} 后再发送，方便传输其它消息。
     */
    public boolean sendPayload(Message.MessageType type, MessageLite payload) {
        Message.Packet packet = Message.Packet.newBuilder()
                .setMsgType(type)
                .setPayload(payload.toByteString())
                .build();
        return sendPacket(packet);
    }
}
