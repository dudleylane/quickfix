package com.dudleylane.quickfix.test;

import java.util.Objects;
import java.util.function.BooleanSupplier;

/** Assertions for the suites. */
final class Check {
    private Check() {}

    static void isTrue(boolean condition, String what) {
        if (!condition) {
            throw new AssertionError(what);
        }
    }

    static void equal(Object expected, Object actual, String what) {
        if (!Objects.equals(expected, actual)) {
            throw new AssertionError(what + ": expected <" + expected + "> but was <" + actual + ">");
        }
    }

    interface Action {
        void run() throws Exception;
    }

    /** Runs action and returns what it threw, which must be a type. */
    static <T extends Throwable> T raises(Class<T> type, Action action) {
        try {
            action.run();
        } catch (Throwable t) {
            if (type.isInstance(t)) {
                return type.cast(t);
            }
            throw new AssertionError("expected " + type.getSimpleName() + " but got " + t, t);
        }
        throw new AssertionError("expected " + type.getSimpleName() + " but nothing was thrown");
    }

    /** Waits up to seconds for condition. */
    static boolean eventually(double seconds, BooleanSupplier condition) throws InterruptedException {
        long deadline = System.nanoTime() + (long) (seconds * 1e9);
        while (System.nanoTime() < deadline) {
            if (condition.getAsBoolean()) {
                return true;
            }
            Thread.sleep(10);
        }
        return condition.getAsBoolean();
    }
}
