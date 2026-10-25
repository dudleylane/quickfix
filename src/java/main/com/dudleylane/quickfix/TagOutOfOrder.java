package com.dudleylane.quickfix;

/** FIX::TagOutOfOrder (Exceptions.h). */
public final class TagOutOfOrder extends FIXException {
    private static final long serialVersionUID = 1L;

    public TagOutOfOrder(int field) {
        this(field, "");
    }

    public TagOutOfOrder(int field, String detail) {
        super("Tag specified out of required order", detail, field, "");
    }

    /** The tag the exception is about. */
    public int getField() {
        return field;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static TagOutOfOrder fromEngine(String type, String detail, int field, String version) {
        return new TagOutOfOrder(type, detail, field, version);
    }

    TagOutOfOrder(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
