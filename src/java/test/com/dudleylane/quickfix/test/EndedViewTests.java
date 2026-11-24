package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.fix44.NewOrderSingle;
import com.dudleylane.quickfix.fix44.group.NoPartyIDs;
import com.dudleylane.quickfix.fix44.group.NoPartySubIDs;
import java.lang.ref.Reference;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import java.util.stream.Collectors;

/**
 * A view of an object that a call from Java can destroy is ended by that call first: its pointer becomes 0, and a
 * call through it throws NullPointerException instead of reaching freed memory (#196). A group's view -- getGroupRef's,
 * getGroupPtr's -- always; a header's, a trailer's or a SessionSettings dictionary's when Java can destroy what it
 * views. Each destroying call is tested through every overload, with the views it must end and those it must leave,
 * and every public method of the classes views point into is classified, so that a new one is classified too.
 */
public class EndedViewTests {
    static NoPartyIDs party(String id, String sub) {
        NoPartyIDs group = NewOrderSingle.newNoPartyIDs();
        group.setField(448, id);
        NoPartySubIDs child = NoPartyIDs.newNoPartySubIDs();
        child.setField(523, sub);
        group.addGroup(child);
        return group;
    }

    /** A group of 628 at tag: NoHops at 627, or any tag a test needs a group at. */
    static Group hop(String id, int tag) {
        Group group = new Group(tag, 628);
        group.setField(628, id);
        return group;
    }

    /** Two parties, each with a sub ID, and a group in the header and in the trailer. */
    static Message message() {
        NewOrderSingle result = new NewOrderSingle();
        result.addGroup(party("A", "A1"));
        result.addGroup(party("B", "B1"));
        result.getHeader().addGroup(hop("H", 627));
        result.getTrailer().addGroup(hop("T", 627));
        return result;
    }

    static String wire() {
        Message source = new Message();
        source.getHeader().setField(8, "FIX.4.4");
        source.getHeader().setField(35, "D");
        source.setField(11, "X");
        return source.toString();
    }

    /** A group added to owner at tag, and a view of it. */
    static FieldMap holding(FieldMap owner, int tag) {
        owner.addGroup(tag, hop("X", tag));
        return owner.getGroupRef(1, tag);
    }

    /** A view of every group in a message: getGroupRef's and getGroupPtr's, of a group and of one under it. */
    static final class Taken {
        static final List<String> NAMES = List.of("a", "a1", "b", "b1", "header", "trailer");
        private final Map<String, FieldMap> views = new LinkedHashMap<>();

        Taken(Message message) {
            FieldMap a = message.getGroupRef(1, 453);
            FieldMap b = message.getGroupPtr(2, 453);
            views.put("a", a);
            views.put("a1", a.getGroupRef(1, 802));
            views.put("b", b);
            views.put("b1", b.getGroupPtr(1, 802));
            views.put("header", message.getHeader().getGroupRef(1, 627));
            views.put("trailer", message.getTrailer().getGroupRef(1, 627));
        }

        FieldMap get(String name) {
            return views.get(name);
        }
    }

    static final Set<String> ALL = Set.copyOf(Taken.NAMES);
    static final Set<String> BODY = Set.of("a", "a1", "b", "b1");

    static void checkEnded(Object view, String what) {
        try {
            if (view instanceof FieldMap map) {
                map.totalFields();
            } else {
                ((Dictionary) view).size();
            }
        } catch (NullPointerException e) {
            return;
        }
        throw new AssertionError(what + ": the view was not ended");
    }

    static void checkLive(FieldMap view, String what) {
        try {
            view.totalFields();
        } catch (NullPointerException e) {
            throw new AssertionError(what + ": the view was ended", e);
        }
    }

    /** The views named in ended throw; the others still read their group. */
    static void checkViews(Taken taken, Set<String> ended, String what) {
        for (String name : Taken.NAMES) {
            if (ended.contains(name)) {
                checkEnded(taken.get(name), what + ": " + name);
            } else {
                checkLive(taken.get(name), what + ": " + name);
            }
        }
    }

