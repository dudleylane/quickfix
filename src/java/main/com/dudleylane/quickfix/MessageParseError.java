package com.dudleylane.quickfix;

/** FIX::MessageParseError (Exceptions.h). */
public final class MessageParseError extends FIXException {
    private static final long serialVersionUID = 1L;

    public MessageParseError() {
        this("");
    }

    public MessageParseError(String detail) {
        super("Could not parse message", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static MessageParseError fromEngine(String type, String detail, int field, String version) {
        return new MessageParseError(type, detail, field, version);
    }

    MessageParseError(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
