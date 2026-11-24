package com.dudleylane.quickfix;

import java.lang.ref.ReferenceQueue;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.function.LongSupplier;

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

    // What a transport hands out of itself (#197): a Session from lookupSession, registerSession or getSession, a
    // session's store and log, and the transport's log. Each view holds its owner in swigOwner -- a session's view
    // the transport that made the session, a store's or log's view the session's view, a transport's log's view the
    // transport -- so the GC never finalizes a transport a view can reach; and each is kept here, apart from the
    // views above, by the address of what it views. A transport's delete() -- which close() and its finalizer reach
    // -- ends the views of what its destructor frees, before and after it runs, and a constructor that throws ends
    // those of the sessions it had made. Rare, so a walk of every view kept; registering one is what is frequent.

    /** A kept view: weak, so a view the GC collects leaves the set. session is its session's address, or 0. */
    private static final class Kept extends WeakReference<Object> {
        final long address;
        final long session;

        Kept(Object view, long address, long session) {
            super(view, collected);
            this.address = address;
            this.session = session;
        }
    }

    private static final Object transportLock = new Object();
    private static final ReferenceQueue<Object> collected = new ReferenceQueue<>();
    // Guarded by transportLock: the views kept, and each session's transport, by the session's address.
    private static final HashSet<Kept> kept = new HashSet<>();
    private static final HashMap<Long, WeakReference<Object>> owners = new HashMap<>();

    private static native long[] addressesOf(long transport, boolean initiator);

    private static native long[] sessionsOf(long transport, boolean initiator);

    private static native long[] liveSessions();

    // Holding transportLock: drops the views the GC has collected.
    private static void drain() {
        for (Object gone; (gone = collected.poll()) != null;) {
            kept.remove(gone);
        }
    }

    /**
     * A session's view; owner is the transport that made it, or null for a static lookup's, which finds it here --
     * under the lock constructed() takes, so that one of the two gives a view taken during construction its owner.
     */
    static Session session(Session view, long cPtr, Object owner) {
        synchronized (transportLock) {
            if (owner == null) {
                WeakReference<Object> transport = owners.get(cPtr);
                owner = transport == null ? null : transport.get();
            }
            view.swigOwner = owner;
            drain();
            kept.add(new Kept(view, cPtr, cPtr));
        }
        return view;
    }

    /** A log's view: of a session's log, owned by the session's view, or of a transport's, owned by the transport. */
    static Log log(Log view, long cPtr, Object owner) {
        view.swigOwner = owner;
        long session = owner instanceof Session ownerSession ? Session.getCPtr(ownerSession) : 0;
        synchronized (transportLock) {
            drain();
            kept.add(new Kept(view, cPtr, session));
        }
        return view;
    }

    /** A session's store's view, owned by the session's view. */
    static MessageStore store(MessageStore view, long cPtr, Session owner) {
        view.swigOwner = owner;
        synchronized (transportLock) {
            drain();
            kept.add(new Kept(view, cPtr, Session.getCPtr(owner)));
        }
        return view;
    }

    private static void endView(Object view) {
        if (view instanceof Session session) {
            session.delete();
            session.swigOwner = null;
        } else if (view instanceof Log log) {
            log.delete();
            log.swigOwner = null;
        } else if (view instanceof MessageStore store) {
            store.delete();
            store.swigOwner = null;
        }
    }

    /** Ends the views at addresses, sorted, or of sessions not in live, sorted: views no longer kept. */
    private static void endViews(long[] addresses, long[] live) {
        ArrayList<Object> ending = new ArrayList<>();
        synchronized (transportLock) {
            for (var i = kept.iterator(); i.hasNext();) {
                Kept view = i.next();
                boolean ends = addresses != null ? Arrays.binarySearch(addresses, view.address) >= 0
                                                 : view.session != 0 && Arrays.binarySearch(live, view.session) < 0;
                if (ends) {
                    i.remove();
                    Object object = view.get();
                    if (object != null) {
                        ending.add(object);
                    }
                }
            }
        }
        // A view's delete() takes the view's own lock: not under transportLock.
        for (Object view : ending) {
            endView(view);
        }
    }

    /** A transport's constructor: one that throws has freed the sessions it made, and ends their views. */
    static long constructing(LongSupplier make) {
        try {
            return make.getAsLong();
        } catch (RuntimeException | Error e) {
            try {
                long[] live = liveSessions();
                Arrays.sort(live);
                endViews(null, live);
            } catch (RuntimeException | Error suppressed) {
                e.addSuppressed(suppressed);
            }
            throw e;
        }
    }

    /** A transport made: its sessions' views hold it from now on, the ones onCreate handed out before it existed too. */
    static void constructed(Initiator transport) {
        constructed(transport, sessionsOf(Initiator.getCPtr(transport), true));
    }

    static void constructed(Acceptor transport) {
        constructed(transport, sessionsOf(Acceptor.getCPtr(transport), false));
    }

    private static void constructed(Object transport, long[] sessions) {
        Arrays.sort(sessions);
        synchronized (transportLock) {
            for (long session : sessions) {
                owners.put(session, new WeakReference<>(transport));
            }
            for (Kept view : kept) {
                if (view.address == view.session && Arrays.binarySearch(sessions, view.session) >= 0
                    && view.get() instanceof Session session && session.swigOwner == null) {
                    session.swigOwner = transport;
                }
            }
        }
    }

    /** A transport's delete(), before its destructor runs: ends the views of what that frees, and returns them. */
    static long[] teardown(Initiator transport) {
        return teardown(transport, addressesOf(Initiator.getCPtr(transport), true));
    }

    static long[] teardown(Acceptor transport) {
        return teardown(transport, addressesOf(Acceptor.getCPtr(transport), false));
    }

    private static long[] teardown(Object transport, long[] freed) {
        Arrays.sort(freed);
        synchronized (transportLock) {
            for (long address : freed) {
                WeakReference<Object> owner = owners.get(address);
                Object held = owner == null ? null : owner.get();
                if (owner != null && (held == transport || held == null)) {
                    owners.remove(address);
                }
            }
        }
        endViews(freed, null);
        return freed;
    }

    /** After the destructor: a script's store or log factory runs inside it, and can hand out another view. */
    static void endTransportViews(long[] freed) {
        endViews(freed, null);
    }

    // For the tests, by reflection: how many views of transports are kept, once the GC's are dropped.
    private static int transportViewCount() {
        synchronized (transportLock) {
            drain();
            return kept.size();
        }
    }
}
