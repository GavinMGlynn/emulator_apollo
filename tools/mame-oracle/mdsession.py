#!/usr/bin/env python3
"""Drive a long interactive session with the oracle's boot PROM.

`oracle.py` runs the machine to a fixed point in emulated time and dumps state.
That shape cannot install an operating system: the install is a *conversation*,
several stages long, where what to send next depends on what the machine just
said, and where the machine must stay alive between stages because its state
lives in RAM and on the disk being built (`FINDINGS.md` C48).

So this is the other driver: it holds the session open, reads the console, and
sends the next command when the machine asks for it.

## The two things that make it work

**stdin is a pty, not a pipe.** `apollo_stdio_device::poll_timer` reads stdin in
a `while` loop until `read` stops returning 1. Given a pipe with a script
already in it, the first callback drains the lot and then sees EOF -- the whole
conversation delivered in one instant, seconds before the firmware's autobaud
runs, and discarded (`FINDINGS.md` C45). The recipe that first got MD to talk
worked around this by feeding one character every 0.4 s from a shell loop, which
keeps the pipe from ending but is a fixed pace guessing at the machine's.

A pty ends the guessing rather than tuning it. `read` on an empty pty returns
`EAGAIN`, never EOF, so the session stays open with nothing in flight, and a
command is written at the moment the prompt for it appears. The pace stops being
a parameter.

**stdout is the console.** `apollo_stdio_device::rcv_complete` writes each
received character straight to the process's stdout with `putchar` and flushes.
So MD's output needs no tap to read -- which is why `mdsession.lua` taps
nothing and prints its own notes to stderr. One transformation is applied by
MAME and is not ours to undo: `rcv_complete` **drops `\\r`**, so the `CR LF` line
ending `docs/references/MD.md` records byte-exact arrives here as `LF` alone.
The transcript this driver writes is therefore the console stream as MAME
presents it, not as the DUART carried it; `mdcapture.lua` remains the tool for
the second question.

## Determinism, and what is honestly not claimed

`oracle.py`'s flags are reproduced, and for the same reasons -- `-noreadconfig`
and the redirected nvram/cfg/state/diff directories keep a run from depending on
what the last one left behind.

But a run of this driver is **not** reproducible in the sense an oracle reading
is. It is paced by the host: a command is sent when the console shows a prompt,
so the emulated time at which it arrives depends on how fast the host got there.
That is inherent to a conversation with a machine that waits for input, and it
is why the *product* of this tool is a disk image and a transcript rather than a
figure. Nothing timed may be measured through it.

Exit status 0 when the script ran to its end, 1 on a failed expectation or a
machine that died, 2 on a usage or environment problem.
"""

from __future__ import annotations

import argparse
import errno
import os
import pty
import re
import select
import shutil
import subprocess
import sys
import threading
import time
import tty
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent

MAME_NAMES = ("apollo", "mameapollo")
DEFAULT_ROMS = HERE / "out" / "roms"
DEFAULT_RUNDIR = HERE / "out" / "session"
SESSION_LUA = HERE / "mdsession.lua"

# 348 MB, the size the install procedure names (`FINDINGS.md` C47). Recent MAME
# does not create it -- the wiki's claim that it is generated automatically does
# not hold for this build, which refuses and creates nothing -- so the driver
# makes it, in the one command that was verified to work.
DISK_BYTES = 348 * 1024 * 1024


def find_mame(explicit: Path | None) -> Path:
    if explicit is not None:
        if not explicit.is_file():
            sys.stderr.write("mdsession: no MAME binary at %s\n" % explicit)
            raise SystemExit(2)
        return explicit
    for name in MAME_NAMES:
        candidate = REPO / "ext" / "mame" / name
        if candidate.is_file():
            return candidate
    sys.stderr.write(
        "mdsession: no oracle binary in ext/mame. Build it first; see "
        "oracle.py for the command and its memory budget.\n"
    )
    raise SystemExit(2)


def _die_with_parent():
    """Ask the kernel to kill this child when its parent dies.

    The **backstop**, covering the one case a signal handler cannot: the driver
    killed with `SIGKILL`, where no code of ours runs at all. Linux-only --
    `prctl` is a Linux facility -- and a silent no-op elsewhere, because failing
    to arm a safety net is not a reason to refuse to run. The portable path is
    `_install_signal_handlers`, and that is the one under test.
    """
    try:
        import ctypes
        import signal
        PR_SET_PDEATHSIG = 1
        ctypes.CDLL("libc.so.6", use_errno=True).prctl(
            PR_SET_PDEATHSIG, signal.SIGTERM, 0, 0, 0)
    except Exception:
        pass


