package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * A SessionID is equal by value, as the engine compares: by the session's string (#190). Each was a distinct
 * object, equal only to itself, so a callback's never equalled one kept from another callback or made by hand.
 */
public class SessionIDTests {
    public void testEqualsAndHashCodeByValue() {
        SessionID a = new SessionID("FIX.4.2", "A", "B");
        SessionID b = new SessionID("FIX.4.2", "A", "B");
        Check.isTrue(a.equals(b) && b.equals(a), "two SessionIDs naming one session are equal");
        Check.equal(a.hashCode(), b.hashCode(), "their hash codes");
        Check.isTrue(!a.equals(new SessionID("FIX.4.2", "A", "C")), "another TargetCompID");
        Check.isTrue(!a.equals(new SessionID("FIX.4.2", "A", "B", "QUALIFIER")), "another qualifier");
        Check.isTrue(!a.equals(null) && !a.equals("FIX.4.2:A->B"), "null or another type");
    }

    public void testAsAKeyAndInACollection() {
        SessionID a = new SessionID("FIX.4.2", "A", "B");
        SessionID b = new SessionID("FIX.4.2", "A", "B");
        Map<SessionID, String> map = new HashMap<>();
        map.put(a, "session");
        Check.equal("session", map.get(b), "a HashMap key");
        Set<SessionID> set = new HashSet<>(List.of(a));
        Check.isTrue(set.contains(b), "a HashSet member");
        List<SessionID> list = new ArrayList<>(List.of(a));
        Check.isTrue(list.contains(b) && list.remove(b) && list.isEmpty(), "List.contains and List.remove");
    }

    public void testAHandedSessionIDEqualsOneMadeByHand() throws Exception {
        LifetimeTests.Keeper application = new LifetimeTests.Keeper();
        SessionSettings settings = Fixture.initiatorSettings(Fixture.freePort(), "SERVER", "EQUAL", false);
        SessionID id = new SessionID("FIX.4.2", "EQUAL", "SERVER");
        // Never started: making it makes its session, which calls onCreate, and closing it frees the session.
        AutoCloseable initiator = Fixture.initiator("Socket", application, new MemoryStoreFactory(), settings, null);
        try {
            Check.equal(List.of(id), application.handed.get("onCreate"), "onCreate's SessionID");
            Check.equal(id, Session.lookupSession(id).getSessionID(), "Session.getSessionID()");
            Map<SessionID, String> map = new HashMap<>();
            map.put(application.handed.get("onCreate").get(0), "session");
            Check.equal("session", map.get(id), "onCreate's SessionID as a HashMap key");
        } finally {
            initiator.close();
        }
    }
}
