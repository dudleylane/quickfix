# Each exception toApp, fromAdmin and fromApp may raise into the engine reaches it as its own class, with its fields,
# through the director:except handlers in quickfix.i, whether the application made it before the call or the callback
# makes it.

import sys
import unittest

import quickfix as fix


class Finalized:
    def __init__(self, ran):
        self.ran = ran

    def __del__(self):
        self.ran.append(fix.SessionID('FIX.4.2', 'SENDER', 'TARGET').toString())


class Raiser(fix.Application):
    def __init__(self):
        super().__init__()
        self.error = None
        self.make = None
        self.finalized = []

    def onCreate(self, sessionID): pass
    def onLogon(self, sessionID): pass
    def onLogout(self, sessionID): pass
    def toAdmin(self, message, sessionID): pass

    def toApp(self, message, sessionID):
        self.raiseError()

    def fromAdmin(self, message, sessionID):
        self.raiseError()

    def fromApp(self, message, sessionID):
        self.raiseError()

    def raiseError(self):
        if self.make:
            finalized = Finalized(self.finalized)
            raise self.make()
        raise self.error


class CallbackExceptionTestCase(unittest.TestCase):
    # What each callback may raise into the engine, in the order of its handlers.
    RAISES = {
        'toApp': [lambda: fix.DoNotSend()],
        'fromAdmin': [lambda: fix.FieldNotFound(44), lambda: fix.IncorrectDataFormat(38, 'not a number'),
                      lambda: fix.IncorrectTagValue(40), lambda: fix.RejectLogon('refused')],
        'fromApp': [lambda: fix.FieldNotFound(44), lambda: fix.IncorrectDataFormat(38, 'not a number'),
                    lambda: fix.IncorrectTagValue(40), lambda: fix.UnsupportedMessageType('XX')],
    }

    def setUp(self):
        self.app = Raiser()
        self.sync = fix.SynchronizedApplication(self.app)
        self.sessionID = fix.SessionID('FIX.4.2', 'SENDER', 'TARGET')
        self.message = fix.Message()

    # Calls the callback, which raises an exception like expected, and returns what the engine's copy of it became.
    def call(self, callback, expected):
        with self.assertRaises(type(expected)) as caught:
            getattr(self.sync, callback)(self.message, self.sessionID)
        self.assertEqual(str(expected), str(caught.exception))
        for name in ('field', 'detail', 'type'):
            if hasattr(expected, name):
                self.assertEqual(getattr(expected, name), getattr(caught.exception, name), name)
        return caught.exception

    def test_each_exception_reaches_the_engine_as_its_own_class(self):
        for callback, makes in self.RAISES.items():
            for make in makes:
                expected = make()
                with self.subTest(callback=callback, error=type(expected).__name__, made='before the call'):
                    error = make()
                    self.app.error, self.app.make = error, None
                    references = sys.getrefcount(error)
                    # The engine threw a C++ copy, which the wrapper raised as a new exception.
                    self.assertIsNot(error, self.call(callback, expected))
                    self.assertEqual(references, sys.getrefcount(error))
                with self.subTest(callback=callback, error=type(expected).__name__, made='by the callback'):
                    self.app.error, self.app.make = None, make
                    self.app.finalized.clear()
                    self.call(callback, expected)
                    self.assertEqual(['FIX.4.2:SENDER->TARGET'], self.app.finalized)


if __name__ == '__main__':
    unittest.main()
