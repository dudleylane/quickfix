package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.field.*;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;
import java.util.jar.JarFile;

/** The classes spec/GeneratorJava.rb writes: fields, messages, and the groups the messages share. */
public class GeneratedTests {
    static com.dudleylane.quickfix.fix44.NewOrderSingle order() {
        var order = new com.dudleylane.quickfix.fix44.NewOrderSingle();
        ClOrdID.set(order, "ORDER1");
        Side.set(order, Side.BUY);
        TransactTime.set(order, "20261025-10:00:00.000");
        OrdType.set(order, OrdType.LIMIT);
        Price.set(order, 12.5);
        OrderQty.set(order, 100);
        Symbol.set(order, "LNUX");
        HandlInst.set(order, HandlInst.AUTOMATED_EXECUTION_NO_INTERVENTION);
        var party = com.dudleylane.quickfix.fix44.NewOrderSingle.newNoPartyIDs();
        PartyID.set(party, "BROKER");
        PartyIDSource.set(party, PartyIDSource.PROPRIETARY);
        PartyRole.set(party, PartyRole.EXECUTING_FIRM);
        order.addGroup(party);
        return order;
    }

    public void testATypedMessageRoundTrips() {
        var order = order();
        Check.equal("FIX.4.4", order.getHeader().getField(BeginString.FIELD), "BeginString");
        Check.equal("D", MsgType.get(order.getHeader()), "MsgType");
        SenderCompID.set(order.getHeader(), "CLIENT");
        TargetCompID.set(order.getHeader(), "SERVER");
        MsgSeqNum.set(order.getHeader(), 1);
        SendingTime.set(order.getHeader(), "20261025-10:00:00.000");
        DataDictionary dictionary = new DataDictionary(Fixture.spec("FIX44"));

        // What a counterparty receives, validated as it would validate it.
        Message parsed = new Message(order.toString(), dictionary, true);
        Check.equal(Side.BUY, Side.get(parsed), "Side, a char");
        Check.equal(12.5, Price.get(parsed), "Price, a double");
        Check.equal(100.0, OrderQty.get(parsed), "OrderQty");
        Check.equal(1, NoPartyIDs.get(parsed), "NoPartyIDs, an int");
        var party = com.dudleylane.quickfix.fix44.NewOrderSingle.newNoPartyIDs();
        parsed.getGroup(1, NoPartyIDs.FIELD, party);
        Check.equal(PartyRole.EXECUTING_FIRM, PartyRole.get(party), "a field of the group");
        Check.isTrue(!Account.isSet(parsed), "isSet");
        Check.raises(FieldNotFound.class, () -> Account.get(parsed));
    }

    public void testValuesThatDoNotConvertAreIncorrectDataFormat() {
        Message message = new Message();
        message.setField(Side.FIELD, "XY");
        Check.equal(Side.FIELD, Check.raises(IncorrectDataFormat.class, () -> Side.get(message)).getField(), "char");
        message.setField(OrderQty.FIELD, "1e5");
        Check.raises(IncorrectDataFormat.class, () -> OrderQty.get(message));
        message.setField(NoPartyIDs.FIELD, "two");
        Check.raises(IncorrectDataFormat.class, () -> NoPartyIDs.get(message));
        message.setField(PossDupFlag.FIELD, "y");
        Check.raises(IncorrectDataFormat.class, () -> PossDupFlag.get(message));
    }

    public void testDoublesHaveNoExponent() {
        Message message = new Message();
        double[] values = {1e7, 0.1 + 0.2, 1e-7, -2.5, 0.0, 100.0, 123456789.125};
        String[] written = {"10000000", "0.30000000000000004", "0.0000001", "-2.5", "0", "100", "123456789.125"};
        for (int i = 0; i < values.length; ++i) {
            Price.set(message, values[i]);
            Check.equal(written[i], message.getField(Price.FIELD), "Price " + values[i]);
            Check.equal(values[i], Price.get(message), "reads back " + values[i]);
        }
        Check.raises(IllegalArgumentException.class, () -> Price.set(message, Double.NaN));
    }

