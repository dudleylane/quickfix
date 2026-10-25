package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import java.nio.file.Files;
import java.nio.file.Path;

public class StoreTests {
    static void exercise(MessageStore store) {
        Check.isTrue(store.set(1, "first"), "set 1");
        Check.isTrue(store.set(2, "second"), "set 2");
        VectorString messages = new VectorString();
        store.get(1, 2, messages);
        Check.equal(2, messages.size(), "both read back");
        Check.equal("second", messages.get(1), "in order");
        store.setNextSenderMsgSeqNum(7);
        Check.equal(7L, store.getNextSenderMsgSeqNum(), "sender sequence number");
        store.incrNextTargetMsgSeqNum();
        Check.equal(2L, store.getNextTargetMsgSeqNum(), "target sequence number");
    }

    public void testMemoryStore() {
        MemoryStoreFactory factory = new MemoryStoreFactory();
        exercise(factory.create(UtcTimeStamp.now(), new SessionID("FIX.4.2", "A", "B")));
    }

    public void testFileStore() throws Exception {
        Path directory = Files.createTempDirectory("quickfix-java-store");
        FileStoreFactory factory = new FileStoreFactory(directory.toString());
        exercise(factory.create(UtcTimeStamp.now(), new SessionID("FIX.4.2", "A", "B")));
        try (var files = Files.list(directory)) {
            Check.isTrue(files.count() > 0, "it wrote its files");
        }
    }
}
