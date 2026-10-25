package com.dudleylane.quickfix;

/**
 * FIX::ThreadedSocketAcceptor. It keeps what it was made with -- the engine holds C++ references to them, and the GC would
 * otherwise free an argument written inline while the engine still used it (#110) -- and is held while
 * running (Transports). close() stops it and frees it, which unregisters its sessions; until then, or
 * until the GC finalizes it after stop(), another transport cannot take the same sessions.
 */
public class ThreadedSocketAcceptor extends ThreadedSocketAcceptorBase implements AutoCloseable {
    private final Application application;
    private final MessageStoreFactory storeFactory;
    private final SessionSettings settings;
    private final LogFactory logFactory;

    public ThreadedSocketAcceptor(Application application, MessageStoreFactory storeFactory, SessionSettings settings) {
        super(application, storeFactory, settings);
        this.application = application;
        this.storeFactory = storeFactory;
        this.settings = settings;
        this.logFactory = null;
    }

    public ThreadedSocketAcceptor(Application application, MessageStoreFactory storeFactory, SessionSettings settings,
                  LogFactory logFactory) {
        super(application, storeFactory, settings, logFactory);
        this.application = application;
        this.storeFactory = storeFactory;
        this.settings = settings;
        this.logFactory = logFactory;
    }

    // Guarded by this. The shutdown hook, the application's close() and the GC's finalizer can each reach a
    // transport on their own thread; the engine's stop() returns at once to a second caller while the first is
    // still waiting for the transport's threads, and freeing it then freed it under them.
    private boolean closed;

    @Override
    public synchronized void start() {
        if (closed) {
            throw new IllegalStateException("ThreadedSocketAcceptor was closed");
        }
        Transports.started(this);
        try {
            super.start();
        } catch (RuntimeException e) {
            Transports.stopped(this);
            throw e;
        }
    }

    @Override
    public void block() {
        synchronized (this) {
            if (closed) {
                throw new IllegalStateException("ThreadedSocketAcceptor was closed");
            }
            Transports.started(this);
        }
        try {
            super.block();
        } finally {
            Transports.stopped(this);
        }
    }

    @Override
    public synchronized void stop() {
        if (!closed) {
            super.stop();
            Transports.stopped(this);
        }
    }

    @Override
    public synchronized void stop(boolean force) {
        if (!closed) {
            super.stop(force);
            Transports.stopped(this);
        }
    }

    /** Stops the transport and frees it; after that only close() and stop() may be called, and do nothing. */
    @Override
    public void close() {
        synchronized (this) {
            if (closed) {
                return;
            }
            stop();
        }
        Transports.free(this);
    }

    /** Frees the transport, as close() does once it is stopped. */
    @Override
    public synchronized void delete() {
        closed = true;
        Transports.stopped(this);
        super.delete();
    }
}
