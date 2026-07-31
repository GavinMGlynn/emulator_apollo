-- Log every write the machine makes to a chosen address range.
--
-- Some questions about this board cannot be answered by reading a register,
-- because the answer was never stored anywhere readable. The 8259A is the clear
-- case: its initialization command words configure priority, triggering and
-- vectoring, and *none of them can be read back*. The only way to learn how the
-- firmware programmed the part is to watch it do so.
--
-- That makes this the firmware-behaviour evidence route, which this project
-- already accepts elsewhere -- `CLAUDE.md` admits "a ring-firmware disassembly
-- address" as a citable source. Watching the PROM write is the same class of
-- evidence, arrived at by running rather than reading, and it produces a
-- transcript anyone with this checkout can reproduce.
--
-- Loaded by MAME as -autoboot_script, and it needs `-debug -debugger none` for
-- the same reason the other scripts here do (FINDINGS.md C5).
--
-- Determinism, as in dump.lua and steptime.lua: nothing observes the host. The
-- ranges are walked in the order given.
--
-- ## Configuration
--
--   APOLLO_TRACE_RANGES  "name@first-last,..." in hexadecimal. Required; there
--                        is no default, because a trace of the wrong range that
--                        printed nothing would look exactly like a device that
--                        was never programmed.
--   APOLLO_TRACE_UNTIL   emulated seconds to run before stopping (default 3.0)
--   APOLLO_TRACE_LIMIT   maximum lines to emit (default 400). A runaway range
--                        would otherwise produce a file no one reads.
--
-- ## Why the tap goes in at the first periodic and not later
--
-- The boot PROM programs these devices early. A tap installed at two seconds --
-- the point the other probes here sample at -- would miss the whole
-- initialization and report an idle device. The first periodic callback is the
-- earliest point a script can act, and the transcript's first line records the
-- time it started so a reader can see how much was missed.

local ranges_spec = os.getenv("APOLLO_TRACE_RANGES") or ""
local until_s     = tonumber(os.getenv("APOLLO_TRACE_UNTIL") or "") or 3.0
local limit       = tonumber(os.getenv("APOLLO_TRACE_LIMIT") or "") or 400

local installed = false
local finished = false
local emitted = 0
-- The tap objects have to outlive the call that made them; MAME removes a tap
-- when its handle is collected.
local taps = {}

local function out(fmt, ...)
	io.write(string.format(fmt, ...))
end

local function now_attos()
	local t = manager.machine.time
	return t.seconds * 1000000000000000000 + t.attoseconds
end

local function parse_ranges(spec)
	local ranges = {}
	for item in spec:gmatch("[^,]+") do
		local name, first, last = item:match("^%s*([%w_]+)@(%x+)%-(%x+)%s*$")
		if name == nil then
			out("# ERROR unparsable range %q\n", item)
			return nil
		end
		ranges[#ranges + 1] = {
			name = name,
			first = tonumber(first, 16),
			last = tonumber(last, 16),
		}
	end
	if #ranges == 0 then
		out("# ERROR no ranges given; set APOLLO_TRACE_RANGES\n")
		return nil
	end
	return ranges
end

emu.register_periodic(function()
	if finished then
		return
	end

	if not installed then
		installed = true

		local ranges = parse_ranges(ranges_spec)
		if ranges == nil then
			finished = true
			manager.machine:exit()
			return
		end

		local space = manager.machine.devices[":maincpu"].spaces["program"]

		out("# apollo write trace\n")
		out("# tap installed at %d attoseconds\n", now_attos())
		out("# columns: time_attos range offset data mask\n")

		for i = 1, #ranges do
			local r = ranges[i]
			out("# range %s %06X-%06X\n", r.name, r.first, r.last)
			-- Captured by value into the closure, so each tap reports its own
			-- name rather than whichever loop variable survived.
			local name = r.name
			local base = r.first
			taps[#taps + 1] = space:install_write_tap(
				r.first, r.last, r.name,
				function(offset, data, mask)
					if emitted < limit then
						emitted = emitted + 1
						out("%d %s %06X %04X %04X\n",
						    now_attos(), name, base + (offset - base), data, mask)
					elseif emitted == limit then
						emitted = emitted + 1
						out("# LIMIT reached at %d lines; trace truncated\n", limit)
					end
					-- Returning the data unchanged leaves the machine's own
					-- behaviour alone. A tap that altered it would be an
					-- instrument that changed what it measured.
					return data
				end)
		end
		return
	end

	if manager.machine.time.seconds >= until_s then
		out("# end after %d writes\n", emitted)
		finished = true
		manager.machine:exit()
	end
end)
