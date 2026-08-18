#!/usr/bin/env python3
"""Test mdsession.py's driving logic against a stub MAME.

The same split `test_oracle.py` makes, and for the same reason. Whether the
install *works* needs a real emulator, a real cartridge and a quarter of an
hour. Whether the driver reaches a prompt, sends the right commands in the right
order, refuses to mistake an old prompt for a new one, and fails loudly instead
of hanging is ordinary program logic that needs no MAME at all.

The stub is written to the shapes that actually bite here:

  - it is **deaf until spoken to**, like the firmware's autobaud: it prints
    nothing at all until it has received a character. A driver that waited for
    output before sending any would wait forever, and that is the failure the
    knock loop exists to prevent (`FINDINGS.md` C45);
  - it prints a **repeating prompt**, because every MD command ends at the same
    `>`. A driver matching against the whole history would satisfy each
    expectation with the previous command's prompt and run the whole script into
    a machine that never answered;
  - it can be told to **die**, because a machine that exits must be reported as
    such rather than waited out to the timeout;
  - it can be told to **say nothing ever**, because that is the hang this tool
    is most likely to meet and it must end in a message rather than in a stall.

    python3 tools/mame-oracle/test_mdsession.py
"""

from __future__ import annotations

import os
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
DRIVER = HERE / "mdsession.py"

failures = 0


def check(name: str, actual, expected) -> None:
    global failures
    if actual != expected:
        failures += 1
        sys.stderr.write("FAIL %s\n  expected: %r\n  actual:   %r\n"
                         % (name, expected, actual))
    else:
        sys.stdout.write("ok   %s\n" % name)


def check_in(name: str, needle: str, haystack: str) -> None:
    global failures
    if needle not in haystack:
        failures += 1
        sys.stderr.write("FAIL %s\n  %r not found in:\n%s\n"
                         % (name, needle, haystack))
    else:
        sys.stdout.write("ok   %s\n" % name)


