package com.dudleylane.quickfix;

/** FIX::IncorrectMessageStructure (Exceptions.h). */
public final class IncorrectMessageStructure extends FIXException {
    private static final long serialVersionUID = 1L;

    public IncorrectMessageStructure() {
        this("");
    }

    public IncorrectMessageStructure(String detail) {
        super("Incorrect message structure", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static IncorrectMessageStructure fromEngine(String type, String detail, int field, String version) {
        return new IncorrectMessageStructure(type, detail, field, version);
    }

    IncorrectMessageStructure(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
