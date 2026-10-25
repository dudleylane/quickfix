package com.dudleylane.quickfix;

/**
 * FIX::SocketInitiator. It keeps what it was made with -- the engine holds C++ references to them, and the GC would
 * otherwise free an argument written inline while the engine still used it (#110) -- and is held while
 * running (Transports). close() stops it and frees it, which unregisters its sessions; until then, or
 * until the GC finalizes it after stop(), another transport cannot take the same sessions.
 */
public class SocketInitiator extends SocketInitiatorBase implements AutoCloseable {
    private final Application application;
    private final MessageStoreFactory storeFactory;
    private final SessionSettings settings;
    private final LogFactory logFactory;

    public SocketInitiator(Application application, MessageStoreFactory storeFactory, SessionSettings settings) {
        super(application, storeFactory, settings);
        this.application = application;
        this.storeFactory = storeFactory;
        this.settings = settings;
        this.logFactory = null;
    }

    public SocketInitiator(Application application, MessageStoreFactory storeFactory, SessionSettings settings,
                  LogFactory logFactory) {
        super(application, storeFactory, settings, logFactory);
        this.application = application;
        this.storeFactory = storeFactory;
        this.settings = settings;
        this.logFactory = logFactory;
    }

    @Override
    public void start() {
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
        Transports.started(this);
        try {
            super.block();
        } finally {
            Transports.stopped(this);
        }
    }

    @Override
    public void stop() {
        super.stop();
        Transports.stopped(this);
    }

    @Override
    public void stop(boolean force) {
        super.stop(force);
        Transports.stopped(this);
    }

    /** Stops the transport and frees it. */
    @Override
    public void close() {
        stop();
        delete();
    }
}
