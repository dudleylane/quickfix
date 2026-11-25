package com.dudleylane.quickfix;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.LongSupplier;

/**
 * The store and log factories each transport was made with, by its native address, from its construction until
 * delete() has freed it (#201). Its destructor destroys its sessions' stores and logs, and its own log, through
 * them, so the GC must not finalize them first, as it did when it found the transport and them unreachable together.
 * The constructor's JNI call records them as it evaluates its arguments (argument(), from the javain typemaps), in a
 * frame of this thread's own, since onCreate can make another transport inside the call; constructing() pins what
 * was recorded under the address the call returns. A constructor that throws pins nothing. The application is not
 * pinned: one that refers to its transport would never be collected, nor would the transport (#204).
 */
final class TransportArguments {
    private static final Map<Long, Object[]> PINNED = new ConcurrentHashMap<>();
    private static final ThreadLocal<ArrayDeque<List<Object>>> FRAMES = ThreadLocal.withInitial(ArrayDeque::new);

    private TransportArguments() {}

    /** A factory passed to a transport's constructor; outside one it records nothing. */
    static <T> T argument(T value) {
        List<Object> frame = FRAMES.get().peek();
        if (frame != null && value != null) {
            frame.add(value);
        }
        return value;
    }

    /** Runs a transport's constructor call, and pins what it was passed under the address it returns. */
    static long constructing(LongSupplier construct) {
        ArrayDeque<List<Object>> frames = FRAMES.get();
        List<Object> frame = new ArrayList<>(2);
        frames.push(frame);
        long cPtr;
        try {
            cPtr = construct.getAsLong();
        } finally {
            frames.pop();
        }
        if (cPtr != 0 && !frame.isEmpty()) {
            PINNED.put(cPtr, frame.toArray());
        }
        return cPtr;
    }

    /**
     * Takes a transport's factories out before delete() frees it, so that a transport made at the same address
     * meanwhile keeps its own; the caller holds the result until the free has returned.
     */
    static Object take(long cPtr) {
        return PINNED.remove(cPtr);
    }
}
