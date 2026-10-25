package com.dudleylane.quickfix;

/** FIX::IncorrectTagValue (Exceptions.h). */
public final class IncorrectTagValue extends FIXException {
    private static final long serialVersionUID = 1L;

    public IncorrectTagValue(int field) {
        this(field, "");
    }

    public IncorrectTagValue(int field, String detail) {
        super("Value is incorrect (out of range) for this tag: " + field, detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static IncorrectTagValue fromEngine(String type, String detail, int field, String version) {
        return new IncorrectTagValue(type, detail, field, version);
    }

    IncorrectTagValue(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