def _install_signal_handlers(session):
    """Stop the emulator when the driver is asked to stop.

    `close()` covers every path the driver controls and none of the ones that
    matter. A driver killed from outside -- a timeout, a `pkill`, a terminal
    going away, an operator's Ctrl-C -- skips it, and leaves a MAME running with
    nobody reading its console.

    That is not merely untidy. An orphan holds its log file open, so the next
    run opens the same path and the two write at different offsets: the file
    fills with runs of NUL between interleaved fragments, and the new run's
    transcript is quietly corrupt while looking like a machine emitting garbage.
    Two orphans did exactly that here, and the wrong diagnosis was convincing.

    `SIGTERM` and `SIGINT` are what actually arrive in those cases, and handling
    them works on every platform. `SIGKILL` cannot be handled anywhere, which is
    what `_die_with_parent` is for.
    """
    import signal

    def stop(signum, frame):
        try:
            session.close()
        finally:
            # The conventional encoding, and it matters: a caller waiting on
            # this process should be able to tell it was signalled.
            raise SystemExit(128 + signum)

    for signum in (signal.SIGTERM, signal.SIGINT):
        try:
            signal.signal(signum, stop)
        except (ValueError, OSError):
            # Not the main thread, or the platform refuses. The backstop and
            # the ordinary `finally` still apply.
            pass


