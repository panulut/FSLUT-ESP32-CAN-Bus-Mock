#!/usr/bin/env python3
"""Smoke-test the locally running demo HTTP API; restores the stopped vehicle profile."""
import json
import time
import urllib.error
import urllib.request
from html.parser import HTMLParser

BASE = 'http://127.0.0.1:8765'


class Page(HTMLParser):
    token = None
    ids = set()

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'meta' and attrs.get('name') == 'demo-token':
            self.token = attrs['content']
        if 'id' in attrs:
            assert attrs['id'] not in self.ids, 'duplicate element ID'
            self.ids.add(attrs['id'])


def main():
    page = Page()
    page.feed(urllib.request.urlopen(BASE).read().decode('utf-8'))
    assert page.token and {'sent', 'log', 'command', 'start', 'ack'} <= page.ids
    for asset in ('app.js', 'style.css'):
        assert len(urllib.request.urlopen(BASE + '/' + asset).read()) > 100

    def post(commands, token=page.token):
        request = urllib.request.Request(BASE + '/api/command',
                                         data=json.dumps({'commands': commands}).encode(),
                                         headers={'Content-Type': 'application/json', 'X-Demo-Token': token})
        return json.load(urllib.request.urlopen(request))

    def state():
        return json.load(urllib.request.urlopen(BASE + '/api/state'))

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
        post(['@PROFILE VEHICLE', 'INTERVAL 0x701 100', 'START'])
        time.sleep(0.25)
        live = state()
        assert live['running'] and live['sent'] >= 2
        assert any(e.get('id') == '0x701' and e.get('data') == '05' for e in live['events'])
        post(['@ACK OFF'])
        time.sleep(0.35)
        failed = state()
        assert failed['fault'] and not failed['running'] and failed['failed'] >= 1
        post(['@ACK ON', 'RECOVER'])
        assert not state()['fault']
        print('PASS: HTTP page/assets, command session validation, live C++ transmissions, ACK fault and recovery')
    finally:
        post(['@PROFILE VEHICLE'])


if __name__ == '__main__':
    main()
