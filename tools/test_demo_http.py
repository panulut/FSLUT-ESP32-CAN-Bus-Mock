#!/usr/bin/env python3
"""Smoke-test the locally running demo HTTP API; restores the stopped vehicle profile."""
import argparse
import json
import subprocess
import sys
from pathlib import Path
import time
import urllib.error
import urllib.request
from html.parser import HTMLParser

ROOT = Path(__file__).resolve().parents[1]


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.token = None
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'meta' and attrs.get('name') == 'demo-token':
            self.token = attrs['content']
        if 'id' in attrs:
            assert attrs['id'] not in self.ids, 'duplicate element ID'
            self.ids.add(attrs['id'])


def check(base):
    page = Page()
    page.feed(urllib.request.urlopen(base, timeout=5).read().decode('utf-8'))
    assert page.token and {'sent', 'log', 'command', 'start', 'ack'} <= page.ids
    for asset in ('app.js', 'style.css'):
        assert len(urllib.request.urlopen(base + '/' + asset, timeout=5).read()) > 100

    def post(commands, token=page.token):
        request = urllib.request.Request(base + '/api/command',
                                         data=json.dumps({'commands': commands}).encode(),
                                         headers={'Content-Type': 'application/json', 'X-Demo-Token': token})
        return json.load(urllib.request.urlopen(request, timeout=5))

    def state():
        return json.load(urllib.request.urlopen(base + '/api/state', timeout=5))

    def wait_for(predicate):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            current = state()
            if predicate(current):
                return current
            time.sleep(0.02)
        raise AssertionError('demo state did not reach expected condition within 5 seconds')

    try:
        try:
            post(['START'], token='wrong')
            raise AssertionError('unauthorized request accepted')
        except urllib.error.HTTPError as error:
            assert error.code == 403
        try:
            post(['START\nSTOP'])
            raise AssertionError('multiline command accepted')
        except urllib.error.HTTPError as error:
            assert error.code == 400
        post(['@PROFILE VEHICLE', 'START'])
        empty = state()
        assert not empty['running'] and not empty['messages'] and not empty['signals']
        post(['ADD 0x123 100 1 5', 'START'])
        live = wait_for(lambda s: s['sent'] >= 2)
        assert live['running'] and live['sent'] >= 2
        assert any(e.get('id') == '0x123' and e.get('data') == '05' for e in live['events'])
        post(['@ACK OFF'])
        failed = wait_for(lambda s: s['fault'])
        assert failed['fault'] and not failed['running'] and failed['failed'] >= 1
        post(['@ACK ON', 'RECOVER'])
        assert not state()['fault']
        print('PASS: HTTP page/assets, command session validation, live C++ transmissions, ACK fault and recovery')
    finally:
        post(['@PROFILE VEHICLE'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8765')
    parser.add_argument('--spawn', action='store_true', help='start and stop an isolated test server on a free port')
    args = parser.parse_args()
    if not args.spawn:
        check(args.url)
        return
    process = subprocess.Popen([sys.executable, '-u', str(ROOT / 'tools/demo.py'),
                                '--no-open', '--port', '0'], cwd=ROOT,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True)
    try:
        # demo.py prints the actual ephemeral port after binding.
        line = process.stdout.readline()
        if not line.startswith('CAN demo: '):
            raise RuntimeError('test server failed to start: ' + line.strip())
        check(line.split()[2])
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


if __name__ == '__main__':
    main()
