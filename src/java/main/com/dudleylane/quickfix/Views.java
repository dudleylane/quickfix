package com.dudleylane.quickfix;

/**
 * The views the binding hands out of objects that a call from Java can destroy (#196). A view is a proxy that owns
 * nothing, pointing into another object, and it holds that object's proxy (#194); but a call can still destroy what
 * it points at -- clear(), setString(), removeGroup() or delete() on its message, and the rest -- and a call through
 * it then read and wrote freed memory. Each view is registered, by the address of its object, in quickfix.i's
 * QuickfixViews, and such a call first ends it: from then on a call through it throws NullPointerException.
 */
final class Views {
    private Views() {}

    private static native void register(Object view, long cPtr, boolean group);

    // For the tests, by reflection, once the views the GC has collected are dropped: whether view is registered, and
    // how many views are registered at cPtr.
    private static native boolean registered(Object view);

    private static native long countAt(long cPtr);

    /** A group's view: any message's groups can be destroyed from Java, so it is always registered. */
    static <T extends FieldMap> T group(T view, long cPtr) {
        register(view, cPtr, true);
        view.swigRegistered = true;
        return view;
    }

    /**
     * A view into owner -- its header, its trailer, or a generated class's view() of it: registered when a call from
     * Java can destroy owner, which is when owner's proxy owns its object or is itself a registered view. A
     * callback's message is the engine's, and its proxy is neither, so a view taken through it is not registered,
     * and lasts only as long as the callback (#199).
     */
    static <T extends FieldMap> T into(FieldMap owner, T view, long cPtr) {
        if (owner.swigCMemOwn || owner.swigRegistered) {
            register(view, cPtr, false);
            view.swigRegistered = true;
        }
        return view;
    }

    /** One of owner's dictionaries: registered when owner's proxy owns its object. */
    static Dictionary into(SessionSettings owner, Dictionary view, long cPtr) {
        if (owner.swigCMemOwn) {
            register(view, cPtr, false);
        }
        return view;
    }

    /**
     * QuickfixViews::endViews calls this, holding no lock, for each live view of an object a call is about to
     * destroy. A view owns nothing, so delete() only sets its pointer to 0. False if it did not.
     */
    static boolean end(Object view) {
        if (view instanceof FieldMap map) {
            map.delete();
            return FieldMap.getCPtr(map) == 0;
        }
        if (view instanceof Dictionary dictionary) {
            dictionary.delete();
            return Dictionary.getCPtr(dictionary) == 0;
        }
        return false;
    }
}
