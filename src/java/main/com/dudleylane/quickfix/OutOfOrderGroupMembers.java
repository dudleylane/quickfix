package com.dudleylane.quickfix;

/** FIX::OutOfOrderGroupMembers (Exceptions.h). */
public final class OutOfOrderGroupMembers extends FIXException {
    private static final long serialVersionUID = 1L;

    public OutOfOrderGroupMembers(int field) {
        this(field, "");
    }

    public OutOfOrderGroupMembers(int field, String detail) {
        super("Out of order repeating group members", detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static OutOfOrderGroupMembers fromEngine(String type, String detail, int field, String version) {
        return new OutOfOrderGroupMembers(type, detail, field, version);
    }

    OutOfOrderGroupMembers(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
