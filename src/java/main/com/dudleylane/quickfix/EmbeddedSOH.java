package com.dudleylane.quickfix;

/** FIX::EmbeddedSOH (Exceptions.h). */
public final class EmbeddedSOH extends FIXException {
    private static final long serialVersionUID = 1L;

    public EmbeddedSOH(int field) {
        this(field, "");
    }

    public EmbeddedSOH(int field, String detail) {
        super("Embedded SOH", detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static EmbeddedSOH fromEngine(String type, String detail, int field, String version) {
        return new EmbeddedSOH(type, detail, field, version);
    }

    EmbeddedSOH(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
