package com.dudleylane.quickfix;

/** FIX::InvalidMessage (Exceptions.h). */
public final class InvalidMessage extends FIXException {
    private static final long serialVersionUID = 1L;

    public InvalidMessage() {
        this("");
    }

    public InvalidMessage(String detail) {
        super("Invalid message", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static InvalidMessage fromEngine(String type, String detail, int field, String version) {
        return new InvalidMessage(type, detail, field, version);
    }

    InvalidMessage(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
