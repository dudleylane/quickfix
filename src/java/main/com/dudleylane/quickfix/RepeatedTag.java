package com.dudleylane.quickfix;

/** FIX::RepeatedTag (Exceptions.h). */
public final class RepeatedTag extends FIXException {
    private static final long serialVersionUID = 1L;

    public RepeatedTag(int field) {
        this(field, "");
    }

    public RepeatedTag(int field, String detail) {
        super("Repeated tag not part of repeating group", detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static RepeatedTag fromEngine(String type, String detail, int field, String version) {
        return new RepeatedTag(type, detail, field, version);
    }

    RepeatedTag(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
