package com.dudleylane.quickfix;

/** FIX::IOException (Exceptions.h). */
public final class IOException extends FIXException {
    private static final long serialVersionUID = 1L;

    public IOException() {
        this("");
    }

    public IOException(String detail) {
        super("IO Error", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static IOException fromEngine(String type, String detail, int field, String version) {
        return new IOException(type, detail, field, version);
    }

    IOException(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
