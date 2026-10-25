package com.dudleylane.quickfix;

/**
 * FIX::SSLSocketInitiator. It keeps what it was made with -- the engine holds C++ references to them, and the GC would
 * otherwise free an argument written inline while the engine still used it (#110) -- and is held while
 * running (Transports). close() stops it and frees it, which unregisters its sessions; until then, or
 * until the GC finalizes it after stop(), another transport cannot take the same sessions.
 */
public class SSLSocketInitiator extends SSLSocketInitiatorBase implements AutoCloseable {
    private final Application application;
    private final MessageStoreFactory storeFactory;
    private final SessionSettings settings;
    private final LogFactory logFactory;

    public SSLSocketInitiator(Application application, MessageStoreFactory storeFactory, SessionSettings settings) {
        super(application, storeFactory, settings);
        this.application = application;
        this.storeFactory = storeFactory;
        this.settings = settings;
        this.logFactory = null;
    }

    public SSLSocketInitiator(Application application, MessageStoreFactory storeFactory, SessionSettings settings,
                  LogFactory logFactory) {
        super(application, storeFactory, settings, logFactory);
        this.application = application;
        this.storeFactory = storeFactory;
        this.settings = settings;
        this.logFactory = logFactory;
    }

    // Guarded by this: the calls into the engine in flight (start, block, stop), and whether close() has
    // begun. No lock is held across the engine call -- the engine's stop() takes concurrent callers since
    // #174, and a callback that stops its own transport while another thread does returns at once -- but
    // close() frees the transport only once nothing is inside it, and after close() a call does nothing.
    private int inFlight;
    private boolean closed;

    private boolean enter() {
        synchronized (this) {
            if (closed) {
                return false;
            }
            ++inFlight;
            return true;
        }
    }

    private void leave() {
        synchronized (this) {
            if (--inFlight == 0) {
                notifyAll();
            }
        }
    }

    @Override
    public void start() {
        if (!enter()) {
            throw new IllegalStateException("SSLSocketInitiator was closed");
        }
        try {
            Transports.started(this);
            try {
                super.start();
            } catch (RuntimeException e) {
                Transports.stopped(this);
                throw e;
            }
        } finally {
            leave();
        }
    }

    @Override
    public void block() {
        if (!enter()) {
            throw new IllegalStateException("SSLSocketInitiator was closed");
        }
        try {
            Transports.started(this);
            try {
                super.block();
            } finally {
                Transports.stopped(this);
            }
        } finally {
            leave();
        }
    }

    @Override
    public void stop() {
        if (enter()) {
            try {
                super.stop();
                Transports.stopped(this);
            } finally {
                leave();
            }
        }
    }

    @Override
    public void stop(boolean force) {
        if (enter()) {
            try {
                super.stop(force);
                Transports.stopped(this);
            } finally {
                leave();
            }
        }
    }

    /**
     * Stops the transport and frees it, once no other call is inside it; after that every call does nothing,
     * or, for start() and block(), throws. Not from a callback of this transport, which would free it under
     * the callback's own thread: stop() it there, and close() it elsewhere.
     */
    @Override
    public void close() {
        stop();
        boolean interrupted = false;
        synchronized (this) {
            if (closed) {
                return;
            }
            closed = true;
            while (inFlight > 0) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    interrupted = true;
                }
            }
        }
        Transports.free(this);
        if (interrupted) {
            Thread.currentThread().interrupt();
        }
    }

    /** Frees the transport, as close() does once it is stopped. */
    @Override
    public synchronized void delete() {
        closed = true;
        Transports.stopped(this);
        super.delete();
    }
}
