import os
import subprocess
import sys
import unittest


# The engine holds each Log a LogFactory creates until destroy; the binding keeps the script's Log alive meanwhile
# and hands destroy the object create returned (#205). Each case runs in a process of its own.
class ScriptLogFactoryTestCase(unittest.TestCase):

    def child(self, mode):
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ScriptLogFactoryChild.py')
        result = subprocess.run([sys.executable, script, mode], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=60)
        self.assertNotIn('Sanitizer', result.stdout)
        self.assertEqual(0, result.returncode, result.stdout)
        lines = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        self.assertEqual('true', lines.get('survived'), result.stdout)
        return lines, result.stdout

    # A factory that keeps nothing: after its logs are collected, events still reach them, because the binding keeps
    # them. On the unfixed binding the collected director is used and the process crashes.
    def test_a_factory_that_keeps_nothing(self):
        lines, output = self.child('drop')
        self.assertEqual('True', lines.get('loggedOn'), output)

    # A factory that matches destroy by identity holds nothing once the transport is deleted, because destroy is
    # handed the object create returned; the binding's own keep map empties too (release fired).
    def test_destroy_gets_the_created_log(self):
        lines, output = self.child('match')
        self.assertNotEqual('0', lines.get('created'), output)
        self.assertEqual('0', lines.get('held_after_destroy'), output)
        self.assertEqual('0', lines.get('binding_held'), output)

    # The binding keeps each log while the transport lives (so logs held only weakly stay alive) and releases them
    # when it goes (so they are then collected).
    def test_the_binding_keeps_then_releases(self):
        lines, output = self.child('weakref')
        self.assertNotEqual('0', lines.get('created'), output)
        self.assertEqual(lines.get('created'), lines.get('alive_during'), output)
        self.assertEqual('0', lines.get('alive_after'), output)
        self.assertEqual('0', lines.get('binding_held'), output)

    # One Log returned for every create: kept once per address with a count, released only after the last destroy.
    def test_a_shared_log(self):
        lines, output = self.child('shared')
        self.assertEqual('0', lines.get('outstanding'), output)
        self.assertEqual('0', lines.get('binding_held'), output)


if __name__ == '__main__':
    unittest.main()