    public void testAViewIsTheSameMessage() {
        Message generic = new Message(order().toString());
        var view = com.dudleylane.quickfix.fix44.NewOrderSingle.view(generic);
        Check.equal("ORDER1", ClOrdID.get(view), "reads the generic message");
        ClOrdID.set(view, "ORDER2");
        Check.equal("ORDER2", ClOrdID.get(generic), "and writes it");
        Check.raises(IllegalArgumentException.class, () -> com.dudleylane.quickfix.fix44.Logon.view(generic));
    }

    public void testFix5MessagesCarryTheirApplVerID() {
        var order = new com.dudleylane.quickfix.fix50sp2.NewOrderSingle();
        Check.equal("FIXT.1.1", order.getHeader().getField(BeginString.FIELD), "BeginString");
        Check.equal(ApplVerID.FIX50_SP2, ApplVerID.get(order.getHeader()), "ApplVerID");
        var logon = new com.dudleylane.quickfix.fixt11.Logon();
        Check.equal("FIXT.1.1", logon.getHeader().getField(BeginString.FIELD), "an admin message");
        Check.isTrue(!logon.getHeader().isSetField(ApplVerID.FIELD), "with no ApplVerID");
    }

    /** Every generated message class of every version. */
    static List<Class<?>> messageClasses() throws Exception {
        String jar = null;
        for (String entry : System.getProperty("java.class.path").split(java.io.File.pathSeparator)) {
            if (entry.endsWith("quickfix.jar")) {
                jar = entry;
            }
        }
        Check.isTrue(jar != null, "quickfix.jar on the class path");
        List<Class<?>> classes = new ArrayList<>();
        try (JarFile file = new JarFile(jar)) {
            for (var entries = file.entries(); entries.hasMoreElements();) {
                String name = entries.nextElement().getName();
                if (name.matches("com/dudleylane/quickfix/fix[^/]*/[A-Za-z0-9]+\\.class") && !name.endsWith("/Message.class")) {
                    classes.add(Class.forName(name.replace('/', '.').replace(".class", "")));
                }
            }
        }
        return classes;
    }

    /**
     * Each message's groups are classes shared with the version's other messages (GeneratorJava.rb). For every
     * group every message makes, an instance with only its delimiter set, sent and parsed back with the
     * version's dictionary, is that message's group: the tag and delimiter the shared class carries are the
     * ones this message's definition has.
     */
    public void testEveryMessagesGroupsAreItsOwn() throws Exception {
        DataDictionary transport = new DataDictionary(Fixture.spec("FIXT11"));
        int checked = 0;
        for (Class<?> type : messageClasses()) {
            String pkg = type.getPackageName().substring("com.dudleylane.quickfix.".length());
            String spec = switch (pkg) {
            case "fixt11" -> "FIXT11";
            default -> pkg.toUpperCase();
            };
            DataDictionary application = Dictionaries.get(spec);
            for (Method factory : type.getMethods()) {
                // Its own groups: the version's Message makes the header's (NoHops), which testHeaderGroups covers.
                if (!factory.getName().startsWith("new") || !Modifier.isStatic(factory.getModifiers())
                    || factory.getParameterCount() != 0 || factory.getDeclaringClass() != type) {
                    continue;
                }
                Message message = (Message) type.getConstructor().newInstance();
                Group group = (Group) factory.invoke(null);
                List<Integer> members = members(group);
                // A data field needs its length field, set to its length, before it: leave both out.
                members.removeIf(tag -> application.isDataField(tag) || application.isDataField(tag + 1)
                                        || transport.isDataField(tag) || transport.isDataField(tag + 1));
                if (members.isEmpty()) {
                    // Made only of nested groups (NoAsgnReqs: Parties and NoRelatedSym): the delimiter alone.
                    members.add(group.delim());
                }
                for (int tag : members) {
                    group.setField(tag, "1");
                }
                message.addGroup(group);
                String wire = message.toString();
                Message parsed = pkg.startsWith("fix5") ? new Message(wire, transport, application, false)
                                                         : new Message(wire, application, false);
                String what = type.getName() + "." + factory.getName() + "()";
                Check.isTrue(parsed.hasGroup(1, group.field()) && !parsed.hasGroup(2, group.field()),
                             what + ": parsed back as one " + group.field() + " group");
                Group back = (Group) factory.invoke(null);
                parsed.getGroup(1, group.field(), back);
                for (int tag : members) {
                    Check.isTrue(back.isSetField(tag), what + ": member " + tag + " is in this message's group");
                }
                ++checked;
            }
        }
        Check.isTrue(checked > 1000, "checked the groups of every message: " + checked);
    }