# A stand-in for MAME with the stdio terminal compiled in. It speaks MD's
# format from docs/references/MD.md -- sign-on, then CR LF CR LF '>' for each
# carriage return -- and, like the real device, echoes what it receives.
#
# `MDSTUB_MODE` picks the behaviour under test; `MDSTUB_RECORD` names a file to
# append every received byte to, which is how the ordering of a stage is checked
# without parsing the driver's own log.
STUB = r"""#!/usr/bin/env python3
import os, sys

mode = os.environ.get("MDSTUB_MODE", "normal")
record = os.environ.get("MDSTUB_RECORD")

# The swap channel's other end, standing in for mdsession.lua. A real swap goes
# through MAME's image device; what is testable without MAME is the protocol --
# that a request is noticed, that the sequence number comes back, and that a
# failure is reported rather than swallowed.
swapfile = os.environ.get("APOLLO_MD_SWAPFILE")
swap_seen = -1
swap_log = os.environ.get("MDSTUB_SWAPLOG")

def poll_swap():
    global swap_seen
    if not swapfile or not os.path.exists(swapfile):
        return
    try:
        with open(swapfile) as handle:
            lines = handle.read().splitlines()
    except OSError:
        return
    if len(lines) < 2:
        return
    sequence = int(lines[0])
    if sequence <= swap_seen:
        return
    swap_seen = sequence
    name = lines[1]
    path = lines[2] if len(lines) > 2 else ""
    if os.environ.get("MDSTUB_SWAPFAIL") == "1":
        status = "load failed: stub refuses"
    elif name != "ctape":
        status = "no image named " + name
    else:
        status = "ok"
    if swap_log:
        with open(swap_log, "a") as handle:
            handle.write("%s %s\n" % (name, path))
    with open(swapfile + ".ack", "w") as handle:
        handle.write("%d\n%s\n" % (sequence, status))

# The era is passed to `mdsession.lua` through the environment, and the
# environment is the one thing a stub can read that a Lua script would.
envlog = os.environ.get("MDSTUB_ENVLOG")
if envlog:
    with open(envlog, "w") as handle:
        handle.write(os.environ.get("APOLLO_MD_ERA", "<unset>"))

if mode == "exitwatch":
    # Stands in for mdsession.lua's poll_exit: notice the request file beside the
    # swap channel and shut down. The real one calls manager.machine:exit(),
    # which is the path that writes NVRAM.
    import time as _t
    out = sys.stdout
    out.write("\nMD7C REV 8.00, 1989/08/16.17:23:52\n\n>")
    out.flush()
    target = (swapfile or "") + ".exit"
    while True:
        if swapfile and os.path.exists(target):
            sys.exit(0)
        _t.sleep(0.05)

if mode == "scribble":
    # Stands in for `sc499_device::write_block`, which is an fseek/fwrite
    # straight into whatever file `-ctape` named. A real guest did exactly this
    # to block 0 of the one bootable SR10.3 cartridge.
    ctape = sys.argv[sys.argv.index("-ctape") + 1]
    with open(ctape, "r+b") as handle:
        # What was there first, then the damage. Recording the *pre-existing*
        # bytes is what lets a test tell re-staging from inheritance: a second
        # scribble over a first one is indistinguishable from a fresh copy
        # otherwise.
        was = handle.read(12)
        handle.seek(0)
        handle.write(b"SCRIBBLE")
    with open(os.environ["MDSTUB_CTAPELOG"], "wb") as handle:
        handle.write(ctape.encode() + b"\n" + was)
    sys.exit(0)

if mode == "silent":
    # Reads and answers nothing, ever. The machine that hangs.
    while True:
        try:
            os.read(0, 1)
        except OSError:
            break
    sys.exit(0)

out = sys.stdout
signed_on = False
seen = 0
line = ""
# Characters still to be swallowed after a reset. The real machine is deaf
# while its autobaud hunts for a rate, and that window is exactly where a
# command sent too early disappears.
deaf = 0

import select

while True:
    # Polled rather than blocking, so a swap request is noticed while the
    # machine is sitting at a prompt with nothing being typed -- which is
    # exactly when a tape gets changed.
    poll_swap()
    ready, _, _ = select.select([0], [], [], 0.1)
    if not ready:
        continue
    try:
        data = os.read(0, 4096)
    except OSError:
        break
    if not data:
        break
    for byte in data:
        char = bytes([byte])
        if deaf > 0:
            # Deaf, like the firmware during its autobaud: the byte is not
            # heard at all. Not recorded either -- the record is what the
            # machine acted on, and a test that recorded discarded input would
            # report a command as delivered when it was thrown away.
            deaf -= 1
            continue
        if record:
            with open(record, "ab") as handle:
                handle.write(char)
        if not signed_on:
            # Deaf until spoken to: the sign-on is provoked, not spontaneous.
            signed_on = True
            out.write("\nMD7C REV 8.00, 1989/08/16.17:23:52\n")
        if char == b"\r":
            seen += 1
            if mode == "die" and seen >= 3:
                out.flush()
                sys.exit(4)
            if line.strip() == "re":
                # Reset System: the machine goes deaf again, exactly as the
                # real one does, and only a further knock brings it back.
                # Without this the stub cannot exercise the case that cost a
                # whole session (C50).
                signed_on = False
                deaf = 3
                line = ""
                continue
            line = ""
            out.write("\n\n>")
        else:
            line += char.decode("latin-1")
            out.write(char.decode("latin-1"))
        out.flush()
"""


