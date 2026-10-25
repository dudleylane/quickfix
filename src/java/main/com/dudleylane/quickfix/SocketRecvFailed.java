package com.dudleylane.quickfix;

/** FIX::SocketRecvFailed (Exceptions.h). */
public final class SocketRecvFailed extends SocketException {
    private static final long serialVersionUID = 1L;

    public SocketRecvFailed() {
        this("");
    }

    public SocketRecvFailed(String detail) {
        super("Socket Error", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static SocketRecvFailed fromEngine(String type, String detail, int field, String version) {
        return new SocketRecvFailed(type, detail, field, version);
    }

    SocketRecvFailed(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
