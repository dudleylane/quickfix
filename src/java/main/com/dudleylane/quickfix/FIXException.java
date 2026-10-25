package com.dudleylane.quickfix;

/**
 * FIX::Exception (Exceptions.h): the base of every exception the engine throws into Java, and of those a
 * callback throws to it. Unchecked, as in the C++ API, which declares none.
 */
public class FIXException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    private final String type;
    // Read by the native code (quickfix.i): what a callback's exception carries back into the engine.
    private final String detail;
    protected final int field;
    protected final String version;

    public FIXException(String type, String detail) {
        this(type, detail, 0, "");
    }

    protected FIXException(String type, String detail, int field, String version) {
        super(detail == null || detail.isEmpty() ? type : type + ": " + detail);
        this.type = type;
        this.detail = detail == null ? "" : detail;
        this.field = field;
        this.version = version == null ? "" : version;
    }

    /** The exception's kind, as the engine words it ("Field not found: 55"). */
    public String getType() {
        return type;
    }

    /** What the thrower added, or "". */
    public String getDetail() {
        return detail;
    }

    static FIXException fromEngine(String type, String detail, int field, String version) {
        return new FIXException(type, detail, field, version);
    }
}
