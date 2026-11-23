package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.field.PartyID;
import com.dudleylane.quickfix.field.PartySubID;
import com.dudleylane.quickfix.field.Price;
import com.dudleylane.quickfix.fix44.NewOrderSingle;
import java.lang.ref.Reference;
import java.lang.ref.WeakReference;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * What a call returns from inside another object outlives nothing it points into (#194). A view -- a message's
 * header, trailer or group, a SessionSettings' dictionary, a group's DataDictionary -- holds its owner; a const view
 * -- a field, a transport's set of SessionIDs -- is the caller's own copy; a call whose result is its argument
 * returns the argument. Each was a proxy that owned nothing and kept nothing alive, so one kept past its owner read
 * and wrote freed memory once the GC had finalized the owner.
 */
public class ViewTests {
    /** owner is reachable only through view now: it must survive the GC while view does. */
    static void checkKeeps(Object view, WeakReference<?> owner, String what) {
        for (int i = 0; i < 3; ++i) {
            System.gc();
        }
        Check.isTrue(owner.get() != null, what + ": the view keeps its owner");
        Reference.reachabilityFence(view);
    }

    static Message order() {
        NewOrderSingle message = new NewOrderSingle();
        Price.set(message, 1.5);
        com.dudleylane.quickfix.fix44.group.NoPartyIDs party = NewOrderSingle.newNoPartyIDs();
        PartyID.set(party, "PARTY");
        com.dudleylane.quickfix.fix44.group.NoPartySubIDs sub = com.dudleylane.quickfix.fix44.group.NoPartyIDs.newNoPartySubIDs();
        PartySubID.set(sub, "SUB");
        party.addGroup(sub);
        message.addGroup(party);
        return message;
    }

    public void testHeaderAndTrailerKeepTheirMessage() {
        Message message = new Message();
        WeakReference<Message> owner = new WeakReference<>(message);
        Header header = message.getHeader();
        Trailer trailer = message.getTrailer();
        message = null;
        checkKeeps(header, owner, "getHeader");
        checkKeeps(trailer, owner, "getTrailer");
        header.setField(35, "D");
        trailer.setField(10, "123");
        Check.equal("D", header.getField(35), "the header, written");
        Check.equal("123", trailer.getField(10), "the trailer, written");
    }

    public void testGroupViewsKeepTheirMessage() {
        for (String method : List.of("getGroupRef", "getGroupPtr")) {
            Message message = order();
            WeakReference<Message> owner = new WeakReference<>(message);
            FieldMap view = method.equals("getGroupRef") ? message.getGroupRef(1, 453) : message.getGroupPtr(1, 453);
            message = null;
            checkKeeps(view, owner, method);
            Check.equal("PARTY", view.getField(448), method);
            // A view is what C++ returns: a write through it reaches the message.
            view.setField(452, "3");
            Check.equal("3", owner.get().getGroupRef(1, 453).getField(452), method + ": written through");
        }
    }

    public void testSettingsViewsKeepTheirSettings() {
        SessionSettings settings = Fixture.settings("[DEFAULT]\nConnectionType=initiator\nHeartBtInt=30\n"
                                                    + "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=S\nTargetCompID=T\n");
        SessionID id = new SessionID("FIX.4.2", "S", "T");
        WeakReference<SessionSettings> owner = new WeakReference<>(settings);
        Dictionary defaults = settings.get();
        Dictionary section = settings.get(id);
        settings = null;
        checkKeeps(defaults, owner, "get()");
        checkKeeps(section, owner, "get(id)");
        Check.equal("initiator", defaults.getString("ConnectionType"), "the defaults");
        // Writing through get() reaches the settings, as it did.
        section.setString("HeartBtInt", "45");
        Check.equal("45", owner.get().get(id).getString("HeartBtInt"), "written through");
    }

    public void testAGroupDictionaryKeepsItsDictionary() {
        DataDictionary dictionary = new DataDictionary(Fixture.spec("FIX42"));
        WeakReference<DataDictionary> owner = new WeakReference<>(dictionary);
        DataDictionary group = dictionary.getGroupDictionary("V", 146);
        dictionary = null;
        checkKeeps(group, owner, "getGroupDictionary");
        Check.isTrue(group.isField(55) && !group.isField(11), "the group's own dictionary");
    }

    public void testConstViewsAreCopies() {
        for (String method : List.of("getFieldRef", "getFieldPtr")) {
            Message message = new Message();
            message.setField(44, "1.5");
            FieldBase field = method.equals("getFieldRef") ? message.getFieldRef(44) : message.getFieldPtr(44);
            // The caller's own: writing it changes nothing else, and it outlives the message growing, which moves
            // the message's fields.
            field.setString("2");
            Check.equal("1.5", message.getField(44), method + ": the message's own");
            for (int tag = 5000; tag < 5100; ++tag) {
                message.setField(tag, "x");
            }
            Check.equal("2", field.getString(), method);
        }
    }

    public void testATransportsSessionsAreACopy() throws Exception {
        SessionSettings settings = Fixture.initiatorSettings(Fixture.freePort(), "SERVER", "VIEWS", false);
        // Never started: making it makes its session, and closing it frees the session.
        AutoCloseable initiator = Fixture.initiator("Socket", new Fixture.Recorder(), new MemoryStoreFactory(), settings, null);
        try {
            SessionIDSet sessions = ((Initiator) initiator).getSessions();
            Check.equal(1, sessions.size(), "one session");
            sessions.clear();
            Check.equal(1, ((Initiator) initiator).getSessions().size(), "clearing the copy leaves the transport's");
        } finally {
            initiator.close();
        }
    }

    public void testAResultThatIsTheArgumentIsTheArgument() {
        Message message = order();
        FieldBase price = new FieldBase(44, "");
        Check.isTrue(message.getField(price) == price, "getField(field)");
        Check.equal("1.5", price.getString(), "the field, read");
        Group party = NewOrderSingle.newNoPartyIDs();
        Check.isTrue(message.getGroup(1, party) == party, "Message.getGroup");
        Check.equal("PARTY", party.getField(448), "the group, read");
        Group sub = com.dudleylane.quickfix.fix44.group.NoPartyIDs.newNoPartySubIDs();
        Check.isTrue(party.getGroup(1, sub) == sub, "Group.getGroup");
        Check.equal("SUB", sub.getField(523), "the nested group, read");
        FieldMap plain = new FieldMap();
        Check.isTrue(message.getGroup(1, 453, plain) == plain, "FieldMap.getGroup");
        Check.equal("PARTY", plain.getField(448), "the plain group, read");
        Group hop = new Group(627, 628);
        hop.setField(628, "HOP");
        for (FieldMap part : List.of(message.getHeader(), message.getTrailer())) {
            Group got = new Group(627, 628);
            if (part instanceof Header header) {
                header.addGroup(hop);
                Check.isTrue(header.getGroup(1, got) == got, "Header.getGroup");
            } else {
                Trailer trailer = (Trailer) part;
                trailer.addGroup(hop);
                Check.isTrue(trailer.getGroup(1, got) == got, "Trailer.getGroup");
            }
            Check.equal("HOP", got.getField(628), "the header's or trailer's group, read");
        }
    }

    /** A proxy whose object was deleted has swigCPtr 0: as self a call ran on a null pointer, which crashed the JVM,
     *  and as a pointer argument it read as none. */
    public void testADeletedObjectThrowsWhereverItIsPassed() {
        Message message = order();
        message.getHeader().setField(8, "FIX.4.4");
        message.getHeader().setField(35, "D");
        String wire = message.toString();
        message.delete();
        Check.raises(NullPointerException.class, () -> message.getHeader());
        Header header = new Header();
        header.delete();
        Check.raises(NullPointerException.class, () -> new Message().reverseRoute(header));
        DataDictionary dictionary = new DataDictionary();
        dictionary.delete();
        Check.raises(NullPointerException.class, () -> new Message().setString(wire, false, dictionary));
        Message parsed = new Message();
        parsed.setString(wire, false, null);
        Check.equal("D", parsed.getHeader().getField(35), "null is still no dictionary");
    }

    public void testInternalsAreNotWrapped() {
        Map<Class<?>, List<String>> removed = Map.of(
            FieldMap.class, List.of("groups", "cloneInto"), Group.class, List.of("cloneInto"),
            DataDictionary.class, List.of("getOrderedFields", "getHeaderOrderedFields", "getTrailerOrderedFields",
                                          "getMessageOrderedFields"),
            SocketAcceptorBase.class, List.of("sessionToPort"));
        removed.forEach((type, names) -> {
            for (String name : names) {
                Check.isTrue(Arrays.stream(type.getMethods()).map(Method::getName).noneMatch(name::equals),
                             type.getSimpleName() + "." + name);
            }
        });
        Check.raises(ClassNotFoundException.class, () -> Class.forName("com.dudleylane.quickfix.GroupArena"));
    }
}