def _children_of(pid: int):
    """Direct children of `pid`, from /proc. No dependency on `ps` output."""
    found = []
    try:
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                stat = (entry / "stat").read_text()
            except OSError:
                continue
            # The comm field is parenthesised and may hold spaces, so the
            # parent pid is counted from the closing bracket rather than by
            # splitting the whole line.
            fields = stat[stat.rfind(")") + 1:].split()
            if len(fields) >= 2 and fields[1] == str(pid):
                found.append(int(entry.name))
    except OSError:
        pass
    return found


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def write_stub(directory: Path) -> Path:
    path = directory / "apollo"
    path.write_text(STUB)
    path.chmod(path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
    return path


def run(stub: Path, extra, environment=None, timeout=60, append_after=None):
    env = dict(os.environ)
    env.update(environment or {})
    # A run directory of its own, beside the stub. The driver wipes whatever it
    # is given, and the default is the one a real session uses -- so a test left
    # on the default would delete a live install's nvram and cfg out from under
    # it, on a run that takes a quarter of an hour to reach its first prompt.
    extra = ["--rundir", str(stub.parent / "run")] + list(extra)
    # A short settle: the default is sized for a machine that has to be
    # scheduled and emulated before it reads what was written to it, and a stub
    # reads it immediately. Kept non-zero rather than removed, because zero
    # would stop this suite from covering the race the settle exists for.
    command = [sys.executable, str(DRIVER), "--mame", str(stub),
               "--settle", "0.5"] + list(extra)
    writer = None
    if append_after is not None:
        target, text, delay = append_after
        import threading

        def _append():
            time.sleep(delay)
            with open(target, "a") as handle:
                handle.write(text)

        writer = threading.Thread(target=_append)
        writer.start()
    proc = subprocess.run(command, capture_output=True, text=True, env=env,
                          timeout=timeout)
    if writer is not None:
        writer.join()
    return proc


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        stub = write_stub(work)

        # Reaching the prompt at all. The stub says nothing until it is sent
        # something, so this passes only if the driver knocks first.
        proc = run(stub, ["--stage", "prompt"])
        check("a silent machine is knocked into answering", proc.returncode, 0)

        check_in("the prompt is recognised", "at the MD prompt", proc.stderr)
        check_in("the sign-on reaches the console", "MD7C REV 8.00",
                 proc.stdout)

        # And the stage that must *not* knock. The stub answers only when it is
        # sent something, so a driver that knocked here would reach the prompt
        # and this would pass for the wrong reason -- the check is that nothing
        # comes back at all, which is the whole point of watching.
        #
        # Its own name, not `proc`: the first version of this reused it and
        # landed *above* the two checks that read the prompt run's output, so
        # they silently began inspecting this run instead and failed.
        watched = run(stub, ["--stage", "watch", "--settle", "0.2"])
        check("watching returns without knocking", watched.returncode, 0)
        # One character, and exactly one. The stub answers whatever it is sent,
        # so a prompt coming back proves the autobaud character went; what must
        # not happen is the repeated knocking the other stages do.
        check("watching still autobauds the port",
              "MD7C" in (watched.stdout or ""), True)
        check_in("and says it is watching", "watching, not knocking",
                 watched.stderr)

        # The whole invol stage, in order, read back from what the machine
        # received rather than from what the driver said it sent.
        record = work / "received"
        proc = run(stub, ["--stage", "invol"],
                   {"MDSTUB_RECORD": str(record)})
        check("the invol stage runs to its end", proc.returncode, 0)
        received = record.read_bytes().decode("latin-1")
        commands = [line for line in received.split("\r") if line]
        check("the commands arrive in the procedure's order",
              commands, ["re", "re", "di c", "ex invol"])

        # A machine that exits mid-script is reported as a machine that exited,
        # not waited out. The stub dies on the third carriage return, which is
        # inside the stage rather than before it.
        proc = run(stub, ["--stage", "invol", "--knock-timeout", "10",
                          "--timeout", "10"],
                   {"MDSTUB_MODE": "die"})
        check("a machine that exits fails the run", proc.returncode, 1)
        check_in("and says that it exited", "the machine exited", proc.stderr)

        # A machine that never answers ends in a message rather than a stall.
        proc = run(stub, ["--stage", "prompt", "--knock-timeout", "3"],
                   {"MDSTUB_MODE": "silent"})
        check("a machine that never prompts fails", proc.returncode, 1)
        check_in("and says it was knocking", "no prompt after",
                 proc.stderr)

        # The disk image is made when it is missing, at the size the install
        # procedure names -- recent MAME creates nothing and fails instead
        # (FINDINGS.md C47), so this is the driver's job and not a convenience.
        disk = work / "made" / "dn3500.awd"
        proc = run(stub, ["--stage", "prompt", "--disk", str(disk)])
        check("a missing disk image is created", disk.is_file(), True)
        check("at 348 MB", disk.stat().st_size, 348 * 1024 * 1024)

        # And not re-made over an existing one. An install is many stages long;
        # a driver that truncated the image on the second stage would destroy
        # exactly the work the first one did.
        disk.write_bytes(b"already installed")
        proc = run(stub, ["--stage", "prompt", "--disk", str(disk)])
        check("an existing image is left alone", disk.read_bytes(),
              b"already installed")

        # The cartridge is staged, so a guest that writes to the tape cannot
        # reach our copy of the medium.
        #
        # This is a regression test for a real loss, not a precaution. A
        # successful SR10.3 boot overwrote block 0 of
        # `019439-001.CRTG_PSKQ3_91_BOOT_1` -- the `SYSBOOT REV`/`0013D800`
        # descriptor the boot PROM validates -- and the cartridge then reported
        # `error: sysboot not found` for every run afterwards. Six
        # emulator-side hypotheses were eliminated first, because the media was
        # an input and inputs are not suspected.
        #
        # The stub writes to the path it is given, which is what
        # `sc499_device::write_block` does. So this fails on any driver that
        # hands MAME the source file, and it fails on the *content*, not on the
        # argument -- a check that only read the command line would pass a
        # driver that staged the copy and mounted the original anyway.
        cartridge = work / "pristine.ct"
        cartridge.write_bytes(b"SYSBOOT REV " + bytes(500))
        before = cartridge.read_bytes()
        ctapelog = work / "ctape-path"
        proc = run(stub, ["--stage", "prompt", "--ctape", str(cartridge)],
                   environment={"MDSTUB_MODE": "scribble",
                                "MDSTUB_CTAPELOG": str(ctapelog)})
        mounted, seen = ctapelog.read_bytes().split(b"\n", 1)
        mounted = Path(mounted.decode())
        check("the source cartridge is not what MAME is given",
              mounted == cartridge, False)
        check("and a guest that writes to the tape leaves it untouched",
              cartridge.read_bytes(), before)
        check("the staged copy carries the source's content",
              seen, b"SYSBOOT REV ")
        check("and it is the file written to",
              mounted.read_bytes()[:8], b"SCRIBBLE")

        # And every run re-stages, so a cartridge damaged by one run is not
        # inherited by the next. Without this a `--keep-rundir` install would
        # carry the first run's damage into every later stage, which is the
        # shape the original defect had.
        proc = run(stub, ["--stage", "prompt", "--ctape", str(cartridge),
                          "--keep-rundir"],
                   environment={"MDSTUB_MODE": "scribble",
                                "MDSTUB_CTAPELOG": str(ctapelog)})
        check("a kept run directory re-stages rather than inheriting damage",
              ctapelog.read_bytes().split(b"\n", 1)[1], b"SYSBOOT REV ")

        # The era reaches the driver script, and its default is C47's install
        # procedure rather than the host's own year. A flag because the two
        # callers want opposite answers: the install wants the 25-year shift,
        # and a volume this project built wants none of it -- Domain/OS refuses
        # a clock 24 years behind its own last-shutdown stamp (C53). An edit to
        # the Lua table would serve one caller and have to be remembered.
        eralog = work / "era"
        proc = run(stub, ["--stage", "prompt"],
                   environment={"MDSTUB_ENVLOG": str(eralog)})
        check("the era defaults to the install procedure's 25-year shift",
              eralog.read_text(), "25")
        proc = run(stub, ["--stage", "prompt", "--era", "none"],
                   environment={"MDSTUB_ENVLOG": str(eralog)})
        check("and --era none reaches the driver script", eralog.read_text(),
              "none")

        # The system config is planted *before* MAME starts, and `Node ID from
        # Disk` is off in it.
        #
        # `mdsession.lua` sets the same field and cannot set it in time: it runs
        # from a periodic callback, so `MACHINE_RESET` has already happened once
        # with MAME's defaults. For every other field that is only late, and the
        # Lua's soft reset repairs it. This one is not re-read at all --
        # `apollo.cpp:911` acts on it once, overwriting the node-ID device from
        # the label of the disk on unit 0 -- so a reset cannot put back what the
        # first one destroyed, and `-node_id` was silently ignored on every run
        # that had a labelled volume mounted. Measured: the node-ID ROM read
        # `01 23 45` (the disk's node) with the volume on unit 0 and `02 22 22`
        # (the image's) with the same volume on unit 1. `FINDINGS.md` C151.
        #
        # Checked against the *file*, because that is the whole mechanism: MAME
        # reads it at startup, and a cfg naming another machine, or carrying a
        # mask or default value that does not match the port, is ignored without
        # a word.
        # Off only when a node-ID image is named, and the file is written either
        # way: without one, taking the node from the disk is what lets a volume
        # from another node present the ID it records, and turning it off would
        # trade that for MAME's `DEFAULT_NODE_ID`.
        image = work / "node.ani"
        image.write_bytes(b"\0" * 32)
        planted = stub.parent / "run" / "cfg" / "dn3500.cfg"

        proc = run(stub, ["--stage", "prompt", "--node-id", str(image)])
        check("the system config is planted for the machine being run",
              planted.exists(), True)
        written = planted.read_text()
        check_in("naming that machine, since MAME matches on it",
                 '<system name="dn3500">', written)
        check_in("and a named node-ID image turns Node ID from Disk off",
                 '<port tag=":apollo_config" type="CONFIG" mask="256" '
                 'defvalue="256" value="0" />', written)
        check_in("the driver says which source the run got",
                 "node from the -node_id image", proc.stderr)

        proc = run(stub, ["--stage", "prompt"])
        check_in("and a run without one leaves MAME taking it from the disk",
                 '<port tag=":apollo_config" type="CONFIG" mask="256" '
                 'defvalue="256" value="256" />', planted.read_text())
        check_in("saying so", "node from the disk", proc.stderr)

        # A different machine gets its own, or MAME ignores the file entirely.
        proc = run(stub, ["--stage", "prompt", "--machine", "dn3000"])
        check("and a different machine gets a config of its own name",
              (stub.parent / "run" / "cfg" / "dn3000.cfg").exists(), True)

        # `!exit` asks for a clean shutdown and waits for it. The stub exits when
        # it sees the request file, standing in for `manager.machine:exit()` --
        # what is testable without MAME is that the driver writes the request
        # beside the swap channel and then waits for the process rather than
        # killing it, which is the whole difference from `!quit`.
        #
        # It matters because NVRAM -- the calendar's battery configuration table,
        # and so the node ID -- is written on the clean path only.
        commands = work / "exit.txt"
        commands.write_text("!exit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "20"],
                   {"MDSTUB_MODE": "exitwatch"})
        check("!exit asks for a clean shutdown", proc.returncode, 0)
        check_in("and says so", "clean shutdown requested", proc.stderr)
        check_in("and waits for the machine rather than killing it",
                 "the machine exited cleanly", proc.stderr)

        # And a stale request does not kill the *next* run. `--keep-rundir` is
        # precisely the case `!exit` exists for -- set a configuration in one run,
        # use it in the next -- and the request file left behind by the first made
        # the second exit at once, with nothing sent. The check is that a normal
        # stage still reaches the prompt in a run directory that already holds
        # one.
        stale = stub.parent / "run" / "swap.exit"
        stale.parent.mkdir(parents=True, exist_ok=True)
        stale.write_text("exit\n")
        proc = run(stub, ["--stage", "prompt", "--keep-rundir"])
        check("a stale exit request does not end the next run",
              proc.returncode, 0)
        check_in("which still reaches the prompt", "at the MD prompt",
                 proc.stderr)
        check("and the request is gone", stale.exists(), False)

        # A followed command file, written before the run: the directives parse
        # and arrive in order, and !quit ends the session rather than the
        # timeout doing it.
        record = work / "received-file"
        commands = work / "commands.txt"
        commands.write_text(
            "# a comment, which is not sent\n"
            "\n"
            "re\n"
            "!knock \\n\\n>\n"
            "di c\n"
            "!raw x\n"
            "!quit\n"
        )
        # `!knock` and not `!expect` after `re`, and the stub is what enforces
        # it: a reset machine is deaf, so an expectation here waits for output
        # that cannot come. The first version of this fixture used `!expect`
        # and passed only because the stub could not yet model a reset.
        # A blank line in the file is *not* an empty answer, and must not be:
        # a file being appended to a line at a time is full of momentarily
        # blank tails. `!cr` is how an empty answer is spelt.
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "10"],
                   {"MDSTUB_RECORD": str(record)})
        check("a followed command file runs to !quit", proc.returncode, 0)
        received = record.read_bytes().decode("latin-1")
        # The leading "\r" is the knock that reached the power-on prompt, and
        # the doubled one after "re" is the knock that brought the reset
        # machine back.
        check("its directives arrive in order and !raw adds no return",
              received, "\rre\r\rdi c\rx")

        # `!cr` sends a bare carriage return and `!raw` interprets escapes, so
        # an answer that is nothing at all can be spelt. INVOL ends its badspot
        # list with one, and the first attempt at it went out as a space --
        # which is a different byte and, on the knock path, a fatal one (C50).
        record = work / "received-cr"
        commands = work / "cr.txt"
        commands.write_text("!cr\n!raw a\\rb\n!quit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "10"],
                   {"MDSTUB_RECORD": str(record)})
        check("!cr and !raw escapes run", proc.returncode, 0)
        check("!cr is a bare return and !raw \\r is a real one",
              record.read_bytes().decode("latin-1"), "\r\ra\rb")

        # `!knock` is a directive rather than a stage's private trick, because
        # every stage that resets needs it: after `re` the machine is deaf and
        # an expectation waits for output it cannot produce.
        record = work / "received-knock"
        commands = work / "knock.txt"
        commands.write_text("re\n!knock MD7C\n!quit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "10", "--knock-timeout", "20"],
                   {"MDSTUB_RECORD": str(record)})
        check("!knock reaches a machine that has gone quiet",
              proc.returncode, 0)

        # And the character it knocks with is settable, which is what let the
        # carriage-return claim in C50 be measured instead of asserted.
        record = work / "received-char"
        commands = work / "char.txt"
        commands.write_text("!quit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--knock-char", "\r", "--timeout", "10"],
                   {"MDSTUB_RECORD": str(record)})
        check("the knock character is what reaches the machine",
              record.read_bytes().decode("latin-1"), "\r")

        # `!swap` changes a medium without stopping the machine. MINST takes
        # four cartridges in turn, so an install that cannot change one is an
        # install that stops after the first.
        swaplog = work / "swaps"
        commands = work / "swap.txt"
        # The tapes exist, because a swapped cartridge is staged the same way
        # `--ctape` is and a copy needs something to copy.
        (work / "tape1.ct").write_bytes(b"tape one")
        (work / "tape2.ct").write_bytes(b"tape two")
        commands.write_text(
            "!swap ctape %s\n" % (work / "tape1.ct")
            + "!swap ctape %s\n" % (work / "tape2.ct")
            + "!quit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "20"],
                   {"MDSTUB_SWAPLOG": str(swaplog)})
        check("two swaps run in order", proc.returncode, 0)
        # Absolute paths under the run directory, not the sources: the driver
        # resolves deliberately, because MAME runs from its own directory, and
        # it stages, so that MINST's four cartridges are four copies rather
        # than four originals held open read/write.
        #
        # Named `resolve()`d on both sides rather than compared as written.
        # macOS makes /var a symlink to /private/var, so resolving is not a
        # no-op there even for a path that is already absolute -- asserting the
        # unresolved form tests the platform, not us.
        staged_dir = (work / "run" / "media").resolve()
        check("and both reach the machine, staged, as absolute paths",
              swaplog.read_text().split(),
              ["ctape", str(staged_dir / "tape1.ct"),
               "ctape", str(staged_dir / "tape2.ct")])
        check("with the source's content",
              [(staged_dir / n).read_bytes() for n in ("tape1.ct", "tape2.ct")],
              [b"tape one", b"tape two"])

        # A cartridge that is not there is passed through rather than copied,
        # so the error stays MAME's "load failed" and does not become a
        # traceback out of the stager.
        commands = work / "missing.txt"
        commands.write_text("!swap ctape %s\n!quit\n" % (work / "absent.ct"))
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "20"])
        check("a missing cartridge is reported, not staged", proc.returncode, 0)
        check_in("and named", "absent.ct does not exist", proc.stderr)

        # A swap must not change which file the driver reads its commands
        # from. `!swap` used to assign the resolved medium to a local called
        # `path` -- the same name as follow_commands' parameter holding the
        # command file -- so from the next poll the driver read the *cartridge
        # image* as commands and typed it at the machine: 308,250 sends and 150
        # Mbyte of log in the real run, with the real command file silently
        # never read again.
        #
        # The fixture is the shape that catches it: swap in a file with
        # recognisable content, then append a further command. If the command
        # file is still the one being followed, the command arrives; if the
        # driver has been redirected onto the medium, it never does.
        record = work / "received-afterswap"
        medium = work / "cartridge.ct"
        medium.write_text("NOTACOMMAND\n" * 50)
        commands = work / "afterswap.txt"
        commands.write_text("!swap ctape %s\n" % medium)
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--commands-timeout", "25", "--timeout", "20"],
                   {"MDSTUB_RECORD": str(record),
                    "MDSTUB_SWAPLOG": str(work / "swaps-afterswap")},
                   append_after=(commands, "afterswap\n!quit\n", 4.0))
        got = record.read_bytes().decode("latin-1")
        check("a command appended after a swap still arrives",
              "afterswap" in got, True)
        check("and the medium's contents are never sent as commands",
              "NOTACOMMAND" in got, False)

        # A refused swap fails the run rather than being swallowed. A tape that
        # did not mount looks exactly like a tape that mounted and holds nothing
        # the installer wants, and the second is a much harder thing to debug.
        commands = work / "swapfail.txt"
        commands.write_text("!swap ctape %s\n!quit\n" % (work / "tape1.ct"))
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "20"],
                   {"MDSTUB_SWAPFAIL": "1"})
        check("a refused swap fails the run", proc.returncode, 1)
        check_in("and says why", "load failed", proc.stderr)

        # And followed *while running*, which is the property the whole
        # mechanism exists for: a stage learnt from the machine's own output
        # must be answerable without restarting the machine, because reaching
        # some of these prompts costs ten minutes of emulated tape.
        record = work / "received-live"
        commands = work / "live.txt"
        commands.write_text("")

        import threading

        def append_later():
            import time as _time
            _time.sleep(3.0)
            with open(commands, "a") as handle:
                handle.write("late\n!quit\n")

        writer = threading.Thread(target=append_later)
        writer.start()
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--commands-timeout", "30", "--timeout", "10"],
                   {"MDSTUB_RECORD": str(record)})
        writer.join()
        check("a line appended after the run started is still sent",
              proc.returncode, 0)
        check_in("and reaches the machine", "late",
                 record.read_bytes().decode("latin-1"))

        # A prompt that was already waiting must not satisfy the *next*
        # expectation. This is the bug that cost two commands in one real
        # session: unconsumed output left by an earlier step made a later
        # `!knock` return instantly, so the command after it was typed into a
        # machine that had just reset and was deaf.
        #
        # The fixture reproduces the shape exactly. `noop` is sent with nothing
        # waiting on its answer, so its prompt is left unread -- which is what
        # `shut` did. Then `re`, which makes the machine deaf. Then a knock that
        # the leftover prompt could satisfy. If it does, the knock returns
        # without waking anything and `last` is typed into the deaf window and
        # swallowed, exactly as `ex domain_os` was.
        record = work / "received-stale"
        commands = work / "stale.txt"
        # The `!wait` is the real shape and not padding: in the session that
        # found this, `shut`'s prompt had been sitting unread for seconds before
        # `re` was sent. Without the wait the fixture races instead -- the sends
        # outrun the machine, and `noop`'s output arrives *after* the cursor
        # moved, which no cursor discipline can help. That is a different bug
        # (a script that does not wait for what it asked for) and the answer to
        # it is `!expect`, not this.
        commands.write_text("noop\n!wait 2\nre\n!knock \\n\\n>\nlast\n!quit\n")
        proc = run(stub, ["--stage", "prompt", "--commands", str(commands),
                          "--timeout", "20", "--knock-timeout", "20"],
                   {"MDSTUB_RECORD": str(record)})
        check("a stale prompt does not satisfy the next wait",
              proc.returncode, 0)
        check_in("so the command after it still arrives", "last",
                 record.read_bytes().decode("latin-1"))

        # A killed driver takes its emulator with it. `close()` covers every
        # path the driver controls and none of the ones that matter -- a driver
        # killed from outside skips it, and the orphan then holds the log file
        # open so the next run's writes interleave with NUL padding. Both were
        # observed.
        commands = work / "orphan.txt"
        commands.write_text("!wait 60\n!quit\n")
        proc = subprocess.Popen(
            [sys.executable, str(DRIVER), "--mame", str(stub),
             "--rundir", str(stub.parent / "run"),
             "--stage", "prompt", "--commands", str(commands)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # Long enough for the stub to be running and past the knock.
        deadline = time.time() + 20
        child = None
        while time.time() < deadline and child is None:
            for pid in _children_of(proc.pid):
                child = pid
            time.sleep(0.2)
        # SIGTERM, not SIGKILL. This is what a timeout, a `pkill` or a
        # terminal going away actually sends, it is handleable on every
        # platform, and so it is the path worth guaranteeing. SIGKILL is
        # covered by PR_SET_PDEATHSIG where the kernel offers it, which is
        # Linux only and therefore not something a portable suite can assert.
        proc.terminate()
        proc.wait(timeout=15)
        gone = False
        deadline = time.time() + 15
        while time.time() < deadline:
            if child is None or not _alive(child):
                gone = True
                break
            time.sleep(0.2)
        check("a killed driver does not leave the machine running", gone, True)

        # Relative image paths are resolved against the caller's directory, not
        # MAME's. The driver runs the emulator from its own directory, so an
        # unresolved path is created here and looked for there.
        relative = Path("relative-disk.awd")
        cwd = os.getcwd()
        os.chdir(work)
        try:
            proc = run(stub, ["--stage", "prompt", "--disk", str(relative)])
            check_in("a relative disk path is resolved for MAME",
                     str(work / relative), proc.stderr)
        finally:
            os.chdir(cwd)

    if failures:
        sys.stderr.write("\n%d check(s) failed\n" % failures)
        return 1
    sys.stdout.write("\nall checks passed\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