class Session:
    """A live machine, its console, and the ability to answer it."""

    def __init__(self, command, cwd, environment, log: Path | None,
                 echo: bool = True, swapfile: Path | None = None):
        # The pty is the whole point (see the module docstring): stdin must
        # never reach EOF, or the firmware stops being able to hear us.
        self.master, slave = pty.openpty()

        # Raw, and both halves of that matter.
        #
        # A pty comes up in canonical mode with ICRNL, which rewrites every
        # `\r` into `\n` on its way to the machine. MD's line terminator is
        # `\r`, so a session run over a cooked pty is sending something other
        # than what it says it sends -- here it survives only because
        # `apollo_stdio_device::poll_timer` happens to map `\n` back to `\r`,
        # which is a property of one MAME device and not a thing to rest a
        # harness on.
        #
        # And it comes up with ECHO on, which copies everything written to the
        # master straight back into the master's own read buffer. Nothing here
        # ever reads that -- the console arrives on stdout, not on the pty --
        # so the buffer only fills, and a long enough install would eventually
        # block in `os.write` with the machine waiting for input that the
        # driver can no longer send. The failure would arrive hours in and look
        # like the machine hanging.
        tty.setraw(slave)
        self.proc = subprocess.Popen(
            command, cwd=str(cwd), env=environment,
            stdin=slave, stdout=subprocess.PIPE, stderr=None,
            preexec_fn=_die_with_parent,
        )
        os.close(slave)

        self.buffer = bytearray()
        self.cursor = 0
        self.lock = threading.Lock()
        self.echo = echo
        self.log = open(log, "wb") if log is not None else None
        self.closed = False
        self.swapfile = swapfile
        self.swap_sequence = 0
        # Where `!swap ctape` stages its copies. Derived rather than passed
        # because it must be the *same* run directory MAME was given, and the
        # swap channel already establishes which that is.
        self.media_dir = swapfile.parent if swapfile is not None else None

        self.reader = threading.Thread(target=self._read_console, daemon=True)
        self.reader.start()

    def _read_console(self):
        # Whatever is available, never by lines. A prompt is a bare ">" with
        # nothing after it: MD prints it and then waits, so the line it sits on
        # has no end until the answer comes back, and a `readline` would hold
        # the prompt back until the thing it is prompting for had happened.
        stream = self.proc.stdout
        while True:
            chunk = stream.read1(4096) if hasattr(stream, "read1") \
                else stream.read(1)
            if not chunk:
                break
            with self.lock:
                self.buffer += chunk
            if self.log is not None:
                self.log.write(chunk)
                self.log.flush()
            if self.echo:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
        self.closed = True

    def text(self) -> str:
        with self.lock:
            return self.buffer[self.cursor:].decode("latin-1")

    def expect(self, pattern: str, timeout: float) -> str:
        """Wait for `pattern` and consume the console up to its end.

        Consuming is what makes a repeated prompt usable: every MD command ends
        at a ">" and a session that matched against the whole history would
        satisfy the next expectation with the previous stage's prompt and send
        its command into a machine that is still busy.
        """
        expression = re.compile(pattern, re.S)
        deadline = time.monotonic() + timeout
        while True:
            with self.lock:
                window = self.buffer[self.cursor:].decode("latin-1")
                match = expression.search(window)
                if match is not None:
                    self.cursor += match.end()
                    return match.group(0)
            if self.proc.poll() is not None and self.closed:
                raise SessionError(
                    "the machine exited (status %s) while waiting for %r"
                    % (self.proc.returncode, pattern), window)
            if time.monotonic() >= deadline:
                raise SessionError(
                    "timed out after %.0fs waiting for %r" % (timeout, pattern),
                    window)
            time.sleep(0.05)

    def send(self, text: str, char_delay: float = 0.0):
        # Everything already received is now *old*. Anything waited for after
        # this send must be produced in answer to it, so the cursor jumps to the
        # end of the buffer before a byte goes out.
        #
        # Without this, `expect` and `knock` can be satisfied by a prompt that
        # was already sitting unread -- and they were, twice, in the same
        # session. `shut` left a prompt nobody consumed; the `!knock` after the
        # following `re` matched *that* and returned instantly, so `ex domain_os`
        # was sent into a machine that had just reset and was deaf, and vanished.
        # Then the same thing through `expect`, and the command arrived as
        # `eomain_os` -- half of it typed into a machine that was still busy.
        #
        # The failure is nasty because it does not look like a synchronisation
        # bug. It looks like the *machine* mangling input, and the transcript
        # shows a corrupted command with no hint of why.
        with self.lock:
            self.cursor = len(self.buffer)
        data = text.encode("latin-1")
        if char_delay <= 0:
            os.write(self.master, data)
            return
        for byte in data:
            os.write(self.master, bytes([byte]))
            time.sleep(char_delay)

    def knock(self, pattern: str, timeout: float, every: float = 0.4,
              char: str = "\r") -> str:
        """Send carriage returns until the machine answers.

        A prompt that follows a reset is the one exchange that cannot be driven
        by expectation, because until the autobaud completes there is nothing to
        expect: the firmware is cycling clock-select rates waiting for a
        character to decode, and it has to be given characters *while* it does
        (`FINDINGS.md` C45). After that, every send is answer-driven.

        **0.4 s, and it is C45's measured interval rather than a round number.**
        The rate is load-bearing. An earlier version knocked every two seconds,
        which reached the power-on prompt every time and got a sign-on out of
        `re` **once in four runs** -- the one success arriving first, which is
        the worst order for it. The autobaud after a reset is evidently a
        narrower window than the one the machine sits in at power-on.

        Worth stating because of the shape of the failure: too slow a knock does
        not degrade, it just misses, and a missed window is indistinguishable
        from a machine that died in the reset. Three runs were spent looking for
        a fault in the reset path before the interval -- the one number here
        that had been picked rather than measured -- was suspected.

        **The character matters too, and it must be the carriage return.** C45
        says the autobaud needs "a character"; it needs *this* character.
        Knocking with a space followed by a carriage return never reaches the
        prompt at all -- not after a reset, not even at power-on -- where the
        carriage return alone reaches it every time. Measured as an A/B pair,
        which is why `--knock-char` exists: the claim is testable rather than
        asserted. The firmware is matching a known byte to decide whether a
        candidate rate decoded correctly, so the first byte it sees has to be
        the one it is looking for.
        """
        deadline = time.monotonic() + timeout
        while True:
            self.send(char)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SessionError(
                    "no prompt after %.0fs of knocking" % timeout, self.text())
            try:
                return self.expect(pattern, min(every, remaining))
            except SessionError:
                if self.proc.poll() is not None and self.closed:
                    raise

    def swap(self, name: str, path: str, timeout: float = 60.0) -> str:
        """Change a mounted medium while the machine runs.

        `MINST` takes four cartridges in turn, so a driver that can only mount
        one at startup is a driver that stops after the first. MAME's Lua can
        load an image mid-run and the cartridge device does not reset on load
        (`sc499_ctape_image_device` inherits `magtape_image_device`, whose
        `is_reset_on_load` is false), so the capability is there -- what was
        missing was a way to ask for it.

        Two files, because the driver and the script share nothing else: MAME's
        stdout is the console and its stderr is not readable from here. The
        sequence number is what makes waiting meaningful -- the same tape can be
        asked for twice, and without it the second request would be satisfied by
        the first one's acknowledgement.
        """
        if self.swapfile is None:
            raise SessionError("this session has no swap channel")

        self.swap_sequence += 1
        sequence = self.swap_sequence
        ack = Path(str(self.swapfile) + ".ack")
        if ack.exists():
            ack.unlink()
        self.swapfile.write_text("%d\n%s\n%s\n" % (sequence, name, path))

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if ack.exists():
                try:
                    lines = ack.read_text().splitlines()
                except OSError:
                    lines = []
                if len(lines) >= 2 and lines[0].strip() == str(sequence):
                    status = lines[1].strip()
                    if not status.startswith("ok"):
                        raise SessionError("swap %s -> %s: %s"
                                           % (name, path, status))
                    return status
            if self.proc.poll() is not None and self.closed:
                raise SessionError("the machine exited during a swap")
            time.sleep(0.2)
        raise SessionError("no answer to swap %s -> %s within %.0fs"
                           % (name, path, timeout))

    def close(self):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()
        try:
            os.close(self.master)
        except OSError as exc:
            if exc.errno != errno.EBADF:
                raise
        if self.log is not None:
            self.log.close()


class SessionError(Exception):
    def __init__(self, message, window=""):
        super().__init__(message)
        self.window = window


# The MD prompt, from `docs/references/MD.md`: a bare ">" with no trailing
# space, preceded by a blank line. Anchored on the two line endings so a ">"
# inside a utility's own output cannot be mistaken for it -- and with the CR
# dropped by `rcv_complete`, what arrives here is "\n\n>".
MD_PROMPT = r"\n\n>"

