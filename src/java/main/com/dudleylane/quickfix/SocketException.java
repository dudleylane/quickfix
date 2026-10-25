package com.dudleylane.quickfix;

/** FIX::SocketException (Exceptions.h). */
public class SocketException extends FIXException {
    private static final long serialVersionUID = 1L;

    public SocketException() {
        this("");
    }

    public SocketException(String detail) {
        super("Socket Error", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static SocketException fromEngine(String type, String detail, int field, String version) {
        return new SocketException(type, detail, field, version);
    }

    protected SocketException(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
