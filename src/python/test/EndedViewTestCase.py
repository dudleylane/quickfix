# A group's view -- getGroupRef's, getGroupPtr's -- points into its message, so a call through the binding that
# destroys the group ends the view first: its pointer becomes null, and using it raises ReferenceError instead of
# reaching freed memory (#196). Each destroying call is tested through every overload, with the views it must end
# and those it must leave, and every public method of the classes groups live in is classified, so that a new one
# is classified too.
import gc
import unittest

import quickfix as fix
import quickfix44 as fix44

NoPartyIDs = fix44.NewOrderSingle.NoPartyIDs
NoPartySubIDs = fix44.NewOrderSingle.NoPartyIDs.NoPartySubIDs


def party(identifier, sub):
    group = NoPartyIDs()
    group.setField(fix.PartyID(identifier))
    child = NoPartySubIDs()
    child.setField(fix.PartySubID(sub))
    group.addGroup(child)
    return group


def hop(identifier, tag=627):
    """A group of 628, at tag: NoHops by default, or any tag a test needs a group at."""
    group = fix.Group(tag, 628)
    group.setField(628, identifier)
    return group


def message():
    """Two parties, each with a sub ID, and a group in the header and in the trailer."""
    result = fix44.NewOrderSingle()
    result.addGroup(party("A", "A1"))
    result.addGroup(party("B", "B1"))
    result.getHeader().addGroup(hop("H"))
    result.getTrailer().addGroup(hop("T"))
    return result


def wire():
    source = fix.Message()
    source.getHeader().setField(fix.BeginString("FIX.4.4"))
    source.getHeader().setField(fix.MsgType("D"))
    source.setField(fix.ClOrdID("X"))
    return source.toString()


def holding(owner, tag=9000):
    """owner, holding a group at tag, and a view of that group."""
    if isinstance(owner, fix.Group):
        owner.addGroup(hop("X", tag))
    else:
        owner.addGroup(tag, hop("X", tag))
    return owner, owner.getGroupRef(1, tag)


class Views:
    """A view of every group in a message: getGroupRef's and getGroupPtr's, of a group and of one under it."""

    NAMES = ("a", "a1", "b", "b1", "header", "trailer")

    def __init__(self, message):
        self.a = message.getGroupRef(1, 453)
        self.a1 = self.a.getGroupRef(1, 802)
        self.b = message.getGroupPtr(2, 453)
        self.b1 = self.b.getGroupPtr(1, 802)
        self.header = message.getHeader().getGroupRef(1, 627)
        self.trailer = message.getTrailer().getGroupRef(1, 627)


ALL = set(Views.NAMES)
BODY = {"a", "a1", "b", "b1"}

# Every public method of the classes a group lives in: what destroys a group a view may point at, and what does not.
DESTROYS = {
    fix.FieldMap: {"clear", "removeField", "removeGroup", "replaceGroup", "getGroup", "swap"},
    fix.Message: {"clear", "setString", "setStringHeader", "removeGroup", "replaceGroup", "getGroup", "reverseRoute"},
    fix.Header: {"removeGroup", "replaceGroup", "getGroup"},
    fix.Trailer: {"removeGroup", "replaceGroup", "getGroup"},
    fix.Group: {"removeGroup", "replaceGroup", "getGroup"},
}
KEEPS = {
    fix.FieldMap: {"addGroup", "begin", "calculateLength", "calculateString", "calculateTotal", "end", "g_begin",
                   "g_end", "getField", "getFieldIfSet", "getFieldPtr", "getFieldRef", "getGroupPtr", "getGroupRef",
                   "groupCount", "hasGroup", "isEmpty", "isSetField", "setField", "totalFields"},
    fix.Message: {"InitializeXML", "addGroup", "bodyLength", "checkSum", "getHeader", "getSessionID", "getTrailer",
                  "hasEmbeddedSOH", "hasGroup", "hasOutOfOrderGroupMembers", "hasValidStructure", "isAdmin",
                  "isAdminMsgType", "isApp", "isEmpty", "isHeaderField", "isTrailerField", "setGroup", "setSessionID",
                  "toApplVerID", "toBeginString", "toString", "toXML"},
    fix.Header: {"addGroup", "hasGroup"},
    fix.Trailer: {"addGroup", "hasGroup"},
    fix.Group: {"addGroup", "delim", "field", "hasGroup"},
}