# Stages, as data. Each step is (how to wait, what to wait for, what to send).
#
# "knock" rather than "expect" after every `re`, and that is not belt and
# braces. `re` is *Reset System*: the machine resets, and the firmware's
# autobaud starts over from nothing, so the console goes deaf again exactly as
# it was at power-on. A step that merely waited for the sign-on would be waiting
# for output the machine cannot produce until something is typed at it.
#
# The sequence is the install procedure of `FINDINGS.md` C47, with one addition
# the MAME wiki does not carry: `RE` is sent **twice**. The Apollo Survival
# Guide is explicit that a reset should be run twice before a standalone
# utility, to "clear and reset the memory management hardware to default
# settings", and a utility loaded over a half-reset MMU is exactly the kind of
# failure that would present as a corrupt tape.
STAGES = {
    "prompt": [],
    # One character, then silence. Every other stage *knocks* -- sends
    # repeatedly until the MD prompt answers -- and that interrupts the boot, so
    # every session this harness has run has been a machine stopped on its way
    # up. A self-test sequence cannot be compared that way.
    #
    # But nor can it be watched in total silence, which is what this stage did
    # first and why it captured eleven minutes of nothing: **the boot PROM
    # autobauds**. It cannot transmit until it has received a character and
    # learnt the rate, so a console nobody types at never says anything at all.
    # One carriage return is what the headless frontend sends for the same
    # reason, and it is enough -- our own machine runs every self-test after it.
    #
    # Pair with `APOLLO_MD_POST=none`, which stops the Lua side pressing a key
    # of its own, and `--settle` long enough for the tests to run.
    "watch": [],
    "invol": [
        ("send",   None,      "re\r"),
        ("knock",  MD_PROMPT, "re\r"),
        ("knock",  MD_PROMPT, "di c\r"),
        ("expect", MD_PROMPT, "ex invol\r"),
    ],
}