    /** The fields of a group's order (its ORDER), less the count tags of the groups nested in it. */
    static List<Integer> members(Group group) throws Exception {
        java.lang.reflect.Field field = group.getClass().getDeclaredField("ORDER");
        field.setAccessible(true);
        IntArray order = (IntArray) field.get(null);
        java.util.Set<Integer> nested = new java.util.HashSet<>();
        for (Method factory : group.getClass().getMethods()) {
            if (factory.getName().startsWith("new") && Modifier.isStatic(factory.getModifiers())
                && factory.getParameterCount() == 0) {
                nested.add(((Group) factory.invoke(null)).field());
            }
        }
        List<Integer> members = new ArrayList<>();
        for (int i = 0; order.getitem(i) != 0; ++i) {
            if (!nested.contains(order.getitem(i))) {
                members.add(order.getitem(i));
            }
        }
        return members;
    }

    /** One dictionary per spec, loaded once. */
    static final class Dictionaries {
        private static final java.util.Map<String, DataDictionary> loaded = new java.util.HashMap<>();

        static synchronized DataDictionary get(String spec) {
            return loaded.computeIfAbsent(spec, s -> new DataDictionary(Fixture.spec(s)));
        }
    }

    public void testTimesAsJavaTime() {
        Message message = new Message();
        java.time.Instant instant = java.time.Instant.parse("2026-10-25T10:15:30.123Z");
        TransactTime.set(message, instant);
        Check.equal("20261025-10:15:30.123", TransactTime.get(message), "written in milliseconds");
        Check.equal(instant, TransactTime.getInstant(message), "and read back");
        java.time.Instant micros = java.time.Instant.parse("2026-10-25T10:15:30.123456Z");
        TransactTime.set(message, micros);
        Check.equal("20261025-10:15:30.123456", TransactTime.get(message), "finer when the value is");
        Check.equal(micros, TransactTime.getInstant(message), "read back to the microsecond");
        TransactTime.set(message, "20261025-10:15:30");
        Check.equal(java.time.Instant.parse("2026-10-25T10:15:30Z"), TransactTime.getInstant(message), "no fraction");
        TransactTime.set(message, "2026-10-25T10:15:30Z");
        Check.equal(TransactTime.FIELD, Check.raises(IncorrectDataFormat.class, () -> TransactTime.getInstant(message)).getField(),
                    "ISO 8601 is not FIX");
        TradeDate.set(message, java.time.LocalDate.of(2026, 10, 25));
        Check.equal("20261025", TradeDate.get(message), "a date");
        Check.equal(java.time.LocalDate.of(2026, 10, 25), TradeDate.getDate(message), "read back");
        TradeDate.set(message, "20261325");
        Check.raises(IncorrectDataFormat.class, () -> TradeDate.getDate(message));
        MDEntryTime.set(message, java.time.LocalTime.of(9, 30, 5));
        Check.equal("09:30:05.000", MDEntryTime.get(message), "a time of day");
        Check.equal(java.time.LocalTime.of(9, 30, 5), MDEntryTime.getTime(message), "read back");
    }

    public void testHeaderGroups() {
        var order = new com.dudleylane.quickfix.fix44.NewOrderSingle();
        var hop = com.dudleylane.quickfix.fix44.Message.newNoHops();
        HopCompID.set(hop, "HOP1");
        order.getHeader().addGroup(hop);
        Check.equal(1, NoHops.get(order.getHeader()), "the header's count field");
        Message parsed = new Message(order.toString(), new DataDictionary(Fixture.spec("FIX44")), false);
        Check.isTrue(parsed.getHeader().hasGroup(1, NoHops.FIELD), "parsed back into the header");
    }
}