class Application(fix.Application):
    def onCreate(self, sessionID): pass
    def onLogon(self, sessionID): pass
    def onLogout(self, sessionID): pass
    def toAdmin(self, message, sessionID): pass
    def toApp(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass
    def fromApp(self, message, sessionID): pass


class EndedViewTestCase(unittest.TestCase):

    def assertEnded(self, view, msg=None):
        with self.assertRaises(ReferenceError, msg=msg):
            view.totalFields()

    def assertViews(self, views, ended, msg=None):
        """The views named in ended raise; the others still read their group."""
        for name in Views.NAMES:
            view = getattr(views, name)
            if name in ended:
                self.assertEnded(view, f"{msg}: {name}")
            else:
                view.totalFields()

    def test_clear(self):
        m, other = message(), message()
        v, w = Views(m), Views(other)
        v.a.clear()
        self.assertViews(v, {"a1"}, "a group's clear")
        self.assertEqual(0, v.a.totalFields())
        header = m.getHeader()
        m.clear()
        self.assertViews(v, ALL, "Message.clear")
        self.assertViews(w, set(), "another message")
        # The header is no group: its view stays.
        self.assertEqual(0, header.totalFields())
        plain, view = holding(fix.FieldMap())
        plain.clear()
        self.assertEnded(view)

    def test_set_string(self):
        text = wire()
        for call in (lambda m: m.setString(text), lambda m: m.setString(text, False),
                     lambda m: m.setString(text, False, None), lambda m: m.setString(text, False, None, None),
                     lambda m: m.setStringHeader(text)):
            m = message()
            v = Views(m)
            call(m)
            self.assertViews(v, ALL)
            self.assertEqual("D", m.getHeader().getField(35))
        # A string it refuses ends them too: it clears before it parses.
        m = message()
        v = Views(m)
        with self.assertRaises(fix.InvalidMessage):
            m.setString("garbage")
        self.assertViews(v, ALL)

    def test_remove_field(self):
        m = message()
        v = Views(m)
        m.removeField(9999)
        self.assertViews(v, set(), "a field that is not set")
        m.removeField(453)
        self.assertViews(v, BODY, "a group's count")
        m.getHeader().removeField(627)
        self.assertViews(v, BODY | {"header"})
        m.getTrailer().removeField(627)
        self.assertViews(v, ALL)

    def test_remove_group(self):
        for call, ended in (
                (lambda m: fix.FieldMap.removeGroup(m, 1, 453), {"a", "a1"}),
                (lambda m: fix.FieldMap.removeGroup(m, 453), BODY),
                (lambda m: fix.FieldMap.removeGroup(m, 3, 453), set()),
                (lambda m: fix.FieldMap.removeGroup(m, 9000), set()),
                (lambda m: m.removeGroup(2, NoPartyIDs()), {"b", "b1"}),
                (lambda m: m.removeGroup(NoPartyIDs()), BODY),
                (lambda m: m.getHeader().removeGroup(1, fix.Group(627, 628)), {"header"}),
                (lambda m: m.getHeader().removeGroup(fix.Group(627, 628)), {"header"}),
                (lambda m: m.getTrailer().removeGroup(1, fix.Group(627, 628)), {"trailer"}),
                (lambda m: m.getTrailer().removeGroup(fix.Group(627, 628)), {"trailer"})):
            m = message()
            v = Views(m)
            call(m)
            self.assertViews(v, ended, str(ended))
        # A group's own: the group, and every group under it.
        for call, all_of_them in ((lambda g: g.removeGroup(1, NoPartySubIDs()), False),
                                  (lambda g: g.removeGroup(NoPartySubIDs()), True)):
            owner = NoPartyIDs()
            for _ in range(2):
                owner.addGroup(holding(NoPartySubIDs())[0])
            first, second = owner.getGroupRef(1, 802), owner.getGroupRef(2, 802)
            deep, deep2 = first.getGroupRef(1, 9000), second.getGroupRef(1, 9000)
            call(owner)
            self.assertEnded(first)
            self.assertEnded(deep)
            if all_of_them:
                self.assertEnded(second)
                self.assertEnded(deep2)
            else:
                self.assertEqual("X", deep2.getField(628))

    def test_replace_group(self):
        # The group stays, with what replaced it; the groups under it go.
        m = message()
        v = Views(m)
        fix.FieldMap.replaceGroup(m, 1, 453, party("C", "C1"))
        self.assertViews(v, {"a1"})
        self.assertEqual("C", v.a.getField(448))
        self.assertEqual("C1", v.a.getGroupRef(1, 802).getField(523))
        m.replaceGroup(2, party("D", "D1"))
        self.assertViews(v, {"a1", "b1"})
        self.assertEqual("D", v.b.getField(448))
        m.getHeader().replaceGroup(1, hop("H2"))
        m.getTrailer().replaceGroup(1, hop("T2"))
        self.assertViews(v, {"a1", "b1"})
        self.assertEqual(["H2", "T2"], [v.header.getField(628), v.trailer.getField(628)])
        # One that is not there: nothing.
        m = message()
        v = Views(m)
        fix.FieldMap.replaceGroup(m, 3, 453, party("E", "E1"))
        m.replaceGroup(3, party("E", "E1"))
        self.assertViews(v, set())
        # A group's own.
        owner = party("P", "P1")
        sub = owner.getGroupRef(1, 802)
        child, _ = holding(NoPartySubIDs())
        owner.replaceGroup(1, child)
        deeper = sub.getGroupRef(1, 9000)
        owner.replaceGroup(1, NoPartySubIDs())
        sub.totalFields()
        self.assertEnded(deeper)

    def test_get_group(self):
        # FieldMap's and Group's assign over the target, and only when the source is there.
        m = message()
        target, view = holding(fix.FieldMap())
        with self.assertRaises(fix.FieldNotFound):
            fix.FieldMap.getGroup(m, 9, 453, target)
        view.totalFields()
        self.assertIs(target, fix.FieldMap.getGroup(m, 1, 453, target))
        self.assertEnded(view)
        self.assertEqual("A", target.getField(448))
        owner = party("P", "P1")
        target, view = holding(NoPartySubIDs())
        with self.assertRaises(fix.FieldNotFound):
            owner.getGroup(9, target)
        view.totalFields()
        owner.getGroup(1, target)
        self.assertEnded(view)
        self.assertEqual("P1", target.getField(523))
        # Message's, Header's and Trailer's clear the target before they look, so it loses its groups either way.
        for part, tag, make in ((lambda m: m, 453, NoPartyIDs), (lambda m: m.getHeader(), 627, lambda: hop("X")),
                                (lambda m: m.getTrailer(), 627, lambda: hop("X"))):
            m = message()
            for number in (9, 1):
                target, view = holding(make())
                if number == 9:
                    with self.assertRaises(fix.FieldNotFound):
                        part(m).getGroup(number, target)
                else:
                    part(m).getGroup(number, target)
                self.assertEnded(view, f"{tag}, {number}")

    def test_swap(self):
        # The bodies' groups pass to the other message.
        m, n = message(), message()
        v, w = Views(m), Views(n)
        m.swap(n)
        self.assertViews(v, BODY)
        self.assertViews(w, BODY)

    def test_reverse_route(self):
        # It removes the routing fields from the header, and a group at one of their tags goes with it.
        m = message()
        v = Views(m)
        m.getHeader().addGroup(hop("R", tag=128))
        routed = m.getHeader().getGroupRef(1, 128)
        reply = message()
        w = Views(reply)
        m.reverseRoute(reply.getHeader())
        self.assertViews(v, set())
        self.assertEnded(routed)
        self.assertViews(w, set(), "its argument's")

    def test_send(self):
        # send removes PossDupFlag and OrigSendingTime from the header, and a group at one of their tags goes with
        # them. An initiator that is never started makes the session.
        settings = fix.SessionSettings()
        settings.setFromString("[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\n"
                               "SocketConnectPort=1\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
                               "HeartBtInt=30\n[SESSION]\nBeginString=FIX.4.4\nSenderCompID=VIEWS\nTargetCompID=T\n")
        application = Application()
        initiator = fix.SocketInitiator(application, fix.MemoryStoreFactory(), settings)
        sessionID = fix.SessionID("FIX.4.4", "VIEWS", "T")
        try:
            for name, send in (("sendToTarget", lambda m: fix.Session.sendToTarget(m, sessionID)),
                               ("send", lambda m: fix.Session.lookupSession(sessionID).send(m))):
                m = message()
                v = Views(m)
                m.getHeader().addGroup(hop("P", tag=43))
                m.getHeader().addGroup(hop("O", tag=122))
                removed = [m.getHeader().getGroupRef(1, 43), m.getHeader().getGroupRef(1, 122)]
                send(m)
                self.assertViews(v, set(), name)
                for view in removed:
                    self.assertEnded(view, name)
            # No such session: nothing is sent, and nothing goes.
            m = message()
            v = Views(m)
            m.getHeader().addGroup(hop("P", tag=43))
            kept = m.getHeader().getGroupRef(1, 43)
            with self.assertRaises(fix.SessionNotFound):
                fix.Session.sendToTarget(m, fix.SessionID("FIX.4.4", "NOBODY", "T"))
            self.assertViews(v, set())
            self.assertEqual("P", kept.getField(628))
        finally:
            del initiator
            gc.collect()

    def test_every_method_is_classified(self):
        for cls in DESTROYS:
            public = {name for name, value in vars(cls).items() if not name.startswith("_") and callable(value)}
            self.assertEqual(set(), DESTROYS[cls] & KEEPS[cls], cls.__name__)
            self.assertEqual(public, DESTROYS[cls] | KEEPS[cls], cls.__name__)

    def test_the_registry_keeps_only_what_is_held(self):
        m = message()
        gc.collect()
        before = fix._viewCount()
        views = [m.getGroupRef(1, 453) for _ in range(200)]
        self.assertEqual(before + 200, fix._viewCount())
        del views
        self.assertEqual(before, fix._viewCount())
        views = [m.getGroupRef(1, 453) for _ in range(10)]
        m.clear()
        self.assertEqual(before, fix._viewCount())
        for view in views:
            self.assertEnded(view)


if __name__ == "__main__":
    unittest.main()
