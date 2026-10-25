package com.dudleylane.quickfix;

/** FIX::InvalidMessageType (Exceptions.h). */
public final class InvalidMessageType extends FIXException {
    private static final long serialVersionUID = 1L;

    public InvalidMessageType() {
        this("");
    }

    public InvalidMessageType(String detail) {
        super("Invalid Message Type", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static InvalidMessageType fromEngine(String type, String detail, int field, String version) {
        return new InvalidMessageType(type, detail, field, version);
    }

    InvalidMessageType(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
