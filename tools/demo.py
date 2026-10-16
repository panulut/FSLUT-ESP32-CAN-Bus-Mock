#!/usr/bin/env python3
"""Local browser demo backed by the firmware's real C++ simulator core."""
from __future__ import annotations

import argparse
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import threading
import time
import webbrowser

ROOT = Path(__file__).resolve().parents[1]
DEMO = ROOT / 'demo'


def build_engine(compiler=None):
    binary = ROOT / 'build' / ('demo_engine.exe' if sys.platform == 'win32' else 'demo_engine')
    dependencies = [DEMO / 'engine.cpp', *list((ROOT / 'include').glob('*.h')),
                    ROOT / 'include/recording.inc']
    if binary.exists() and binary.stat().st_mtime >= max(p.stat().st_mtime for p in dependencies):
        return binary
    compiler = compiler or shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        local = ROOT / '.tools/native/ziglang/zig.exe'
        if local.exists():
            compiler = str(local)
    if not compiler:
        raise RuntimeError('C++17 compiler missing. Pass --cxx PATH (g++, clang++ or zig).')
    binary.parent.mkdir(exist_ok=True)
    command = [compiler]
    if Path(compiler).stem == 'zig':
        command.append('c++')
    command += ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                str(DEMO / 'engine.cpp'), '-o', str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    return binary


class Engine:
    def __init__(self, binary):
        self.process = subprocess.Popen([str(binary)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True, encoding='utf-8',
                                        bufsize=1, cwd=ROOT)
        self.lock = threading.Lock()
        self.events = deque(maxlen=600)
        self.sequence = 0
        self.state = {}
        self.error = None
        self.closed = threading.Event()
        self._exchange('@STEP 0')

    def _exchange(self, command):
        if self.error:
            raise RuntimeError(self.error)
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('C++ demo engine stopped')
        state = json.loads(line)
        for event in state.pop('events'):
            self.sequence += 1
            event['seq'] = self.sequence
            self.events.append(event)
        self.state = state
        return state

    def command(self, commands):
        with self.lock:
            for command in commands:
                self.sequence += 1
                self.events.append({'seq': self.sequence, 'kind': 'command',
                                    'time_ms': self.state.get('time_ms', 0), 'text': command})
                self._exchange(command)

    def snapshot(self, since):
        with self.lock:
            return {**self.state, 'events': [e for e in self.events if e['seq'] > since],
                    'sequence': self.sequence, 'error': self.error}

    def run(self):
        previous = time.monotonic()
        try:
            while not self.closed.wait(0.01):
                current = time.monotonic()
                elapsed = int((current - previous) * 1000)
                if elapsed:
                    # No simulated time is discarded; advance in bounded chunks.
                    with self.lock:
                        remaining = elapsed
                        while remaining:
                            chunk = min(remaining, 1000)
                            self._exchange(f'@STEP {chunk}')
                            remaining -= chunk
                    previous += elapsed / 1000
        except (OSError, ValueError, RuntimeError) as error:
            self.error = str(error)

    def close(self):
        self.closed.set()
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)


