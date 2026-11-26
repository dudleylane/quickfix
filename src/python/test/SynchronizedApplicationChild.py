# Run by SynchronizedApplicationTestCase in a process of its own: an application wrapped in a
# SynchronizedApplication with no other reference to it, then collected, then called through the wrapper. The
# wrapper keeps the application (#208), so the calls reach it and the process survives; before, the collected
# application was used from freed memory and the process crashed. A crash is a failure in the parent.
import gc

import quickfix as fix


class App(fix.Application):
    def onCreate(self, sessionID): pass
    def onLogon(self, sessionID): pass
    def onLogout(self, sessionID): pass
    def toAdmin(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass
    def toApp(self, message, sessionID): pass
    def fromApp(self, message, sessionID): pass


# The only reference to the application is the one the wrapper keeps. Built with the keyword argument, which the
# keeping must leave working.
sync = fix.SynchronizedApplication(app=App())
gc.collect()
sessionID = fix.SessionID('FIX.4.2', 'SENDER', 'TARGET')
message = fix.Message()
for _ in range(1000):
    sync.onLogon(sessionID)
    sync.fromApp(message, sessionID)
print('end=true')
