package com.dudleylane.quickfix;

/** FIX::UnsupportedVersion (Exceptions.h). */
public final class UnsupportedVersion extends FIXException {
    private static final long serialVersionUID = 1L;

    public UnsupportedVersion() {
        this("");
    }

    public UnsupportedVersion(String detail) {
        super("Unsupported Version", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static UnsupportedVersion fromEngine(String type, String detail, int field, String version) {
        return new UnsupportedVersion(type, detail, field, version);
    }

    UnsupportedVersion(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
