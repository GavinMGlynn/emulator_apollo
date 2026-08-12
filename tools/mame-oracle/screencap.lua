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

-- `APOLLO_SNAP_KEYS`: `PORT_NAME@seconds`, comma separated, e.g.
-- `Y@45,Return@46`. Without this the oracle cannot get past SELF_TEST's
-- "DO YOU WISH TO CONTINUE (Y,N)?", which is not a fault but a setup step, and
-- every capture stops there showing a machine that looks stuck.
--
-- Pressed by `PORT_NAME` directly rather than through MAME's natural keyboard,
-- for the reason `mdsession.lua` records: `apollo_kbd.cpp` defines no
-- `PORT_CHAR` entries, so the translation layer has nothing to work with and
-- silently does nothing. A press is held for `APOLLO_SNAP_KEY_HOLD` seconds
-- (default 0.4) and then released -- a key that is never released repeats.
local function parse_keys(spec)
	local out = {}
	for item in spec:gmatch("[^,]+") do
		local name, when = item:match("^%s*(.-)%s*@%s*([%d%.]+)%s*$")
		if name ~= nil then
			out[#out + 1] = { name = name, at = tonumber(when), done = false }
		end
	end
	table.sort(out, function(a, b) return a.at < b.at end)
	return out
end

local keys     = parse_keys(os.getenv("APOLLO_SNAP_KEYS") or "")
local key_hold = tonumber(os.getenv("APOLLO_SNAP_KEY_HOLD") or "") or 0.4

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

local held = nil
local held_until = 0

local function press_key(name)
	for tag, port in pairs(manager.machine.ioport.ports) do
		if tag:find("kbd") then
			for field_name, field in pairs(port.fields) do
				if field_name == name then
					held = field
					field:set_value(1)
					out("# pressed %q on %s at %.3fs\n", name, tag, now_s())
					return
				end
			end
		end
	end
	-- Naming what is present, because "not found" cannot distinguish a
	-- misspelled `PORT_NAME` from a machine with no keyboard fitted.
	out("# key %q not found; keyboard fields present:\n", name)
	for tag, port in pairs(manager.machine.ioport.ports) do
		if tag:find("kbd") then
			for field_name, _ in pairs(port.fields) do
				out("#   %s %s\n", tag, field_name)
			end
		end
	end
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
	if held ~= nil and now >= held_until then
		held:set_value(0)
		out("# released at %.3fs\n", now)
		held = nil
	end
	if held == nil then
		for _, k in ipairs(keys) do
			if not k.done and now >= k.at then
				k.done = true
				press_key(k.name)
				held_until = now + key_hold
				break
			end
		end
	end
	while S.next <= #at and now >= at[S.next] do
		snap(at[S.next])
		S.next = S.next + 1
	end
	if now >= until_s then
		out("# end at %.6fs\n", now)
		manager.machine:exit()
	end
end)
