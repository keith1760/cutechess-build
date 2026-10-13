#!/usr/bin/env python3
# Minimal UCI engine for tests. Logs every line it receives to the file
# named by the FAKEUCI_LOG environment variable.
import os, sys

log = open(os.environ["FAKEUCI_LOG"], "a") if "FAKEUCI_LOG" in os.environ else None

def out(s):
    sys.stdout.write(s + "\n")
    sys.stdout.flush()

for line in sys.stdin:
    line = line.strip()
    if log:
        log.write(line + "\n")
        log.flush()
    if line == "uci":
        out("id name FakeUci")
        out("id author test")
        out("option name Hash type spin default 16 min 1 max 33554432")
        out("option name Threads type spin default 1 min 1 max 1024")
        out("option name HugeHash type spin default 1 min 1 max 4294967295")
        out("option name Clear Hash type button")
        out("option name Ponder type check default false")
        out("option name Style type combo default Normal var Solid var Normal var Risky")
        out("option name Note type string default hello")
        out("uciok")
    elif line.startswith("isready"):
        out("readyok")
    elif line == "quit":
        break