    // The registry, through its test hooks: Views is the binding's own, so by reflection. Each first drops the views
    // the GC has collected.
    static Object call(Class<?> owner, String name, Class<?> type, Object argument) {
        try {
            Method method = owner.getDeclaredMethod(name, type);
            method.setAccessible(true);
            return method.invoke(null, argument);
        } catch (ReflectiveOperationException e) {
            throw new AssertionError(e);
        }
    }

    static Class<?> views() {
        try {
            return Class.forName("com.dudleylane.quickfix.Views");
        } catch (ClassNotFoundException e) {
            throw new AssertionError(e);
        }
    }

    static boolean registered(Object view) {
        return (Boolean) call(views(), "registered", Object.class, view);
    }

    static long cPtr(FieldMap map) {
        return (Long) call(FieldMap.class, "getCPtr", FieldMap.class, map);
    }

    /** How many views are registered at the object at cPtr. */
    static long countAt(long cPtr) {
        return (Long) call(views(), "countAt", long.class, cPtr);
    }

    public void testClear() {
        Message m = message(), other = message();
        Taken v = new Taken(m), w = new Taken(other);
        v.get("a").clear();
        checkViews(v, Set.of("a1"), "a group's clear");
        Check.equal(0L, v.get("a").totalFields(), "the group, cleared");
        Header header = m.getHeader();
        m.clear();
        checkViews(v, ALL, "Message.clear");
        checkViews(w, Set.of(), "another message");
        // The header is no group, and stays.
        Check.equal(0L, header.totalFields(), "the header");
        FieldMap plain = new FieldMap();
        FieldMap view = holding(plain, 9000);
        plain.clear();
        checkEnded(view, "FieldMap.clear");
    }

