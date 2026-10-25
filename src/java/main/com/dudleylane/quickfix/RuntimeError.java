package com.dudleylane.quickfix;

/** FIX::RuntimeError (Exceptions.h). */
public final class RuntimeError extends FIXException {
    private static final long serialVersionUID = 1L;

    public RuntimeError() {
        this("");
    }

    public RuntimeError(String detail) {
        super("Runtime error", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static RuntimeError fromEngine(String type, String detail, int field, String version) {
        return new RuntimeError(type, detail, field, version);
    }

    RuntimeError(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
