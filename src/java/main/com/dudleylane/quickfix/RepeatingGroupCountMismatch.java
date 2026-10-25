package com.dudleylane.quickfix;

/** FIX::RepeatingGroupCountMismatch (Exceptions.h). */
public final class RepeatingGroupCountMismatch extends FIXException {
    private static final long serialVersionUID = 1L;

    public RepeatingGroupCountMismatch(int field) {
        this(field, "");
    }

    public RepeatingGroupCountMismatch(int field, String detail) {
        super("Repeating group count mismatch", detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static RepeatingGroupCountMismatch fromEngine(String type, String detail, int field, String version) {
        return new RepeatingGroupCountMismatch(type, detail, field, version);
    }

    RepeatingGroupCountMismatch(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
