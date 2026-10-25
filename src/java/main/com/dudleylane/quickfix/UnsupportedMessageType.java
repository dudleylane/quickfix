package com.dudleylane.quickfix;

/** FIX::UnsupportedMessageType (Exceptions.h). */
public final class UnsupportedMessageType extends FIXException {
    private static final long serialVersionUID = 1L;

    public UnsupportedMessageType() {
        this("");
    }

    public UnsupportedMessageType(String detail) {
        super("Unsupported Message Type", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static UnsupportedMessageType fromEngine(String type, String detail, int field, String version) {
        return new UnsupportedMessageType(type, detail, field, version);
    }

    UnsupportedMessageType(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
