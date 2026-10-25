package com.dudleylane.quickfix;

/** FIX::FieldNotFound (Exceptions.h). */
public final class FieldNotFound extends FIXException {
    private static final long serialVersionUID = 1L;

    public FieldNotFound(int field) {
        this(field, "");
    }

    public FieldNotFound(int field, String detail) {
        super("Field not found: " + field, detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static FieldNotFound fromEngine(String type, String detail, int field, String version) {
        return new FieldNotFound(type, detail, field, version);
    }

    FieldNotFound(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
