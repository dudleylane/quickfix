package com.dudleylane.quickfix;

/** FIX::ConfigError (Exceptions.h). */
public final class ConfigError extends FIXException {
    private static final long serialVersionUID = 1L;

    public ConfigError() {
        this("");
    }

    public ConfigError(String detail) {
        super("Configuration failed", detail, 0, "");
    }

    /** Made by the native code for an exception the engine threw; type and detail are the engine's. */
    static ConfigError fromEngine(String type, String detail, int field, String version) {
        return new ConfigError(type, detail, field, version);
    }

    ConfigError(String type, String detail, int field, String version) {
        super(type, detail, field, version);
    }
}
