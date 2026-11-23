package com.dudleylane.quickfix;

/** What every pointer argument goes through on its way to the engine (quickfix.i's javain for SWIGTYPE *). */
final class Proxies {
    private Proxies() {}

    /**
     * proxy's C++ pointer: 0 for null, the null pointer the engine expects, and a NullPointerException for a proxy
     * whose object was deleted, or a view whose object the binding has destroyed. Both have a pointer of 0, which
     * the engine took for null: setString read such a dictionary as none.
     */
    static long cPtr(long cPtr, Object proxy) {
        if (cPtr == 0 && proxy != null) {
            throw new NullPointerException(proxy.getClass().getSimpleName() + ": its object was deleted, or the view ended");
        }
        return cPtr;
    }
}
