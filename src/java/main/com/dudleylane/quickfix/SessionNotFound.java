package com.dudleylane.quickfix;

/** FIX::SessionNotFound (Exceptions.h). */
public final class SessionNotFound extends FIXException {
    private static final long serialVersionUID = 1L;

    public SessionNotFound() {
        this("");
    }

    public SessionNotFound(String detail) {
        super("Session Not Found", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static SessionNotFound fromEngine(String type, String detail, int field, String version) {
        return new SessionNotFound(type, detail, field, version);
    }

    SessionNotFound(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
