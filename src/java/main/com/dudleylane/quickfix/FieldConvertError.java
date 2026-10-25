package com.dudleylane.quickfix;

/** FIX::FieldConvertError (Exceptions.h). */
public final class FieldConvertError extends FIXException {
    private static final long serialVersionUID = 1L;

    public FieldConvertError() {
        this("");
    }

    public FieldConvertError(String detail) {
        super("Could not convert field", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static FieldConvertError fromEngine(String type, String detail, int field, String version) {
        return new FieldConvertError(type, detail, field, version);
    }

    FieldConvertError(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
