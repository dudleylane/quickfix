package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;

/**
 * The child JVM of SynchronizedApplicationTests: an application wrapped in a SynchronizedApplication with no other
 * reference to it, then collected and finalized, then called through the wrapper. The wrapper keeps the application
 * (#208), so the calls reach it and the JVM survives; before, the application (held only by the director's weak
 * global reference) was collected, its finalizer deleted the native director, and the call used freed memory.
 */
public final class SynchronizedApplicationChild {
    private SynchronizedApplicationChild() {}

    static final class QuietApp extends Application {
        @Override public void onCreate(SessionID id) {}
        @Override public void onLogon(SessionID id) {}
        @Override public void onLogout(SessionID id) {}
        @Override public void toAdmin(Message m, SessionID id) {}
        @Override public void toApp(Message m, SessionID id) {}
        @Override public void fromAdmin(Message m, SessionID id) {}
        @Override public void fromApp(Message m, SessionID id) {}
    }

    public static void main(String[] args) throws Exception {
        // The only reference to the application is the one the wrapper keeps.
        SynchronizedApplication sync = new SynchronizedApplication(new QuietApp());
        for (int i = 0; i < 200; ++i) {
            System.gc();
            Thread.sleep(10);
        }
        SessionID sessionID = new SessionID("FIX.4.2", "SENDER", "TARGET");
        for (int i = 0; i < 1000; ++i) {
            sync.onLogon(sessionID);
            sync.fromApp(new Message(), sessionID);
        }
        System.out.println("end=true");
    }
}
