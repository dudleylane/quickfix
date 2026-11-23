# What a call returns from inside another object outlives nothing it points into (#194). A view -- a message's
# header, trailer or group, a SessionSettings' dictionary -- holds its owner; a const view -- a field, a SessionID's
# BeginString -- is the caller's own copy; a call whose result is its argument returns the argument. Each was a
# proxy that owned nothing and kept nothing alive, so one kept past its owner read and wrote freed memory.
import gc
import unittest
import weakref

import quickfix as fix
import quickfix44 as fix44


def order():
    message = fix44.NewOrderSingle()
    message.setField(fix.Price(1.5))
    party = fix44.NewOrderSingle.NoPartyIDs()
    party.setField(fix.PartyID("PARTY"))
    sub = fix44.NewOrderSingle.NoPartyIDs.NoPartySubIDs()
    sub.setField(fix.PartySubID("SUB"))
    party.addGroup(sub)
    message.addGroup(party)
    return message


class ViewTestCase(unittest.TestCase):

    def assertKeeps(self, view, owner):
        """owner is a weak reference to what view is a view into, which nothing else refers to now."""
        gc.collect()
        self.assertIsNotNone(owner(), "the view keeps its owner")
        self.assertIs(owner(), view._quickfixOwner)

    def test_header_and_trailer_keep_their_message(self):
        for method in ("getHeader", "getTrailer"):
            message = fix.Message()
            view = getattr(message, method)()
            owner = weakref.ref(message)
            del message
            self.assertKeeps(view, owner)
        # So a chained call is safe: the message the statement made lives as long as its header.
        header = fix.Message().getHeader()
        trailer = fix.Message().getTrailer()
        gc.collect()
        header.setField(fix.MsgType("D"))
        trailer.setField(fix.CheckSum(123))
        self.assertEqual("D", header.getField(35))
        self.assertEqual("123", trailer.getField(10))

    def test_group_views_keep_their_message(self):
        for method in ("getGroupRef", "getGroupPtr"):
            message = order()
            view = getattr(message, method)(1, 453)
            owner = weakref.ref(message)
            del message
            self.assertKeeps(view, owner)
            self.assertEqual("PARTY", view.getField(448), method)
            # A view is what C++ returns: a write through it reaches the message.
            view.setField(fix.PartyRole(3))
            self.assertEqual("3", owner().getGroupRef(1, 453).getField(452), method)

    def test_settings_views_keep_their_settings(self):
        settings = fix.SessionSettings()
        settings.setFromString("[DEFAULT]\nConnectionType=initiator\nHeartBtInt=30\n"
                               "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=S\nTargetCompID=T\n")
        sessionID = fix.SessionID("FIX.4.2", "S", "T")
        defaults, section = settings.get(), settings.get(sessionID)
        owner = weakref.ref(settings)
        del settings
        self.assertKeeps(defaults, owner)
        self.assertKeeps(section, owner)
        self.assertEqual("initiator", defaults.getString("ConnectionType"))
        # Writing through get() reaches the settings, as it did.
        section.setString("HeartBtInt", "45")
        self.assertEqual("45", owner().get(sessionID).getString("HeartBtInt"))

    def test_const_views_are_copies(self):
        for method in ("getFieldRef", "getFieldPtr"):
            message = fix.Message()
            message.setField(fix.Price(1.5))
            field = getattr(message, method)(44)
            self.assertTrue(field.thisown, method)
            # The caller's own: writing it changes nothing else, and it outlives both the message growing, which
            # moves the message's fields, and the message.
            field.setString("2")
            self.assertEqual("1.5", message.getField(44), method)
            for tag in range(5000, 5100):
                message.setField(tag, "x")
            self.assertEqual("2", field.getString(), method)
            del message
            gc.collect()
            self.assertEqual("2", field.getString(), method)

    def test_session_id_getters_are_copies(self):
        sessionID = fix.SessionID("FIX.4.2", "SENDER", "TARGET")
        fields = sessionID.getBeginString(), sessionID.getSenderCompID(), sessionID.getTargetCompID()
        self.assertTrue(all(field.thisown for field in fields))
        fields[1].setString("CHANGED")
        self.assertEqual("SENDER", sessionID.getSenderCompID().getString())
        del sessionID
        gc.collect()
        self.assertEqual(["FIX.4.2", "CHANGED", "TARGET"], [field.getString() for field in fields])

    def test_a_result_that_is_the_argument_is_the_argument(self):
        message = order()
        price = fix.Price()
        self.assertIs(price, message.getField(price))
        self.assertEqual(1.5, message.getField(fix.Price()).getValue())
        self.assertEqual("1.5", message.getField(44))
        party = fix44.NewOrderSingle.NoPartyIDs()
        self.assertIs(party, message.getGroup(1, party))
        self.assertEqual("PARTY", message.getGroup(1, fix44.NewOrderSingle.NoPartyIDs()).getField(448))
        sub = fix44.NewOrderSingle.NoPartyIDs.NoPartySubIDs()
        self.assertIs(sub, party.getGroup(1, sub))
        self.assertEqual("SUB", sub.getField(523))
        plain = fix.FieldMap()
        self.assertIs(plain, fix.FieldMap.getGroup(message, 1, 453, plain))
        self.assertEqual("PARTY", plain.getField(448))
        hop = fix.Group(627, 628)
        hop.setField(628, "HOP")
        for part in (message.getHeader(), message.getTrailer()):
            part.addGroup(hop)
            got = fix.Group(627, 628)
            self.assertIs(got, part.getGroup(1, got))
            self.assertEqual("HOP", got.getField(628))

    def test_an_ended_view_raises_wherever_it_is_passed(self):
        # A view whose object is destroyed is ended: its pointer becomes null. As self it ran C++ on a null
        # pointer, and as a pointer argument it read as none.
        message = order()
        message.getHeader().setField(fix.BeginString("FIX.4.4"))
        message.getHeader().setField(fix.MsgType("D"))
        wire = message.toString()
        header = message.getHeader()
        self.assertTrue(fix._endView(header))
        with self.assertRaises(ReferenceError):
            header.getField(35)
        # As a reference argument SWIG raised already.
        with self.assertRaises(TypeError):
            fix.Message().reverseRoute(header)
        # As a pointer argument: setString's dictionary, which None still leaves out.
        dictionary = fix.DataDictionary()
        dictionary.thisown = False
        self.assertTrue(fix._endView(dictionary))
        with self.assertRaises(ReferenceError):
            fix.Message().setString(wire, False, dictionary)
        parsed = fix.Message()
        parsed.setString(wire, False, None)
        self.assertEqual("D", parsed.getHeader().getField(35))
        # An owner is never ended: its pointer is what frees its object.
        self.assertFalse(fix._endView(message))
        self.assertEqual("D", message.getHeader().getField(35))

    def test_internals_are_not_wrapped(self):
        for owner, names in ((fix.FieldMap, ("groups", "cloneInto")), (fix.Group, ("cloneInto",)),
                             (fix.DataDictionary, ("getOrderedFields", "getHeaderOrderedFields",
                                                   "getTrailerOrderedFields", "getMessageOrderedFields")),
                             (fix.SocketAcceptorBase, ("sessionToPort",))):
            for name in names:
                self.assertFalse(hasattr(owner, name), f"{owner.__name__}.{name}")
        self.assertFalse(hasattr(fix, "GroupArena"))


if __name__ == "__main__":
    unittest.main()
