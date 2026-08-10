#!/bin/sh
# Reach the Mnemonic Debugger, and drive it.
#
# This exists as a script for the same reason `identity-boot.sh` does: the
# dialogue is only reproducible *together with the invocation that produced it*,
# and the previous recipe was recorded as prose -- service mode, a key press, an
# interval, forty returns -- which five reconstructions failed to rebuild. The
# prose is commentary; this file is the artifact.
#
# ## The one byte that matters
#
# The PROM's service-mode entry does **not** wait for a carriage return. It
# autobauds, and the way it autobauds is to look at the *bit pattern* a byte
# makes at whatever rate its receiver currently holds, then reprogram the rate
# to match. Disassembled from `3500_BOOT_12191_7.bin` at `000844`-`0008B8`:
#
#     0007EC  move.b  $16(a0),d1      the receive buffer
#     000844  cmp.b   #$FF,d1   ->    move.b #$BB,$12(a0,d4.w)   9600
#     00085A  cmp.b   #$FE,d1   ->    move.b #$99,$12(a0,d4.w)   4800
#     000870  cmp.b   #$C7,d1   ->    move.b #$88,$12(a0,d4.w)
#     000886  cmp.b   #$72,d1   ->    move.b #$66,$159(a6)       1200
#     0008A0  cmp.b   #$C0,d1   ->    move.b #$44,$159(a6)
#     0008A4  bne.w   $78E            anything else: back to the poll
#
# `0D` is not in that set, so a carriage return -- at *any* rate -- is ignored
# and the firmware polls for ever. That is what every earlier attempt sent, and
# passing `--boot-input-rate 0x99` made it worse rather than better: it delivered
# the character *correctly*, and the firmware only recognises the garbled
# patterns. Sending `FF` outright is the same thing the hardware would see from a
# console running faster than the receiver, and it selects 9600 -- which is what
# this frontend transmits at by default, so everything after it arrives clean.
#
# ## Usage
#
#   tools/md-session.sh [extra apollo-headless arguments...]
#
# With no arguments it reaches the `>` prompt and sits there. To drive it, pass
# a script:
#
#   tools/md-session.sh --boot-script my.script --boot-limit 1500000000
#
# where `my.script` is `expect MD7C` followed by `send EX DOMAIN_OS\r`.
#
# Service mode is the Normal/Service switch, not a debugger flag: it is what
# makes the PROM offer MD instead of booting. No disk is mounted here -- add
# `--disk` when the command needs one, as `EX` does.
#
# ## Driving MD from the *keyboard* instead
#
# Which console the operating system picks decides which interrupts it unmasks,
# so a keyboard session is not interchangeable with this serial one. `--boot-
# type` types on the keyboard, but a dialogue handed to it at power-on is
# delivered inside the first tenth of an emulated second -- long before the PROM
# reaches its console-selection poll -- and every character but the last is
# gone. Hold it until the firmware is listening:
#
#   tools/md-session.sh --disk <copy> --screen 19i --boot-input "" \
#     --boot-type      "$(printf '\r\r')"           --boot-type-after-pc      0x78E \
#     --boot-type-then "$(printf 'EX DOMAIN_OS\r')" --boot-type-then-after-pc 0x930 \
#     --screenshot out.png
#
# **Two phases, and the split is not arbitrary.** `0x78E` is the top of the
# console-selection poll; two characters are spent before MD exists -- one
# selects the console (`00080E` posts `09`, the keyboard branch) and one
# carriage return gets past `0008C8` to the banner (`0F`). `0x930` is the
# instruction after that banner, and the command must wait for it because **the
# firmware reads and discards whatever is typed while it prints**: a run with
# everything in one string reported 15 characters typed, `sio1 reg 3` read 15
# times, `rx_flushed` never moved -- and MD received `AIN_OS` out of
# `EX DOMAIN_OS`. Nothing was lost on the wire; six characters were read by the
# PROM and dropped, which is ordinary type-ahead handling and not a defect.
#
# **`--screenshot` is not optional.** This console is the *display*, so
# `--boot-console` prints nothing at all: a run without a screenshot is silent
# by construction rather than for want of output, and the screen is how
# `AIN_OS` was found.
#
# Use `printf` for the returns: a shell here-string sends `0A`, and no key
# produces `0A`.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bin=$root/build/linux-release/src/frontend/headless/apollo-headless
[ -x "$bin" ] || bin=$root/build/linux-debug/src/frontend/headless/apollo-headless

# FF selects 9600, then carriage returns for the prompt. The interval is 0.4 s
# of *emulated* time: the probe runs late, and a burst delivered at once is
# consumed long before it.
input=$(printf '\377\015\015\015\015\015\015\015\015\015\015\015\015\015\015\015\015\015\015\015\015')

exec "$bin" \
  --boot-prom "$root/roms/firmware/3500_BOOT_12191_7.bin" \
  --boot-console \
  --service-mode \
  --boot-input "$input" \
  --boot-input-port 1 \
  --boot-input-channel B \
  --boot-input-interval 400000 \
  --boot-limit 500000000 \
  "$@"
