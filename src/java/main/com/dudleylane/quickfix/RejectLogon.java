package com.dudleylane.quickfix;

/** FIX::RejectLogon (Exceptions.h). */
public final class RejectLogon extends FIXException {
    private static final long serialVersionUID = 1L;

    public RejectLogon() {
        this("");
    }

    public RejectLogon(String detail) {
        super("Rejected Logon Attempt", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static RejectLogon fromEngine(String type, String detail, int field, String version) {
        return new RejectLogon(type, detail, field, version);
    }

    RejectLogon(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
