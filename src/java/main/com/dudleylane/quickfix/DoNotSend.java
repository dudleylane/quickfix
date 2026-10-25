package com.dudleylane.quickfix;

/** FIX::DoNotSend (Exceptions.h). */
public final class DoNotSend extends FIXException {
    private static final long serialVersionUID = 1L;

    public DoNotSend() {
        this("");
    }

    public DoNotSend(String detail) {
        super("Do Not Send Message", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static DoNotSend fromEngine(String type, String detail, int field, String version) {
        return new DoNotSend(type, detail, field, version);
    }

    DoNotSend(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