def follow_commands(session: Session, path: Path, timeout: float,
                    poll: float = 0.5, limit: float = 0.0,
                    knock_char: str = "\r") -> None:
    """Send lines from a file as they are appended to it.

    The stages above can only be written once the dialogue they answer is
    known, and INVOL's is not published anywhere this project has found -- the
    MAME wiki gives the option *numbers* and neither it nor the handbook gives
    the prompts between them. Learning them means reading the machine's output
    and answering it, and the cost of getting that wrong is high: reaching
    INVOL's first menu takes ten minutes of emulated cartridge scan, and killing
    the session to edit a script pays that again.

    So the script becomes a file the session *follows*. An operator -- a person
    at a terminal, or a program working a turn at a time -- appends the next
    answer once it has read the last response, and the machine never stops.
    That is the same shape as the stage tables, with the authoring moved to run
    time, which is what an undocumented dialogue requires.

    Directives, one per line:

      # ...            a comment
      !expect REGEX    wait for it before reading the next line
      !knock REGEX     send carriage returns until it appears. What a stage
                       needs after `re`, since a reset leaves the machine deaf
      !wait SECONDS    let the machine run, sending nothing
      !raw TEXT        send exactly this, with no carriage return added.
                       `\\r`, `\\n`, `\\t` and `\\xNN` are interpreted. The last
                       one is not a convenience: `\\x04` -- EOT -- is what leaves
                       the RBAK environment's `sh`, where `exit` echoes and the
                       prompt returns, and `shut` is not a shell command
      !cr              send a bare carriage return: an *empty* answer
      !swap NAME PATH  change a mounted medium without stopping the machine,
                       and wait for the script to confirm it. NAME alone
                       ejects. A `ctape` is staged into the run directory
                       first, so the guest cannot write on our only copy of it
                       -- see `stage_cartridge`
      !exit            ask the machine to shut down **cleanly**, and wait for
                       it. NVRAM -- which holds the calendar's battery
                       configuration table, and so the node ID -- is written on
                       this path and not on `!quit`'s
      !quit            end the session
      anything else    sent as typed, with a carriage return

    `!cr` is not a convenience. A blank line is a real answer on this machine --
    INVOL ends its badspot list with one, and a disk with no badspots is
    answered entirely by pressing return. A blank line in the file cannot mean
    it, because a file being appended to a line at a time is full of incomplete
    blank lines, so the empty answer needs a name of its own.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()
    offset = 0
    started = time.monotonic()
    pending = ""

    while True:
        if limit > 0 and time.monotonic() - started > limit:
            sys.stderr.write("mdsession: command file timeout, ending\n")
            return

        # Read as **bytes** and decode latin-1, tracking a plain byte offset.
        #
        # Two reasons, and the second cost a session. Text mode applies
        # universal-newline handling, which turns a `\r` in the file into a line
        # break and silently splits a directive carrying a carriage return --
        # `newline=""` fixed that. But text mode also decodes as UTF-8 and
        # `tell()` returns an opaque cookie rather than a byte position, and
        # seeking a cookie from one handle into a freshly opened one on a file
        # that is being appended to is not a contract Python offers. It raised
        # `UnicodeDecodeError` on a byte that is not in the file, killed the
        # driver, and took the emulator with it -- at MINST's first tape prompt,
        # which is forty minutes in.
        #
        # latin-1 cannot fail: every byte maps to a character. A command file is
        # a control channel, and a control channel that can be killed by its own
        # contents is worse than no channel at all.
        with open(path, "rb") as handle:
            handle.seek(offset)
            raw = handle.read()
            offset += len(raw)
        chunk = raw.decode("latin-1")

        if not chunk:
            # The machine is checked here too, not only after a command. With
            # the check only on the command path, a driver whose emulator had
            # died went on polling an idle file forever -- and because it never
            # exited, it never reaped the child, which then sat as a zombie
            # looking like a second live emulator. Seen twice.
            if session.closed:
                sys.stderr.write("mdsession: the machine exited\n")
                return
            time.sleep(poll)
            continue

        pending += chunk
        # A trailing partial line is kept back rather than sent: a file being
        # appended to can be read between the text and its newline, and half a
        # command is a different command.
        lines = pending.split("\n")
        pending = lines.pop()

        for line in lines:
            line = line.rstrip("\r")
            if not line or line.startswith("#"):
                continue
            if line.startswith("!expect "):
                sys.stderr.write("mdsession: expect %r\n" % line[8:])
                session.expect(line[8:], timeout)
            elif line.startswith("!knock "):
                # After `re` the machine is deaf again and an expectation would
                # wait for output it cannot produce. Every stage that resets has
                # to knock, so the directive exists rather than being a stage's
                # private trick.
                sys.stderr.write("mdsession: knock for %r\n" % line[7:])
                session.knock(line[7:], timeout, char=knock_char)
            elif line.startswith("!wait "):
                time.sleep(float(line[6:]))
            elif line.startswith("!raw "):
                # `\xNN` as well as the three named escapes, because the byte
                # this dialogue actually needs has no name: **EOT, `\x04`, is
                # what leaves the RBAK environment's `sh`**. `exit` does not --
                # it echoes and the `$` comes straight back -- and `shut` is not
                # a shell command at all, so an install that runs MINST inside
                # that shell could not shut the guest down, and every volume it
                # made came out with an uncommitted directory (`FINDINGS.md`
                # C192).
                text = (line[5:].replace("\\r", "\r").replace("\\n", "\n")
                        .replace("\\t", "\t"))
                text = re.sub(r"\\x([0-9A-Fa-f]{2})",
                              lambda m: chr(int(m.group(1), 16)), text)
                sys.stderr.write("mdsession: send %r\n" % text)
                session.send(text)
            elif line == "!cr":
                sys.stderr.write("mdsession: send %r (empty answer)\n" % "\r")
                session.send("\r")
            elif line.startswith("!swap "):
                # "!swap NAME PATH", or "!swap NAME" to eject. The path is
                # resolved here rather than in the script, because MAME runs
                # from its own directory and a relative path would mean a
                # different file at each end.
                # `medium`, never `path`: `path` is this function's parameter
                # naming the *command file*. Assigning to it here rebound it,
                # and from the next poll onward the driver read the cartridge
                # image as its command file and typed 58 Mbyte of tape at the
                # machine -- 308,250 sends, 150 Mbyte of log, and the real
                # command file silently never read again.
                parts = line[6:].split(None, 1)
                name = parts[0]
                medium = ""
                if len(parts) > 1:
                    source = Path(parts[1]).resolve()
                    # Staged for the same reason `--ctape` is, and it matters
                    # more here: MINST swaps four cartridges in, so an
                    # unstaged swap exposes four media instead of one. Only
                    # cartridges -- a disk swapped mid-run is being written on
                    # purpose. See `stage_cartridge`.
                    if name.startswith("ctape") and session.media_dir \
                            and source.is_file():
                        source = stage_cartridge(source, session.media_dir)
                    elif name.startswith("ctape") and not source.is_file():
                        # Passed through unstaged, and said so. A cartridge
                        # that is not there is an operator error, and the
                        # error belongs to the image layer -- MAME answers
                        # "load failed" for it, uniformly with every other
                        # unloadable medium. Copying it here would replace that
                        # with a traceback from the stager.
                        sys.stderr.write(
                            "mdsession: %s does not exist; not staged\n"
                            % source)
                    medium = str(source)
                sys.stderr.write("mdsession: swap %s -> %s\n" % (name, medium))
                sys.stderr.write("mdsession: %s\n"
                                 % session.swap(name, medium, timeout))
            elif line == "!exit":
                # A **clean** shutdown, which `!quit` is not. `Session.close`
                # sends SIGTERM and MAME does not write NVRAM on that path --
                # every run directory kept here came back with an empty
                # `nvram/`. The machine's configuration table lives in the
                # calendar's battery RAM (`002398-04` p. 12-3), so a node ID set
                # with `ex config` survived only inside the run that set it,
                # which is what stopped a second ring node from being made.
                #
                # A file beside the swap channel, for the same reason that one is
                # a file: the two processes share nothing else, and the Lua side
                # already polls there on an emulated-time budget.
                if session.swapfile is None:
                    raise SessionError("this session has no exit channel")
                Path(str(session.swapfile) + ".exit").write_text("exit\n")
                sys.stderr.write("mdsession: !exit -- clean shutdown requested\n")
                deadline = time.monotonic() + 60.0
                while time.monotonic() < deadline:
                    if session.proc.poll() is not None:
                        sys.stderr.write("mdsession: the machine exited cleanly\n")
                        return
                    time.sleep(0.2)
                sys.stderr.write("mdsession: no clean exit after 60s\n")
                return
            elif line == "!quit":
                sys.stderr.write("mdsession: !quit\n")
                return
            else:
                sys.stderr.write("mdsession: send %r\n" % line)
                session.send(line + "\r")

        if session.closed:
            sys.stderr.write("mdsession: the machine exited\n")
            return


def make_disk(path: Path) -> bool:
    if path.exists():
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as handle:
        handle.truncate(DISK_BYTES)
    return True


# `:apollo_config`'s `Node ID from Disk` bit and its default, from
# `apollo.h:317` and `apollo_m.cpp:102`. Both are needed: MAME matches a saved
# port by tag, mask *and* default value, and silently ignores a node that
# disagrees with the machine it is loading.
APOLLO_CONF_NODE_ID = 0x0100

# MAME's system config, written before the machine starts.
#
# **`mdsession.lua` sets this same field and cannot set it in time.** The Lua
# side runs from a periodic callback, so its first chance to write
# `:apollo_config` is after `MACHINE_RESET` has already run once with the
# defaults; the file knows this and soft-resets so the firmware re-reads the
# configuration it wanted. That repairs every field that is *re-read* at each
# reset -- Normal/Service, the era shift, the trace flags -- and it cannot
# repair this one, because `Node ID from Disk` is not read by anything. It is
# acted on: `apollo.cpp:911` calls `apollo_ni::set_node_id_from_disk()`, which
# overwrites the node-ID device's value from the label of the disk on unit 0.
# A later reset with the setting Off does not put back what the first reset
# destroyed.
#
# What that cost is `FINDINGS.md` C146 through C149: `-node_id` loaded an image
# for node `22222`, `apollo_ni::call_load` accepted it, and every volume INVOL
# then wrote recorded `12345` -- the node of the *ancestor* volume every image
# here is copied from. Three separate sources were eliminated by measurement
# before the fourth turned out to be the harness stating a configuration it had
# not managed to apply.
#
# Measured both ways, same volume and same `-node_id` image, differing only in
# which unit the labelled disk was on (`FINDINGS.md` C151):
#
#     unit 0, no cfg     node-ID ROM reads 01 23 45   the disk's node
#     unit 1, no cfg     node-ID ROM reads 02 22 22   the ROM image's node
#
# So the rule is general and worth stating once: **a setting whose effect is a
# one-way write has to be in place before the first reset, and a cfg file is
# the only place that is.** Anything the firmware merely re-reads can wait for
# the Lua.
MAME_CONFIG_XML = """<?xml version="1.0"?>
<mameconfig version="10">
    <system name="%s">
        <input>
            <port tag=":apollo_config" type="CONFIG" mask="%d" defvalue="%d" value="%d" />
        </input>
    </system>
