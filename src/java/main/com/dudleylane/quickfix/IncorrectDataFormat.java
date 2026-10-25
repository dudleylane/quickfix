package com.dudleylane.quickfix;

/** FIX::IncorrectDataFormat (Exceptions.h). */
public final class IncorrectDataFormat extends FIXException {
    private static final long serialVersionUID = 1L;

    public IncorrectDataFormat(int field) {
        this(field, "");
    }

    public IncorrectDataFormat(int field, String detail) {
        super("Incorrect data format for value with tag: " + field, detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static IncorrectDataFormat fromEngine(String type, String detail, int field, String version) {
        return new IncorrectDataFormat(type, detail, field, version);
    }

    IncorrectDataFormat(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
