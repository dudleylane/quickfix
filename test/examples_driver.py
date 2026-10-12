#!/usr/bin/env python3
"""Drives the interactive tradeclient example through one limit order per FIX
version and waits for an ExecutionReport for each (#114). Used by runexamples.sh.

    examples_driver.py TRADECLIENT CFG TARGET VERSIONS [SSL-MODE]

VERSIONS are tradeclient's menu digits: 1 FIX.4.0 ... 5 FIX.4.4, 6 FIXT.1.1.
Every prompt is answered by its name, so the order in which a version asks
for its fields does not matter. Exits 0 only if every order was answered.
"""

import os
import re
import select
import subprocess
import sys
import time

TIMEOUT = 20


def main(argv):
    tradeclient, cfg, target, versions = argv[1:5]
    args = [tradeclient, cfg] + argv[5:6]
    client = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              bufsize=0, cwd=os.path.dirname(os.path.abspath(cfg)) + '/..')
    seen = b''

    def read_until(pattern, timeout=TIMEOUT):
        nonlocal seen
        deadline = time.time() + timeout
        while time.time() < deadline:
            match = pattern.search(seen)
            if match:
                return match
            ready, _, _ = select.select([client.stdout], [], [], 0.2)
            if ready:
                chunk = os.read(client.stdout.fileno(), 65536)
                if not chunk:
                    break
                seen += chunk
        return pattern.search(seen)

    def answer(text):
        client.stdin.write((text + '\n').encode())
        client.stdin.flush()

    failures = 0
    # Every session of the requested versions logs on before an order goes out.
    if not read_until(re.compile(rb'Logon - '), 30):
        print('FAILED: tradeclient logged on to nothing')
        return finish(client, seen, 1)
    time.sleep(2)

    for version in versions:
        clordid = f'ORDER{version}{int(time.time() * 1000) % 100000}'
        replies = {
            b'Action': '1', b'BeginString': version, b'ClOrdID': clordid, b'Symbol': 'IBM', b'Side': '1',
            b'OrdType': '2', b'OrderQty': '100', b'TimeInForce': '1', b'Price': '10', b'StopPx': '10',
            b'SenderCompID': 'CLIENT1', b'TargetCompID': target, b'Use a TargetSubID?': 'N', b'Send order?': 'Y',
        }
        prompt = re.compile(rb'(Action|BeginString|ClOrdID|Symbol|Side|OrdType|OrderQty|TimeInForce|Price|StopPx|'
                            rb'SenderCompID|TargetCompID|Use a TargetSubID\?|Send order\?): ')
        while True:
            match = read_until(prompt)
            if not match:
                print(f'{version}: tradeclient stopped asking (no prompt)')
                return finish(client, seen, 1)
            name = match.group(1)
            seen = seen[match.end():]
            answer(replies[name])
            if name == b'Send order?':
                break
        report = re.compile(rb'IN: [^\n]*\x0135=8\x01[^\n]*\x0111=' + clordid.encode() + rb'\x01')
        if read_until(report):
            print(f'{version}: ExecutionReport for {clordid}')
        else:
            print(f'{version}: FAILED, no ExecutionReport for {clordid}')
            failures += 1
    read_until(re.compile(rb'Action: '))
    answer('5')
    return finish(client, seen, 1 if failures else 0)


def finish(client, seen, status):
    try:
        client.stdin.close()
        client.wait(10)
    except Exception:
        client.kill()
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv))