    public void testSetString() {
        String text = wire();
        byte[] bytes = text.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1);
        Map<String, Consumer<Message>> calls = new LinkedHashMap<>();
        calls.put("setString(s)", m -> m.setString(text));
        calls.put("setString(s, v)", m -> m.setString(text, false));
        calls.put("setString(s, v, d)", m -> m.setString(text, false, null));
        calls.put("setString(s, v, d, d)", m -> m.setString(text, false, null, null));
        calls.put("setStringHeader", m -> m.setStringHeader(text));
        calls.put("setBytes(b)", m -> m.setBytes(bytes));
        calls.put("setBytes(b, v)", m -> m.setBytes(bytes, false));
        calls.put("setBytes(b, v, d)", m -> m.setBytes(bytes, false, null));
        calls.forEach((name, call) -> {
            Message m = message();
            Taken v = new Taken(m);
            call.accept(m);
            checkViews(v, ALL, name);
            Check.equal("D", m.getHeader().getField(35), name);
        });
        // A string it refuses ends them too: it clears before it parses.
        Message m = message();
        Taken v = new Taken(m);
        Check.raises(InvalidMessage.class, () -> m.setString("garbage"));
        checkViews(v, ALL, "a refused string");
    }

    public void testRemoveField() {
        Message m = message();
        Taken v = new Taken(m);
        m.removeField(9999);
        checkViews(v, Set.of(), "a field that is not set");
        m.removeField(453);
        checkViews(v, BODY, "a group's count");
        m.getHeader().removeField(627);
        Set<String> ended = new HashSet<>(BODY);
        ended.add("header");
        checkViews(v, ended, "the header's");
        m.getTrailer().removeField(627);
        checkViews(v, ALL, "the trailer's");
    }

    public void testRemoveGroup() {
        Map<String, Consumer<Message>> calls = new LinkedHashMap<>();
        Map<String, Set<String>> ends = new LinkedHashMap<>();
        calls.put("FieldMap.removeGroup(1, 453)", m -> m.removeGroup(1, 453));
        ends.put("FieldMap.removeGroup(1, 453)", Set.of("a", "a1"));
        calls.put("FieldMap.removeGroup(453)", m -> m.removeGroup(453));
        ends.put("FieldMap.removeGroup(453)", BODY);
        calls.put("FieldMap.removeGroup(3, 453)", m -> m.removeGroup(3, 453));
        ends.put("FieldMap.removeGroup(3, 453)", Set.of());
        calls.put("FieldMap.removeGroup(9000)", m -> m.removeGroup(9000));
        ends.put("FieldMap.removeGroup(9000)", Set.of());
        calls.put("Message.removeGroup(2, group)", m -> m.removeGroup(2, NewOrderSingle.newNoPartyIDs()));
        ends.put("Message.removeGroup(2, group)", Set.of("b", "b1"));
        calls.put("Message.removeGroup(group)", m -> m.removeGroup(NewOrderSingle.newNoPartyIDs()));
        ends.put("Message.removeGroup(group)", BODY);
        calls.put("Header.removeGroup(1, group)", m -> m.getHeader().removeGroup(1, new Group(627, 628)));
        ends.put("Header.removeGroup(1, group)", Set.of("header"));
        calls.put("Header.removeGroup(group)", m -> m.getHeader().removeGroup(new Group(627, 628)));
        ends.put("Header.removeGroup(group)", Set.of("header"));
        calls.put("Trailer.removeGroup(1, group)", m -> m.getTrailer().removeGroup(1, new Group(627, 628)));
        ends.put("Trailer.removeGroup(1, group)", Set.of("trailer"));
        calls.put("Trailer.removeGroup(group)", m -> m.getTrailer().removeGroup(new Group(627, 628)));
        ends.put("Trailer.removeGroup(group)", Set.of("trailer"));
        calls.forEach((name, call) -> {
            Message m = message();
            Taken v = new Taken(m);
            call.accept(m);
            checkViews(v, ends.get(name), name);
        });
        // A group's own: the group, and every group under it.
        for (boolean all : new boolean[] {false, true}) {
            NoPartyIDs owner = NewOrderSingle.newNoPartyIDs();
            for (int i = 0; i < 2; ++i) {
                NoPartySubIDs sub = NoPartyIDs.newNoPartySubIDs();
                holding(sub, 9000);
                owner.addGroup(sub);
            }
            FieldMap first = owner.getGroupRef(1, 802), second = owner.getGroupRef(2, 802);
            FieldMap deep = first.getGroupRef(1, 9000), deep2 = second.getGroupRef(1, 9000);
            if (all) {
                owner.removeGroup(NoPartyIDs.newNoPartySubIDs());
            } else {
                owner.removeGroup(1, NoPartyIDs.newNoPartySubIDs());
            }
            String what = all ? "Group.removeGroup(group)" : "Group.removeGroup(1, group)";
            checkEnded(first, what);
            checkEnded(deep, what + ", under it");
            if (all) {
                checkEnded(second, what + ", the second");
                checkEnded(deep2, what + ", under the second");
            } else {
                Check.equal("X", deep2.getField(628), what + ": the second's");
            }
        }
    }

    public void testReplaceGroup() {
        // The group stays, with what replaced it; the groups under it go.
        Message m = message();
        Taken v = new Taken(m);
        m.replaceGroup(1, 453, party("C", "C1"));
        checkViews(v, Set.of("a1"), "FieldMap.replaceGroup");
        Check.equal("C", v.get("a").getField(448), "the group, replaced");
        Check.equal("C1", v.get("a").getGroupRef(1, 802).getField(523), "under it, what replaced it");
        m.replaceGroup(2, party("D", "D1"));
        checkViews(v, Set.of("a1", "b1"), "Message.replaceGroup");
        Check.equal("D", v.get("b").getField(448), "the second, replaced");
        m.getHeader().replaceGroup(1, hop("H2", 627));
        m.getTrailer().replaceGroup(1, hop("T2", 627));
        checkViews(v, Set.of("a1", "b1"), "Header's and Trailer's");
        Check.equal("H2", v.get("header").getField(628), "the header's group, replaced");
        Check.equal("T2", v.get("trailer").getField(628), "the trailer's group, replaced");
        // One that is not there: nothing.
        Message n = message();
        Taken w = new Taken(n);
        n.replaceGroup(3, 453, party("E", "E1"));
        n.replaceGroup(3, party("E", "E1"));
        checkViews(w, Set.of(), "no such group");
        // A group's own.
        NoPartyIDs owner = party("P", "P1");
        FieldMap sub = owner.getGroupRef(1, 802);
        NoPartySubIDs child = NoPartyIDs.newNoPartySubIDs();
        holding(child, 9000);
        owner.replaceGroup(1, child);
        FieldMap deeper = sub.getGroupRef(1, 9000);
        owner.replaceGroup(1, NoPartyIDs.newNoPartySubIDs());
        checkLive(sub, "Group.replaceGroup: the group");
        checkEnded(deeper, "Group.replaceGroup: under it");
    }

    public void testGetGroup() {
        // FieldMap's and Group's assign over the target, and only when the source is there.
        Message m = message();
        FieldMap target = new FieldMap();
        FieldMap view = holding(target, 9000);
        Check.raises(FieldNotFound.class, () -> m.getGroup(9, 453, target));
        checkLive(view, "FieldMap.getGroup, no such group");
        Check.isTrue(m.getGroup(1, 453, target) == target, "FieldMap.getGroup returns its argument");
        checkEnded(view, "FieldMap.getGroup");
        Check.equal("A", target.getField(448), "the group, read");
        NoPartyIDs owner = party("P", "P1");
        NoPartySubIDs sub = NoPartyIDs.newNoPartySubIDs();
        FieldMap subView = holding(sub, 9000);
        Check.raises(FieldNotFound.class, () -> owner.getGroup(9, sub));
        checkLive(subView, "Group.getGroup, no such group");
        owner.getGroup(1, sub);
        checkEnded(subView, "Group.getGroup");
        Check.equal("P1", sub.getField(523), "the nested group, read");
        // Message's, Header's and Trailer's clear the target before they look, so it loses its groups either way.
        for (String part : List.of("Message", "Header", "Trailer")) {
            for (int number : new int[] {9, 1}) {
                Message n = message();
                Group into = part.equals("Message") ? NewOrderSingle.newNoPartyIDs() : hop("X", 627);
                FieldMap held = holding(into, 9000);
                Check.Action call = switch (part) {
                case "Message" -> () -> n.getGroup(number, into);
                case "Header" -> () -> n.getHeader().getGroup(number, into);
                default -> () -> n.getTrailer().getGroup(number, into);
                };
                if (number == 9) {
                    Check.raises(FieldNotFound.class, call);
                } else {
                    try {
                        call.run();
                    } catch (Exception e) {
                        throw new AssertionError(part + ".getGroup", e);
                    }
                }
                checkEnded(held, part + ".getGroup(" + number + ", group)");
            }
        }
    }

    public void testSwap() {
        // The bodies' groups pass to the other message.
        Message m = message(), n = message();
        Taken v = new Taken(m), w = new Taken(n);
        m.swap(n);
        checkViews(v, BODY, "swap: its own");
        checkViews(w, BODY, "swap: its argument's");
    }

    public void testReverseRoute() {
        // It removes the routing fields from the header, and a group at one of their tags goes with them.
        Message m = message();
        Taken v = new Taken(m);
        m.getHeader().addGroup(hop("R", 128));
        FieldMap routed = m.getHeader().getGroupRef(1, 128);
        Message reply = message();
        Taken w = new Taken(reply);
        m.reverseRoute(reply.getHeader());
        checkViews(v, Set.of(), "reverseRoute");
        checkEnded(routed, "reverseRoute: a group at a routing tag");
        checkViews(w, Set.of(), "reverseRoute: its argument's");
    }

    public void testSend() throws Exception {
        // send removes PossDupFlag and OrigSendingTime from the header, and a group at one of their tags goes with
        // them. An initiator that is never started makes the session.
        SessionSettings settings = Fixture.settings(
            "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=1\n"
            + "StartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nHeartBtInt=30\n"
            + "[SESSION]\nBeginString=FIX.4.4\nSenderCompID=ENDED\nTargetCompID=T\n");
        SessionID id = new SessionID("FIX.4.4", "ENDED", "T");
        AutoCloseable initiator = Fixture.initiator("Socket", new Fixture.Recorder(), new MemoryStoreFactory(), settings, null);
        try {
            Map<String, Consumer<Message>> sends = new LinkedHashMap<>();
            sends.put("sendToTarget", m -> Session.sendToTarget(m, id));
            sends.put("send", m -> Session.lookupSession(id).send(m));
            sends.forEach((name, send) -> {
                Message m = message();
                Taken v = new Taken(m);
                m.getHeader().addGroup(hop("P", 43));
                m.getHeader().addGroup(hop("O", 122));
                List<FieldMap> removed = List.of(m.getHeader().getGroupRef(1, 43), m.getHeader().getGroupRef(1, 122));
                send.accept(m);
                checkViews(v, Set.of(), name);
                for (FieldMap view : removed) {
                    checkEnded(view, name + ": a group at a tag it removes");
                }
            });
            // No such session: nothing is sent, and nothing goes.
            Message m = message();
            Taken v = new Taken(m);
            m.getHeader().addGroup(hop("P", 43));
            FieldMap kept = m.getHeader().getGroupRef(1, 43);
            Check.raises(SessionNotFound.class, () -> Session.sendToTarget(m, new SessionID("FIX.4.4", "NOBODY", "T")));
            checkViews(v, Set.of(), "no such session");
            Check.equal("P", kept.getField(628), "no such session: the group at 43");
        } finally {
            initiator.close();
        }
    }

    /** Java's delete() on a proxy that owns its object frees it and everything in it: the views go first. */
    public void testDeleteEndsTheViewsOfWhatItFrees() {
        Message m = message();
        Taken v = new Taken(m);
        Header header = m.getHeader();
        Trailer trailer = m.getTrailer();
        m.delete();
        checkViews(v, ALL, "Message.delete");
        checkEnded(header, "Message.delete: its header");
        checkEnded(trailer, "Message.delete: its trailer");
        for (FieldMap owner : List.of(new FieldMap(), new Group(627, 628), new Header(), new Trailer())) {
            String what = owner.getClass().getSimpleName() + ".delete";
            FieldMap view = holding(owner, 9000);
            FieldMap deeper = holding(view, 9001);
            owner.delete();
            checkEnded(view, what);
            checkEnded(deeper, what + ", under it");
        }
        SessionSettings settings = Fixture.settings("[DEFAULT]\nConnectionType=initiator\nHeartBtInt=30\n"
                                                    + "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=S\nTargetCompID=T\n");
        Dictionary defaults = settings.get();
        Dictionary section = settings.get(new SessionID("FIX.4.2", "S", "T"));
        settings.delete();
        checkEnded(defaults, "SessionSettings.delete: get()");
        checkEnded(section, "SessionSettings.delete: get(id)");
    }

    /** A view its caller deleted stays in the registry until its object goes, and ending it again does nothing. */
    public void testAViewItsCallerDeleted() {
        Message m = message();
        FieldMap view = m.getGroupRef(1, 453);
        view.delete();
        checkEnded(view, "deleted");
        Check.isTrue(registered(view), "still registered");
        m.clear();
        Check.isTrue(!registered(view), "taken by the call that destroys its group");
        checkEnded(view, "and still ended");
    }

    /** A message proxy that owns nothing, as a callback's does: quickfixJNI's director makes new Message(cPtr, false). */
    static final class Unowned extends Message {
        Unowned(Message message) {
            super(getCPtr(message), false);
        }
    }

    /**
     * Which views are registered: a group's always; a header's, a trailer's or a dictionary's when Java can destroy
     * what it views. Through a proxy that owns nothing, such as a callback's message, a header view is not
     * registered, and its message's delete() cannot end it: this records that limit (#199), which README states.
     */
    public void testWhatIsRegistered() {
        Message owned = message();
        Message unowned = new Unowned(owned);
        Header ownedHeader = owned.getHeader();
        Header unownedHeader = unowned.getHeader();
        FieldMap group = unowned.getGroupRef(1, 453);
        FieldMap viaView = owned.getGroupRef(1, 453).getGroupRef(1, 802);
        Check.isTrue(registered(ownedHeader), "the header of a message Java owns");
        Check.isTrue(registered(owned.getTrailer()), "its trailer");
        Check.isTrue(!registered(unownedHeader), "the header of a message proxy that owns nothing (the limit)");
        Check.isTrue(registered(group), "a group's, through a proxy that owns nothing");
        Check.isTrue(registered(viaView), "a group's, through a group's view");
        Check.isTrue(registered(ownedHeader.getGroupRef(1, 627)), "a header group's");
        SessionSettings settings = Fixture.settings("[DEFAULT]\nConnectionType=initiator\n");
        Check.isTrue(registered(settings.get()), "a dictionary of SessionSettings Java owns");
        owned.delete();
        checkEnded(group, "the group's, at its message's delete()");
        checkEnded(ownedHeader, "the owned message's header, at its delete()");
        Reference.reachabilityFence(unownedHeader);
    }

    public void testTheRegistryKeepsOnlyWhatIsHeld() throws Exception {
        Message m = message();
        FieldMap group = m.getGroupRef(2, 453);
        long address = cPtr(group);
        Check.equal(1L, countAt(address), "one view");
        List<FieldMap> views = new ArrayList<>();
        for (int i = 0; i < 200; ++i) {
            views.add(m.getGroupRef(2, 453));
        }
        Check.equal(201L, countAt(address), "200 more, held");
        views = null;
        boolean dropped = false;
        // A proxy has a finalizer, so its weak reference clears a collection after the finalizer thread has run it.
        for (int round = 0; round < 100 && !dropped; ++round) {
            System.gc();
            Thread.sleep(20);
            dropped = countAt(address) == 1;
        }
        Check.isTrue(dropped, "dropped and collected, they leave the registry: " + countAt(address) + " left");
        List<FieldMap> ended = new ArrayList<>();
        for (int i = 0; i < 10; ++i) {
            ended.add(m.getGroupRef(2, 453));
        }
        Check.equal(11L, countAt(address), "10 more");
        m.clear();
        Check.equal(0L, countAt(address), "ended, they leave the registry");
        checkEnded(group, "Message.clear: the first");
        for (FieldMap view : ended) {
            checkEnded(view, "Message.clear");
        }
    }

    /** Threads registering and ending views at once, each on its own message, while the GC frees the old ones. */
    public void testConcurrentViews() throws Exception {
        String text = wire();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        List<Thread> threads = new ArrayList<>();
        for (int t = 0; t < 4; ++t) {
            threads.add(new Thread(() -> {
                try {
                    for (int i = 0; i < 2000; ++i) {
                        Message m = message();
                        FieldMap view = m.getGroupRef(1, 453);
                        Header header = m.getHeader();
                        m.setString(text);
                        checkEnded(view, "setString, threaded");
                        checkLive(header, "the header, threaded");
                        if (i % 2 == 0) {
                            m.delete();
                            checkEnded(header, "delete, threaded");
                        }
                    }
                } catch (Throwable e) {
                    failure.compareAndSet(null, e);
                }
            }));
        }
        threads.forEach(Thread::start);
        for (Thread thread : threads) {
            thread.join();
        }
        if (failure.get() != null) {
            throw new AssertionError(failure.get());
        }
    }

    // Every public and protected method of the classes views point into: what destroys an object a view may point
    // at, and what does not. finalize, getCPtr and swigRelease are SWIG's own.
    static final Map<Class<?>, Set<String>> DESTROYS = Map.of(
        FieldMap.class, Set.of("clear", "removeField", "removeGroup", "replaceGroup", "getGroup", "swap", "delete"),
        Message.class, Set.of("clear", "setString", "setStringHeader", "setBytes", "removeGroup", "replaceGroup",
                              "getGroup", "reverseRoute", "delete"),
        Header.class, Set.of("removeGroup", "replaceGroup", "getGroup", "delete"),
        Trailer.class, Set.of("removeGroup", "replaceGroup", "getGroup", "delete"),
        Group.class, Set.of("removeGroup", "replaceGroup", "getGroup", "delete"),
        SessionSettings.class, Set.of("delete"),
        // getGroupDictionary returns a copy, so nothing points into a DataDictionary.
        DataDictionary.class, Set.of());
    static final Map<Class<?>, Set<String>> KEEPS = Map.of(
        FieldMap.class, Set.of("addGroup", "begin", "calculateLength", "calculateString", "calculateTotal", "end",
                               "g_begin", "g_end", "getField", "getFieldBytes", "getFieldIfSet", "getFieldPtr",
                               "getFieldRef", "getGroupPtr", "getGroupRef", "groupCount", "hasGroup", "isEmpty",
                               "isSetField", "setField", "setFieldBytes", "totalFields"),
        Message.class, Set.of("InitializeXML", "__str__", "addGroup", "bodyLength", "checkSum", "getHeader",
                              "getHeaderField", "getSessionID", "getTrailer", "hasEmbeddedSOH", "hasGroup",
                              "hasOutOfOrderGroupMembers", "hasValidStructure", "isAdmin", "isAdminMsgType", "isApp",
                              "isEmpty", "isHeaderField", "isTrailerField", "setGroup", "setHeaderField",
                              "setSessionID", "toApplVerID", "toBeginString", "toBytes", "toString", "toXML",
                              "typedView"),
        Header.class, Set.of("addGroup", "hasGroup"),
        Trailer.class, Set.of("addGroup", "hasGroup"),
        Group.class, Set.of("addGroup", "delim", "field", "hasGroup"),
        SessionSettings.class, Set.of("get", "getSessions", "has", "set", "setFromString", "size"),
        DataDictionary.class, Set.of("addField", "addFieldName", "addFieldType", "addFieldValue", "addGroup",
                                     "addHeaderField", "addMsgField", "addMsgType", "addRequiredField",
                                     "addTrailerField", "addValueName", "allowUnknownMsgFields",
                                     "checkFieldsHaveValues", "checkFieldsOutOfOrder", "checkUserDefinedFields",
                                     "delete", "getFieldName", "getFieldTag", "getFieldType", "getGroupDelimiter",
                                     "getGroupDictionary", "getNameValue", "getValueName", "getVersion",
                                     "hasFieldValue", "isDataField", "isField", "isFieldValue", "isGroup",
                                     "isHeaderField", "isMessageFieldsOrderPreserved", "isMsgField", "isMsgType",
                                     "isMultipleValueField", "isRequiredField", "isTrailerField",
                                     "preserveMessageFieldsOrder", "readFromDocument", "readFromStream",
                                     "readFromURL", "setVersion", "validate"));

    public void testEveryMethodIsClassified() {
        Set<String> swigs = Set.of("finalize", "getCPtr", "swigRelease");
        for (Class<?> type : DESTROYS.keySet()) {
            Set<String> declared = Arrays.stream(type.getDeclaredMethods())
                                       .filter(method -> !method.isSynthetic())
                                       .filter(method -> (method.getModifiers() & (Modifier.PUBLIC | Modifier.PROTECTED)) != 0)
                                       .map(Method::getName)
                                       .filter(name -> !swigs.contains(name))
                                       .collect(Collectors.toSet());
            Set<String> both = new HashSet<>(DESTROYS.get(type));
            both.retainAll(KEEPS.get(type));
            Check.equal(Set.of(), both, type.getSimpleName() + ": classified both ways");
            Set<String> classified = new HashSet<>(DESTROYS.get(type));
            classified.addAll(KEEPS.get(type));
            Check.equal(new java.util.TreeSet<>(declared), new java.util.TreeSet<>(classified), type.getSimpleName());
        }
    }

    /** getGroupDictionary is a copy: what deletes the dictionary's own group dictionary leaves it. */
    public void testAGroupDictionaryIsACopy() {
        DataDictionary dictionary = new DataDictionary(Fixture.spec("FIX42"));
        DataDictionary group = dictionary.getGroupDictionary("V", 146);
        Check.isTrue(group.isField(55) && !group.isField(11), "the group's own dictionary");
        // addGroup over the same group deletes the one it had; then delete().
        dictionary.addGroup("V", 146, 55, new DataDictionary());
        dictionary.delete();
        Check.isTrue(group.isField(55) && !group.isField(11), "the copy, once its dictionary's own is gone");
        // Reading a spec in deletes the dictionary of a group added before it. (A dictionary refuses a second spec:
        // the fields are defined already, and it refuses them before it reads a group.)
        DataDictionary added = new DataDictionary();
        added.addGroup("V", 146, 55, group);
        DataDictionary copy = added.getGroupDictionary("V", 146);
        added.readFromURL(Fixture.spec("FIX42"));
        Check.isTrue(copy.isField(55) && !copy.isField(11), "the copy, once a read replaced its dictionary's own");
        added.delete();
        Check.isTrue(copy.isField(55), "and once its dictionary is gone");
        Check.isTrue(new DataDictionary(Fixture.spec("FIX42")).getGroupDictionary("V", 146).isField(55),
                     "from a dictionary nothing holds");
        Check.isTrue(new DataDictionary(Fixture.spec("FIX42")).getGroupDictionary("V", 11) == null, "no such group");
    }
}
