# The acceptance-test acceptor, through the Python binding: what src/at.cpp
# does, for test/runat.sh with AT set (see test/acceptor.sh).
#   python3 at.py -f FILE [-t]
import getopt
import os
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import quickfix as fix
import at_application

try:
    options, rest = getopt.getopt(sys.argv[1:], 'f:t')
except getopt.GetoptError:
    options, rest = [], ['usage']
options = dict(options)
if rest or '-f' not in options:
    print('usage: at.py -f FILE [-t]')
    sys.exit(1)

stopped = False


def stop(signum, frame):
    global stopped
    stopped = True


# runat.sh stops the acceptor with SIGTERM and fails the run unless it exits 0.
signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)

settings = fix.SessionSettings(options['-f'])
application = at_application.Application()
factory = fix.FileStoreFactory('store')
acceptorClass = fix.ThreadedSocketAcceptor if '-t' in options else fix.SocketAcceptor
acceptor = acceptorClass(application, factory, settings)

acceptor.start()
while not stopped:
    time.sleep(1)
acceptor.stop()
