package com.dudleylane.quickfix;

/** FIX::DuplicateFieldNumber (Exceptions.h). */
public final class DuplicateFieldNumber extends FIXException {
    private static final long serialVersionUID = 1L;

    public DuplicateFieldNumber() {
        this("");
    }

    public DuplicateFieldNumber(String detail) {
        super("Duplicate field number", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static DuplicateFieldNumber fromEngine(String type, String detail, int field, String version) {
        return new DuplicateFieldNumber(type, detail, field, version);
    }

    DuplicateFieldNumber(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
