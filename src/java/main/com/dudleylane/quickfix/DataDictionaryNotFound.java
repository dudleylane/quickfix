package com.dudleylane.quickfix;

/** FIX::DataDictionaryNotFound (Exceptions.h). */
public final class DataDictionaryNotFound extends FIXException {
    private static final long serialVersionUID = 1L;

    public DataDictionaryNotFound(String version) {
        this(version, "");
    }

    public DataDictionaryNotFound(String version, String detail) {
        super("Could not find data dictionary", detail, 0, version);
    }

    /** The BeginString or ApplVerID no dictionary was found for. */
    public String getVersion() {
        return version;
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static DataDictionaryNotFound fromEngine(String type, String detail, int field, String version) {
        return new DataDictionaryNotFound(type, detail, field, version);
    }

    DataDictionaryNotFound(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
