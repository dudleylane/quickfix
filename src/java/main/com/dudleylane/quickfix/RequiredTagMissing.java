package com.dudleylane.quickfix;

/** FIX::RequiredTagMissing (Exceptions.h). */
public final class RequiredTagMissing extends FIXException {
    private static final long serialVersionUID = 1L;

    public RequiredTagMissing(int field) {
        this(field, "");
    }

    public RequiredTagMissing(int field, String detail) {
        super("Required tag missing: " + field, detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static RequiredTagMissing fromEngine(String type, String detail, int field, String version) {
        return new RequiredTagMissing(type, detail, field, version);
    }

    RequiredTagMissing(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
