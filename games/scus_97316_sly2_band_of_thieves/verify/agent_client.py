#!/usr/bin/env python3
"""agent_client.py -- a minimal client of the agent control plane (app/host/agent_server.h) for the
verify scripts. Newline-delimited JSON over the UNIX socket NATIVEEE_AGENT_SOCKET names; replies are
matched by id, events are queued. Stdlib only; no game data leaves the process except what a script
prints (hashes and small numbers)."""
import json
import os
import socket
import subprocess
import sys
import threading
import time
from collections import deque


class AgentClient:
    def __init__(self, path, timeout=600.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.timeout = timeout
        self.rfile = self.sock.makefile("rb")
        self.next_id = 1
        self.replies = {}
        self.events = deque()
        self.cv = threading.Condition()
        self.closed = False
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.rfile:
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            with self.cv:
                if "event" in msg:
                    self.events.append(msg)
                elif "id" in msg:
                    self.replies[msg["id"]] = msg
                self.cv.notify_all()
        with self.cv:
            self.closed = True
            self.cv.notify_all()

    def send(self, op, **body):
        rid = self.next_id
        self.next_id += 1
        body["id"] = rid
        body["op"] = op
        self.sock.sendall((json.dumps(body) + "\n").encode())
        return rid

    def wait(self, rid, timeout=None):
        deadline = time.time() + (timeout or self.timeout)
        with self.cv:
            while rid not in self.replies:
                if self.closed:
                    raise RuntimeError("connection closed waiting for reply %d" % rid)
                left = deadline - time.time()
                if left <= 0:
                    raise TimeoutError("no reply to request %d" % rid)
                self.cv.wait(min(left, 0.5))
            return self.replies.pop(rid)

    def call(self, op, timeout=None, **body):
        return self.wait(self.send(op, **body), timeout)

    def ok(self, op, **body):
        r = self.call(op, **body)
        if not r.get("ok"):
            raise RuntimeError("%s failed: %s" % (op, r.get("error")))
        return r

    def drain_events(self):
        with self.cv:
            out = list(self.events)
            self.events.clear()
        return out

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def start_game(binary, name, sock, root, extra, env_extra=None, log=None):
    """Starts nativeee headless with the control plane on `sock`; harness lanes under root/name/.
    Returns the Popen. The caller deletes root/name.dump (never holds a PPM: DUMP_FROM far away)."""
    iso = os.environ.get("ISO", "/Users/antmenaz/Downloads/Sly 2 - Band of Thieves.iso")
    for d in (name, name + ".mc", name + ".dump"):
        subprocess.run(["rm", "-rf", os.path.join(root, d)], check=False)
    os.makedirs(os.path.join(root, name + ".mc"), exist_ok=True)
    os.makedirs(os.path.join(root, name + ".dump"), exist_ok=True)
    if os.path.exists(sock):
        os.unlink(sock)
    ini = os.path.join(root, name + ".ini")
    open(ini, "w").close()
    env = dict(os.environ)
    env.update({"NATIVEEE_AGENT_SOCKET": sock, "NATIVEEE_DUMP_FROM": "99999999", "NATIVEEE_MTVU": env.get("NATIVEEE_MTVU", "0")})
    if env_extra:
        env.update(env_extra)
    cmd = [binary, "--headless", "--dump-frames", os.path.join(root, name + ".dump"), "--harness-out", os.path.join(root, name),
           "--iso", iso, "--profile", "deterministic", "--config", ini, "--set", "paths.mc_dir=" + os.path.join(root, name + ".mc")] + list(extra)
    logf = open(log or os.path.join(root, name + ".log"), "wb")
    proc = subprocess.Popen(cmd, env=env, stdout=logf, stderr=subprocess.STDOUT)
    for _ in range(600):
        if os.path.exists(sock):
            break
        if proc.poll() is not None:
            raise RuntimeError("the game exited before opening the socket (rc %s); see %s" % (proc.returncode, logf.name))
        time.sleep(0.1)
    else:
        proc.kill()
        raise RuntimeError("socket never appeared")
    return proc


def connect(sock, tries=50):
    for _ in range(tries):
        try:
            return AgentClient(sock)
        except (ConnectionRefusedError, FileNotFoundError):
            time.sleep(0.1)
    raise RuntimeError("cannot connect to " + sock)


def uptime():
    return subprocess.run(["uptime"], capture_output=True, text=True).stdout.strip()


if __name__ == "__main__":
    # agent_client.py SOCK OP [JSON body]
    c = connect(sys.argv[1])
    body = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
    print(json.dumps(c.call(sys.argv[2], **body)))
