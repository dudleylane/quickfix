package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;

public class MessageTests {
    static String wire() {
        Message message = Fixture.order("ORDERID");
        message.getHeader().setField(8, "FIX.4.2");
        message.getHeader().setField(49, "CLIENT");
        message.getHeader().setField(56, "SERVER");
        message.getHeader().setField(34, "1");
        message.getHeader().setField(52, "20261025-10:00:00.000");
        return message.toString();
    }

    public void testRoundTrip() {
        String wire = wire();
        Check.isTrue(wire.startsWith("8=FIX.4.2\u00019="), "begins with BeginString and BodyLength: " + wire);
        Check.isTrue(wire.contains("\u000110="), "ends with a CheckSum");
        Message parsed = new Message(wire);
        Check.equal("D", parsed.getHeader().getField(35), "MsgType in the header");
        Check.equal("ORDERID", parsed.getField(11), "ClOrdID in the body");
        Check.isTrue(!parsed.isSetField(35), "MsgType is not a body field");
        Check.isTrue(parsed.isApp(), "an application message");
        Check.equal(wire, parsed.toString(), "serialises back to the same bytes");
    }

    public void testAMissingFieldIsFieldNotFound() {
        Message message = new Message(wire());
        FieldNotFound e = Check.raises(FieldNotFound.class, () -> message.getField(58));
        Check.equal(58, e.getField(), "the tag");
    }

    public void testGarbageIsInvalidMessage() {
        Check.raises(InvalidMessage.class, () -> new Message("garbage"));
    }

    public void testOneMessageParsesMany() {
        Message message = new Message();
        for (int i = 0; i < 1000; ++i) {
            message.setString(wire());
            Check.equal("LNUX", message.getField(55), "reused message " + i);
        }
    }

    public void testRepeatingGroups() {
        Message message = new Message();
        message.getHeader().setField(35, "V");
        for (String symbol : new String[] {"IBM", "MSFT", "LNUX"}) {
            Group group = new Group(146, 55);
            group.setField(55, symbol);
            message.addGroup(group);
        }
        Check.equal(3L, message.groupCount(146), "group count");
        Check.equal("3", message.getField(146), "the count field");
        Group second = new Group(146, 55);
        message.getGroup(2, 146, second);
        Check.equal("MSFT", second.getField(55), "the second instance");
        Check.isTrue(message.hasGroup(3, 146), "a third instance");
        Check.isTrue(!message.hasGroup(4, 146), "no fourth");
    }

    public void testCopiesAreIndependent() {
        Message original = new Message(wire());
        Message copy = new Message(original);
        copy.setField(55, "IBM");
        Check.equal("LNUX", original.getField(55), "the original keeps its value");
        Check.equal("IBM", copy.getField(55), "the copy has its own");
    }
}