</mameconfig>
"""


def write_config(rundir: Path, machine: str, node_id: Path | None) -> Path:
    """Plant MAME's system config, so the *first* reset sees it.

    Named for the machine because that is what MAME looks for -- `dn3500.cfg`
    for `dn3500` -- and written into the run directory's own `cfg`, which
    `build_command` already points `-cfg_directory` at. A run therefore
    configures itself and leaves nothing behind for the next one.

    **Off only when a node-ID image is supplied**, and the file is written
    either way. Taking the node from the disk is the right behaviour for a run
    that does not name one: it is how a machine with a volume from some other
    node presents the ID that volume records, which is what this project's own
    `node_id_from_volume` does for the same reason. Turning it off across the
    board would silently give such a run MAME's `DEFAULT_NODE_ID` instead --
    trading one wrong node for another. `--node-id` is the caller saying which
    source they mean, so it is the thing that decides.

    Written in both cases because a configuration that is only stated when it
    is interesting is one nobody can confirm from the run: the file is the
    answer to "what did this run actually get", and an absent file is not one.
    """
    off = node_id is not None
    path = rundir / "cfg" / ("%s.cfg" % machine)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(MAME_CONFIG_XML
                    % (machine, APOLLO_CONF_NODE_ID, APOLLO_CONF_NODE_ID,
                       0 if off else APOLLO_CONF_NODE_ID))
    return path


def stage_cartridge(source: Path, rundir: Path) -> Path:
    """Copy a cartridge into the run directory and return the copy.

    **MAME mounts a cartridge read/write and a guest that writes to it writes
    to our file.** `sc499_device::write_block` is an `fseek`/`fwrite` straight
    into the image, so a `-ctape` argument naming a file in `media/` hands the
    guest a pen and our only copy of the medium.

    That is not hypothetical and it cost most of a thread. A *successful*
    SR10.3 boot overwrote exactly block 0 of
    `019439-001.CRTG_PSKQ3_91_BOOT_1` -- the one block carrying the
    `SYSBOOT REV`/`0013D800` descriptor the boot PROM validates -- so the
    cartridge booted twice and then reported `error: sysboot not found`
    forever. Six emulator-side hypotheses were eliminated (device select, tape
    position, media change, autobaud, clock era, volume state) and the volume
    was `cmp`-ed before and after a run, because the *cartridge* was an input
    and inputs are not suspected. Re-fetched from bitsavers, exactly one
    512-byte block of 50,727,936 differed.

    So a run gets a copy and the medium in `media/` is never opened by the
    emulator. A cartridge is write-protected media on the real machine anyway;
    the copy is what makes that true here, rather than a claim about what a
    guest ought to do.

    The **disk** is deliberately not staged: an install writes to its volume
    and the checkpoints in `media/` are those writes. The asymmetry is the
    hardware's -- a tape is read, a disk is written.
    """
    staged = rundir / "media" / source.name
    staged.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, staged)
    return staged


def build_command(mame: Path, args, rundir: Path) -> list:
    command = [
        str(mame),
        args.machine,
        "-noreadconfig",
        "-rompath", str(args.roms),
        "-video", "none",
        "-sound", "none",
        "-nothrottle",
        "-autoboot_script", str(SESSION_LUA),
    ]
    if args.disk is not None:
        command += ["-disk1", str(args.disk)]
    if args.ctape is not None:
        command += ["-ctape", str(stage_cartridge(args.ctape, rundir))]
    if args.node_id is not None:
        # Not staged: `apollo_ni` never writes it -- its `write` handler logs
        # "Error: writing node id ROM" and stores nothing -- so unlike a
        # cartridge there is nothing for a guest to damage.
        command += ["-node_id", str(args.node_id)]
    for option in ("nvram", "cfg", "state", "diff", "snapshot", "input"):
        command += ["-%s_directory" % option, str(rundir / option)]
    command += args.mame_args
    return command


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Drive an interactive boot-PROM session under the oracle.")
    parser.add_argument("--machine", default="dn3500")
    parser.add_argument("--stage", default="prompt", choices=sorted(STAGES),
                        help="which scripted sequence to run")
    parser.add_argument("--disk", type=Path,
                        help="the .awd disk image; created at 348 MB if absent")
    parser.add_argument("--ctape", type=Path, help="the .ct cartridge to mount")
    parser.add_argument("--mame", type=Path)
    parser.add_argument("--roms", type=Path, default=DEFAULT_ROMS)
    parser.add_argument("--rundir", type=Path, default=DEFAULT_RUNDIR)
    parser.add_argument("--log", type=Path,
                        help="write the raw console stream here")
    parser.add_argument("--timeout", type=float, default=300.0,
                        help="seconds to wait for any one expectation")
    parser.add_argument("--knock-timeout", type=float, default=180.0,
                        help="seconds to spend reaching the first prompt")
    parser.add_argument("--knock-char", default="\r",
                        help="the character to knock with. Exposed so the "
                             "claim that it must be a carriage return can be "
                             "tested rather than asserted")
    parser.add_argument("--commands", type=Path,
                        help="a file to follow: lines appended to it are sent "
                             "as they appear, so a session outlives the script "
                             "that started it")
    parser.add_argument("--commands-timeout", type=float, default=0.0,
                        help="seconds to follow the command file before "
                             "ending; 0 means until !quit or the machine dies")
    parser.add_argument("--settle", type=float, default=3.0,
                        help="seconds to keep the machine running after the "
                             "last send, so it can actually read it")
    parser.add_argument("--hold", type=float, default=0.0,
                        help="seconds to keep reading after the script ends, "
                             "sending nothing. This is how an unrecorded "
                             "dialogue is learnt rather than guessed at.")
    parser.add_argument("--node-id", type=Path,
                        help="a 32-byte node-ID ROM image for MAME's "
                             "`-node_id` slot, as `nodeid.py` writes. Without "
                             "one the oracle uses its compiled-in "
                             "`DEFAULT_NODE_ID` of 0x12345 -- so two volumes "
                             "with different node IDs, which two nodes on one "
                             "ring require, need this")
    parser.add_argument("--mode", default="Normal",
                        choices=("Normal", "Service"),
                        help="the NORMAL/SERVICE switch. A documented "
                             "prerequisite rather than a preference: "
                             "`001746-06` Procedure 2-7 step 2 puts it on "
                             "SERVICE before `EX CALENDAR` and step 5 returns "
                             "it to NORMAL before rebooting")
    parser.add_argument("--era", default="25", choices=("25", "30", "none"),
                        help="the driver's year shift. C47's install procedure "
                             "wants 25, and a volume this project built wants "
                             "none -- Domain/OS refuses a clock 24 years "
                             "behind its own last-shutdown stamp (C53)")
    parser.add_argument("--keep-rundir", action="store_true",
                        help="do not wipe the run directory first")
    parser.add_argument("mame_args", nargs="*",
                        help="extra arguments passed through to MAME")
    args = parser.parse_args(argv)

    mame = find_mame(args.mame)

    # Resolved before anything uses them, because MAME is run from its own
    # directory -- `oracle.py` does the same, so that a relative -rompath means
    # what the caller meant. A relative image path would otherwise be created
    # here and looked for under ext/mame, and the failure says only "No such
    # file or directory" about a file that plainly exists.
    if args.disk is not None:
        args.disk = args.disk.resolve()
    if args.ctape is not None:
        args.ctape = args.ctape.resolve()
    if args.node_id is not None:
        args.node_id = args.node_id.resolve()
    # The run directory too, and for a second reason beyond MAME's own
    # `-nvram_directory` and friends: the swap channel is a *file* in it that
    # both processes must name identically. Left relative, the driver writes it
    # here and the script looks for it under ext/mame, and the swap simply never
    # happens -- which the driver can only report as a timeout.
    args.rundir = args.rundir.resolve()
    args.roms = args.roms.resolve()

    if args.disk is not None and make_disk(args.disk):
        sys.stderr.write("mdsession: created %s (%d bytes)\n"
                         % (args.disk, DISK_BYTES))

    rundir = args.rundir
    if rundir.exists() and not args.keep_rundir:
        shutil.rmtree(rundir)
    rundir.mkdir(parents=True, exist_ok=True)

    environment = dict(os.environ)
    environment.setdefault("APOLLO_MD_POST", "Numpad Enter")
    environment["APOLLO_MD_ERA"] = args.era
    environment["APOLLO_MD_MODE"] = args.mode

    swapfile = rundir / "swap"
    environment["APOLLO_MD_SWAPFILE"] = str(swapfile)

    # **A stale exit request would kill the next run before it started.**
    # `!exit` leaves its request file behind, and `--keep-rundir` is exactly the
    # case where a configuration is set in one run and used by the next -- so the
    # second machine polled, found the first one's request, and exited with
    # status 0 having sent nothing. Removed here rather than by the Lua side,
    # because the driver is what knows a *new* run is starting.
    exitfile = Path(str(swapfile) + ".exit")
    if exitfile.exists():
        exitfile.unlink()

    sys.stderr.write("mdsession: config %s (node from %s)\n"
                     % (write_config(rundir, args.machine, args.node_id),
                        "the -node_id image" if args.node_id is not None
                        else "the disk, as MAME defaults"))

    command = build_command(mame, args, rundir)
    sys.stderr.write("mdsession: %s\n" % " ".join(command))

    session = Session(command, cwd=mame.parent, environment=environment,
                      log=args.log, swapfile=swapfile)
    _install_signal_handlers(session)
    status = 0
    try:
        if args.stage == "watch":
            # One character to autobaud the port, and then nothing: see STAGES.
            # Deliberately *not* `knock`, which repeats until it sees a prompt
            # and so stops the machine on its way up.
            sys.stderr.write("mdsession: watching, not knocking\n")
            session.send(args.knock_char)
        else:
            session.knock(MD_PROMPT, args.knock_timeout,
                          char=args.knock_char)
            sys.stderr.write("mdsession: at the MD prompt\n")

        for index, (mode, pattern, text) in enumerate(STAGES[args.stage],
                                                      start=1):
            if mode == "expect":
                session.expect(pattern, args.timeout)
            elif mode == "knock":
                session.knock(pattern, args.knock_timeout,
                              char=args.knock_char)
            sys.stderr.write("mdsession: [%d] send %r\n" % (index, text))
            session.send(text)

        # A send only puts bytes in the pty; the machine has to be running to
        # take them. Closing straight after the last send therefore kills the
        # emulator before it has read the command -- the script reports every
        # step done and the last one never happened, which is the worst shape a
        # failure can have. Waiting is the whole fix, and it costs seconds.
        time.sleep(args.settle)

        if args.commands is not None:
            sys.stderr.write("mdsession: following %s\n" % args.commands)
            follow_commands(session, args.commands, args.timeout,
                            limit=args.commands_timeout,
                            knock_char=args.knock_char)
            # Again, and for the same reason: `!quit` follows a command that
            # the machine has not necessarily read yet. The last line of an
            # install is `shut`, so losing it is losing the step that makes the
            # disk consistent.
            time.sleep(args.settle)

        if args.hold > 0:
            sys.stderr.write("mdsession: holding %.0fs, sending nothing\n"
                             % args.hold)
            deadline = time.monotonic() + args.hold
            while time.monotonic() < deadline and not session.closed:
                time.sleep(0.5)
    except SessionError as exc:
        sys.stderr.write("\nmdsession: %s\n" % exc)
        sys.stderr.write("mdsession: console since the last match was %r\n"
                         % exc.window[-400:])
        status = 1
    except KeyboardInterrupt:
        sys.stderr.write("\nmdsession: interrupted\n")
        status = 1
    except Exception as exc:
        # Deliberately broad. A driver fault used to propagate out of main(),
        # skip every diagnostic, and kill a machine that was forty minutes into
        # an install -- with a bare traceback as the only explanation. Naming it
        # as a driver fault, rather than something the machine did, is the whole
        # point.
        import traceback
        sys.stderr.write("\nmdsession: driver fault, not the machine: %s\n"
                         % exc)
        traceback.print_exc()
        status = 1
    finally:
        session.close()

    return status


if __name__ == "__main__":
    raise SystemExit(main())
