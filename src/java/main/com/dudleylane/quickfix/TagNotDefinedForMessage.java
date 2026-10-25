package com.dudleylane.quickfix;

/** FIX::TagNotDefinedForMessage (Exceptions.h). */
public final class TagNotDefinedForMessage extends FIXException {
    private static final long serialVersionUID = 1L;

    public TagNotDefinedForMessage(int field) {
        this(field, "");
    }

    public TagNotDefinedForMessage(int field, String detail) {
        super("Tag not defined for this message type: " + field, detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static TagNotDefinedForMessage fromEngine(String type, String detail, int field, String version) {
        return new TagNotDefinedForMessage(type, detail, field, version);
    }

    TagNotDefinedForMessage(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
