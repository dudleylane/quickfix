package com.dudleylane.quickfix.test;

import java.util.HashMap;
import java.util.Map;

/**
 * A transport dropped without close() with the store and log factories it was made with, so that the GC finalizes
 * them together, in its own order: one finalized after a factory used it from its destructor (#201). The factories are
 * pinned until the transport's native object has been deleted. Each case runs TransportGcChild in a JVM of its own,
 * so that a crash is a failure here.
 */
public class TransportGcTests {
    static Map<String, String> collected(String mode) throws Exception {
        Map<String, String> lines = run(mode);
        // Held from the transport's construction until its native object was deleted, and only until then.
        Check.equal("1", lines.get("pinned"), mode + ": pinned with one transport alive");
        Check.equal("0", lines.get("pins"), mode + ": pinned once all were finalized");
        // Deleted, so their sessions are unregistered.
        Check.equal("0", lines.get("registered"), mode + ": sessions still registered");
        return lines;
    }

    /**
     * What the pins cost: a factory that refers to its transport, directly or through its application, keeps it, so
     * the transport is never finalized and its sessions stay registered. The JVM still exits cleanly.
     */
    static void kept(String mode) throws Exception {
        Map<String, String> lines = run(mode);
        Check.equal("1", lines.get("pinned"), mode + ": pinned with one transport alive");
        Check.equal(Integer.toString(TransportGcChild.COUNT + 1), lines.get("pins"), mode + ": still pinned");
        Check.equal(Integer.toString(TransportGcChild.COUNT + 1), lines.get("registered"), mode + ": still registered");
    }

    static Map<String, String> run(String mode) throws Exception {
        CallbackErrorTests.Result result = CallbackErrorTests.child(TransportGcChild.class, mode);
        Check.equal(0, result.status(), mode + ": status; output:\n" + result.output());
        Map<String, String> lines = new HashMap<>();
        for (String line : result.output().split("\n")) {
            int equals = line.indexOf('=');
            if (equals > 0) {
                lines.put(line.substring(0, equals), line.substring(equals + 1));
            }
        }
        Check.equal("true", lines.get("end"), mode + ": got to the end; output:\n" + result.output());
        return lines;
    }

    public void testAcceptor() throws Exception {
        collected("acceptor");
    }

    public void testInitiator() throws Exception {
        collected("initiator");
    }

    public void testASubclassOfTheBaseThatKeepsNothing() throws Exception {
        collected("base");
    }

    public void testStartedAndStopped() throws Exception {
        collected("stopped");
    }

    public void testALogFactoryThatRefersToItsTransportKeepsIt() throws Exception {
        kept("factorycycle");
    }

    public void testALogFactoryThatRefersToItsApplicationKeepsTheTransport() throws Exception {
        kept("appfactorycycle");
    }
}
