#!/usr/bin/env python3
"""
Lokaler Server für die Demo-UI im Browser.

Ohne Cache, damit geänderte Dateien sofort ankommen (python3 -m http.server
cached). Mit Live-Reload: der Server beobachtet ui/. Ändert sich eine
CSS-Datei, tauscht die Seite nur diese aus und behält ihren Zustand, sonst
lädt sie neu. Das Skript dafür hängt der Server beim Ausliefern an
index.html, die Datei selbst bleibt unverändert. Ohne npm und ohne Netz.

    python3 tools/serve-ui.py            # http://localhost:8731
    python3 tools/serve-ui.py 9000       # anderer Port
"""

import functools
import http.server
import os
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ui'))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8731

LIVE_SCRIPT = b'''
<script>
// Live-Reload der Demo (tools/serve-ui.py), nicht Teil des Programms.
(() => {
  const source = new EventSource('/__live');
  source.onmessage = (event) => {
    const file = event.data;
    if (file.endsWith('.css')) {
      for (const link of document.querySelectorAll('link[rel="stylesheet"]')) {
        const href = link.getAttribute('href').split('?')[0];
        if (href === file) link.href = href + '?v=' + Date.now();
      }
    } else {
      location.reload();
    }
  };
})();
</script>
'''


def snapshot():
    """Änderungszeit jeder Datei unter ui/, nach Pfad relativ zu ui/."""
    out = {}
    for base, _, files in os.walk(ROOT):
        for name in files:
            path = os.path.join(base, name)
            try:
                out[os.path.relpath(path, ROOT).replace(os.sep, '/')] = os.stat(path).st_mtime_ns
            except OSError:
                pass
    return out


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, must-revalidate')
        super().end_headers()

    def log_message(self, *args):
        pass

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/__live':
            return self.live()
        if path in ('/', '/index.html'):
            return self.index()
        return super().do_GET()

    def index(self):
        with open(os.path.join(ROOT, 'index.html'), 'rb') as f:
            body = f.read().replace(b'</body>', LIVE_SCRIPT + b'</body>', 1)
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def live(self):
        """Server-Sent Events: meldet jede geänderte Datei, bis die Seite geht."""
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.end_headers()
        before = snapshot()
        quiet = 0
        try:
            while True:
                time.sleep(0.3)
                now = snapshot()
                changed = [p for p, t in now.items() if before.get(p) != t]
                before = now
                for p in changed:
                    self.wfile.write(f'data: {p}\n\n'.encode())
                quiet = 0 if changed else quiet + 1
                if quiet >= 10:                       # merkt, wenn die Seite weg ist
                    self.wfile.write(b': ping\n\n')
                    quiet = 0
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    handler = functools.partial(Handler, directory=ROOT)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), handler)
    server.daemon_threads = True
    print(f'NAMplify Demo auf http://localhost:{PORT}/index.html (Live-Reload)')
    print('(Strg-C beendet.)')
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
