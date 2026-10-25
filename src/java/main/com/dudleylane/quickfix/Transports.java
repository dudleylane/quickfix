package com.dudleylane.quickfix;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * The transports that are running. A running transport's engine threads call into its Application, its
 * store factory and its LogFactory, so the GC must not finalize it or them, and the JVM must not exit
 * under it: each is held here from start() or block() until stop(), and a shutdown hook stops those still
 * running. The hook waits for each to stop, so a callback must not call System.exit (see Application).
 */
final class Transports {
    private static final Set<Object> running = ConcurrentHashMap.newKeySet();

    static {
        Runtime.getRuntime().addShutdownHook(new Thread(Transports::stopAll, "quickfix shutdown"));
    }

    private Transports() {}

    static void started(Object transport) {
        running.add(transport);
    }

    static void stopped(Object transport) {
        running.remove(transport);
    }

    private static void stopAll() {
        for (Object transport : running.toArray()) {
            if (transport instanceof Initiator initiator) {
                initiator.stop();
            } else if (transport instanceof Acceptor acceptor) {
                acceptor.stop();
            }
        }
        running.clear();
    }
}
