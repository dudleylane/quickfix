package com.dudleylane.quickfix;

/** FIX::SocketSendFailed (Exceptions.h). */
public final class SocketSendFailed extends SocketException {
    private static final long serialVersionUID = 1L;

    public SocketSendFailed() {
        this("");
    }

    public SocketSendFailed(String detail) {
        super("Socket Error", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static SocketSendFailed fromEngine(String type, String detail, int field, String version) {
        return new SocketSendFailed(type, detail, field, version);
    }

    SocketSendFailed(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