def self_test(binary):
    engine = Engine(binary)
    checks = 0

    def expect(condition, name):
        nonlocal checks
        if not condition:
            raise AssertionError(name)
        checks += 1

    try:
        expect(not engine.state['running'] and len(engine.state['messages']) == 1, 'boot stopped')
        engine.command(['START', '@STEP 1002'])
        expect(engine.state['sent'] == 2, 'cyclic messages at 0 and 1000 ms')
        engine.command(['SET_SIGNAL NMT_STATE 127', '@STEP 1000'])
        expect(engine.state['messages'][0]['data'] == '7F', 'same signal encoding')
        engine.command(['SET 0x701 0 256'])
        expect(engine.events[-1]['text'].startswith('ERR'), 'invalid value rejected')
        engine.command(['STOP', 'MODE PLAYBACK', 'RESTART', 'LOOP OFF', 'PLAY', '@STEP 2000'])
        expect(engine.state['complete'] and engine.state['sent'] == 52, '49 recorded frames complete')
        frames = [e for e in engine.events if e['kind'] == 'tx' and e.get('id') == '0x301']
        expect(bool(frames) and all(e['dlc'] == 7 for e in frames), 'recorded DLC stays 7')
        engine.command(['@PROFILE SYNTHETIC', 'START', '@STEP 2000'])
        signals = {s['name']: s['value'] for s in engine.state['signals']}
        expect(signals['RPM'] == 1200 and signals['SOC'] == 79, 'real AUTO ramp and quantization')
        engine.command(['@ACK OFF', '@STEP 300'])
        expect(engine.state['fault'] and not engine.state['running'], 'missing ACK stops')
        engine.command(['@ACK ON', 'RECOVER', 'START', '@STEP 110'])
        expect(not engine.state['fault'] and engine.state['running'], 'explicit recovery')
        engine.command(['@RX'])
        expect(engine.state['rx'] == 1, 'software RX injection')
        engine.command(['@PROFILE VEHICLE'])
        expect(engine.state['profile'] == 'vehicle' and not engine.state['running'], 'reset to real profile')
        print(f'PASS: {checks} demo integration checks (real C++ core, software CAN adapter)')
    finally:
        engine.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--cxx')
    parser.add_argument('--no-open', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    binary = build_engine(args.cxx)
    if args.self_test:
        self_test(binary)
        return
    token = secrets.token_urlsafe(24)
    engine = Engine(binary)
    worker = threading.Thread(target=engine.run, daemon=True)
    url = f'http://127.0.0.1:{args.port}'

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def send(self, body, content_type='application/json; charset=utf-8', status=200):
            if not isinstance(body, bytes):
                body = body.encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            from urllib.parse import urlsplit, parse_qs
            path = urlsplit(self.path)
            if path.path == '/api/state':
                try:
                    since = max(0, int(parse_qs(path.query).get('since', ['0'])[0]))
                    self.send(json.dumps(engine.snapshot(since), allow_nan=False))
                except ValueError:
                    self.send('{"error":"invalid sequence"}', status=400)
            elif path.path in ('/', '/app.js', '/style.css'):
                name = 'index.html' if path.path == '/' else path.path[1:]
                data = (DEMO / name).read_text(encoding='utf-8').replace('__TOKEN__', token)
                content_type = {'index.html': 'text/html', 'app.js': 'text/javascript', 'style.css': 'text/css'}[name]
                self.send(data, content_type + '; charset=utf-8')
            elif path.path == '/favicon.ico':
                self.send(b'', status=204)
            else:
                self.send('{"error":"not found"}', status=404)

        def do_POST(self):
            if self.path != '/api/command':
                self.send('{"error":"not found"}', status=404)
                return
            if self.headers.get('X-Demo-Token') != token:
                self.send('{"error":"invalid session"}', status=403)
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if size < 1 or size > 8192:
                    raise ValueError('request length')
                commands = json.loads(self.rfile.read(size)).get('commands')
                if not isinstance(commands, list) or not 1 <= len(commands) <= 16:
                    raise ValueError('expected 1..16 commands')
                if any(not isinstance(c, str) or not 1 <= len(c) <= 512 or '\n' in c or '\r' in c for c in commands):
                    raise ValueError('each command must be a single line, 1..512 characters')
                engine.command(commands)
                self.send('{"ok":true}')
            except (ValueError, AttributeError) as error:
                self.send(json.dumps({'error': str(error)}), status=400)
            except (OSError, RuntimeError) as error:
                self.send(json.dumps({'error': str(error)}), status=503)

    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.daemon_threads = True
    worker.start()
    print(f'CAN demo: {url} (software CAN; no connected hardware)', flush=True)
    if not args.no_open:
        webbrowser.open(url)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        engine.close()


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'error: {error}', file=sys.stderr)
        raise SystemExit(1)
