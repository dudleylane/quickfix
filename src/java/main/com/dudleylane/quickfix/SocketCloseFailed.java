package com.dudleylane.quickfix;

/** FIX::SocketCloseFailed (Exceptions.h). */
public final class SocketCloseFailed extends SocketException {
    private static final long serialVersionUID = 1L;

    public SocketCloseFailed() {
        this("");
    }

    public SocketCloseFailed(String detail) {
        super("Socket Error", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static SocketCloseFailed fromEngine(String type, String detail, int field, String version) {
        return new SocketCloseFailed(type, detail, field, version);
    }

    SocketCloseFailed(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
