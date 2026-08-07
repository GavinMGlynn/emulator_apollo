-- Capture the oracle's *screen* at chosen instants, in a chosen boot mode.
--
-- Every oracle reading before this one was a register dump or a serial tap, and
-- on a DN3500 neither can see what the boot PROM says: the machine has a
-- display, so that is where it talks (FINDINGS.md C34). Our own core's
-- `--screenshot` has shown firmware output since Phase 5 began; this is the
-- other half, and it makes "the screen matches the oracle" a thing that can be
-- checked rather than assumed. `pngcmp.py` does the comparing.
--
-- Loaded as -autoboot_script. Only one autoboot script is honoured, so this
-- does not compose with dump.lua or writetrace.lua.
--
-- Determinism: nothing observes the host, and no input is posted -- a machine
-- with a display needs none, and every character sent to one interrupts it.
--
-- ## Three harness traps, each of which returns a plausible-looking nothing
--
-- 1. `machine.time.seconds` is the attotime's **integer** seconds field, not
--    the instant. A script comparing it against 0.4 waits until second 1, and
--    one comparing against 0.05, 0.2 and 0.4 fires all three at that same
--    instant -- which is what the first version of this file did, producing
--    three byte-identical PNGs that read as a screen which never changes.
--    `as_double()` is the instant. The same trap sits in `mdcapture.lua`.
--
-- 2. The autoboot script does not run until the first timeslice after reset --
--    about 17 ms of emulated time, or something like 100,000 instructions of
--    boot PROM. Anything that must be in place before the firmware runs is
--    therefore too late by default.
--
-- 3. `:apollo_config` is read at `MACHINE_RESET`, so setting Normal/Service
--    from the first periodic callback changes nothing at all: the machine has
--    already been reset with the old value. `mdcapture.lua` sets it there and
--    its "Normal mode" control run was consequently not a control.
--
-- (2) and (3) have one fix: set the configuration, then `soft_reset()` so the
-- firmware runs again with it. The reset re-runs this script with fresh locals,
-- so the guard lives in `_G` or the machine resets for ever.
--
-- ## Configuration
--
--   APOLLO_SNAP_AT    comma-separated emulated seconds to snapshot at,
--                     counted from the reset this script performs
--                     (default "1,3,6")
--   APOLLO_SNAP_UNTIL emulated seconds to run before exiting (default: the
--                     last snapshot instant)
--   APOLLO_SNAP_MODE  "normal" (default) or "service". MAME's own labels are
--                     `PORT_CONFSETTING(0x00, "Service")` and
--                     `(0x01, "Normal")`, with 0x00 the default -- so the
--                     oracle *ships* in Service, and a run that leaves the port
--                     alone is a service-mode run whatever it meant to be.
--                     The two modes are different machines: in Service the PROM
--                     drops straight to the `>` prompt without drawing, and in
--                     Normal it runs its self-tests on the screen.

local function parse_times(s)
	local out = {}
	for piece in s:gmatch("[^,]+") do
		local v = tonumber(piece)
		if v ~= nil then out[#out + 1] = v end
	end
	table.sort(out)
	return out
end

local at      = parse_times(os.getenv("APOLLO_SNAP_AT") or "1,3,6")
local normal  = (os.getenv("APOLLO_SNAP_MODE") or "normal") ~= "service"
local until_s = tonumber(os.getenv("APOLLO_SNAP_UNTIL") or "") or (at[#at] or 6.0)

local G = _G
G.APOLLO_SNAP = G.APOLLO_SNAP or { armed = false, base = 0, next = 1 }
local S = G.APOLLO_SNAP

local function out(fmt, ...)
	io.write(string.format(fmt, ...))
	io.flush()
end

local function now_s() return manager.machine.time:as_double() - S.base end

local function set_mode()
	local port = manager.machine.ioport.ports[":apollo_config"]
	if port == nil then
		-- Naming what is present, because "not found" alone cannot distinguish
		-- a wrong tag from a machine that has no such port.
		out("# no :apollo_config port. ports present:\n")
		for tag, _ in pairs(manager.machine.ioport.ports) do out("#   %s\n", tag) end
		return
	end
	for name, field in pairs(port.fields) do
		if name:find("Service") then
			field.user_value = normal and 1 or 0
			out("# %s = %s\n", name, normal and "Normal" or "Service")
			return
		end
	end
	out("# no Service field found in :apollo_config\n")
end

-- Named by the instant rather than by a counter, so a file cannot be matched to
-- the wrong moment when several runs land in one directory.
local function snap(when)
	for tag, screen in pairs(manager.machine.screens) do
		local safe = tag:gsub("[^%w]", "_")
		local name = string.format("apollo%s_%s_%04dms.png", safe,
		                           normal and "normal" or "service",
		                           math.floor(when * 1000 + 0.5))
		screen:snapshot(name)
		out("# snapshot %s at %.3fs (real %.6fs)\n", name, when, now_s())
	end
end

emu.register_periodic(function()
	if not S.armed then
		S.armed = true
		for tag, screen in pairs(manager.machine.screens) do
			out("# screen %s %dx%d\n", tag, screen.width, screen.height)
		end
		set_mode()
		S.base = manager.machine.time:as_double()
		out("# apollo screen capture, mode=%s, until=%.1fs\n",
		    normal and "normal" or "service", until_s)
		manager.machine:soft_reset()
		return
	end
	local now = now_s()
	while S.next <= #at and now >= at[S.next] do
		snap(at[S.next])
		S.next = S.next + 1
	end
	if now >= until_s then
		out("# end at %.6fs\n", now)
		manager.machine:exit()
	end
end)
