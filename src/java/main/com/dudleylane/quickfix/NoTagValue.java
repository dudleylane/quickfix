package com.dudleylane.quickfix;

/** FIX::NoTagValue (Exceptions.h). */
public final class NoTagValue extends FIXException {
    private static final long serialVersionUID = 1L;

    public NoTagValue(int field) {
        this(field, "");
    }

    public NoTagValue(int field, String detail) {
        super("Tag specified without a value: " + field, detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static NoTagValue fromEngine(String type, String detail, int field, String version) {
        return new NoTagValue(type, detail, field, version);
    }

    NoTagValue(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
