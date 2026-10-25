package com.dudleylane.quickfix;

/** FIX::InvalidTagNumber (Exceptions.h). */
public final class InvalidTagNumber extends FIXException {
    private static final long serialVersionUID = 1L;

    public InvalidTagNumber(int field) {
        this(field, "");
    }

    public InvalidTagNumber(int field, String detail) {
        super("Invalid tag number: " + field, detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static InvalidTagNumber fromEngine(String type, String detail, int field, String version) {
        return new InvalidTagNumber(type, detail, field, version);
    }

    InvalidTagNumber(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
