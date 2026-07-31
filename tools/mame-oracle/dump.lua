-- Dump oracle machine state at a fixed point in emulated time.
--
-- Loaded by MAME as -autoboot_script. Everything it prints goes to stdout in
-- the format tools/mame-oracle/oracle.py expects and diffs.
--
-- The contract this file has to keep is *determinism*: two runs of the same
-- workload must produce byte-identical output, or the dump is worthless as an
-- oracle reading. So nothing here may observe the host. No wall-clock, no
-- os.time, no os.date, no iteration order that depends on a hash table's
-- internal layout -- every table walk below is sorted before it is printed,
-- because Lua's pairs() order is unspecified and would otherwise vary run to
-- run and build to build.
--
-- Configuration arrives through environment variables rather than script
-- arguments, because -autoboot_script takes a path and nothing else:
--
--   APOLLO_DUMP_AT      emulated seconds at which to dump (default 1.0)
--   APOLLO_DUMP_CPU     device tag to dump registers from (default ":maincpu")
--   APOLLO_DUMP_MEM     ranges, "space:start+length[,space:start+length...]"
--                       e.g. "program:0x0+0x100,program:0x1000000+0x40"
--   APOLLO_DUMP_EXIT    "1" to exit once the dump is written (default "1")

local dump_at  = tonumber(os.getenv("APOLLO_DUMP_AT") or "") or 1.0
local cpu_tag  = os.getenv("APOLLO_DUMP_CPU") or ":maincpu"
local mem_spec = os.getenv("APOLLO_DUMP_MEM") or ""
local do_exit  = (os.getenv("APOLLO_DUMP_EXIT") or "1") ~= "0"

local done = false

local function out(fmt, ...)
	io.write(string.format(fmt, ...))
end

-- "program:0x0+0x100,program:0x1000000+0x40" -> list of {space, start, length}
local function parse_ranges(spec)
	local ranges = {}
	for item in spec:gmatch("[^,]+") do
		local space, start, length = item:match("^%s*([%w_]+):([%w%x]+)%+([%w%x]+)%s*$")
		if space then
			ranges[#ranges + 1] = {
				space = space,
				start = tonumber(start) or 0,
				length = tonumber(length) or 0,
			}
		else
			out("# ERROR unparsable range %q\n", item)
		end
	end
	return ranges
end

local function dump_registers(dev)
	local names = {}
	for name in pairs(dev.state) do
		names[#names + 1] = name
	end
	-- Sorted by symbol. MAME's device_state_entries is a sequence in the
	-- device's declaration order, so iteration is already reproducible within
	-- one build -- but that order is a property of the driver, not of the
	-- hardware, and it moves when the ext/mame pin moves. Sorting means a
	-- dump taken against a newer oracle still diffs cleanly against an older
	-- one, and only genuine value changes show up.
	table.sort(names)

	out("[cpu %s]\n", cpu_tag)
	for _, name in ipairs(names) do
		local entry = dev.state[name]
		-- Width from the device's own declared datasize, so a register prints
		-- the same number of digits every run rather than as many as its
		-- current value happens to need.
		local digits = math.max(2, (entry.datasize or 4) * 2)
		if entry.is_float then
			-- An FPU register's value is a Lua float, and the integer format
			-- below would raise "number has no integer representation" on it.
			-- %.17g is the shortest form that round-trips a double exactly, so
			-- the dump stays lossless and still compares as text.
			out("%-8s %.17g\n", name, entry.value)
		else
			out("%-8s %0" .. digits .. "X\n", name, entry.value & 0xFFFFFFFFFFFFFFFF)
		end
	end
end

local function dump_memory(dev, range)
	local space = dev.spaces[range.space]
	if not space then
		out("# ERROR no space %q on %s\n", range.space, cpu_tag)
		return
	end

	out("[mem %s/%s %08X+%X]\n", cpu_tag, range.space, range.start, range.length)
	local line = {}
	local text = {}
	for offset = 0, range.length - 1 do
		local addr = range.start + offset
		-- read_u8 rather than a wider read: this must not depend on the
		-- machine's endianness or on how a wide read straddles a bank edge.
		local byte = space:read_u8(addr) & 0xFF
		line[#line + 1] = string.format("%02X", byte)
		text[#text + 1] = (byte >= 0x20 and byte < 0x7F)
			and string.char(byte) or "."
		if #line == 16 or offset == range.length - 1 then
			out("%08X  %-47s  |%s|\n",
				addr - #line + 1, table.concat(line, " "), table.concat(text))
			line = {}
			text = {}
		end
	end
end

local function dump()
	if done then return end
	done = true

	local machine = manager.machine
	local dev = machine.devices[cpu_tag]

	out("# apollo oracle dump\n")
	out("machine %s\n", emu.romname())
	-- The requested time, not machine.time: the actual time is whatever frame
	-- boundary we happened to land on, which is a host-independent number but a
	-- needlessly noisy one to diff. The scheduled point is what was asked for.
	out("at %.6f\n", dump_at)

	if not dev then
		out("# ERROR no device %q\n", cpu_tag)
	else
		dump_registers(dev)
		for _, range in ipairs(parse_ranges(mem_spec)) do
			dump_memory(dev, range)
		end
	end

	out("# end\n")
	io.flush()

	if do_exit then
		machine:exit()
	end
end

-- Periodic granularity rather than a cycle-exact hook: this samples the oracle,
-- and the sampling point only has to be *reproducible*, not exact. A figure
-- that needs cycle precision is measured by a probe, not by this dumper.
--
-- register_periodic, *not* add_machine_frame_notifier. Frame notifications stop
-- arriving on dn3500 at 3.246948 emulated seconds -- measured: frame 195 is the
-- last, while the machine itself runs on past 19s quite happily. A frame-driven
-- dump therefore silently produced nothing for any point beyond about 3.2s, and
-- MAME still exited 0, so it read as success to anything not counting dump
-- blocks. register_periodic keeps firing for the whole run (verified to 8s),
-- which is what makes a dump point past the video setup reachable at all.
emu.register_periodic(function ()
	if not done and manager.machine.time:as_double() >= dump_at then
		dump()
	end
end)

-- A workload shorter than dump_at (or one that exits early) must still produce
-- a dump rather than silently nothing, so the run is never mistaken for a pass.
emu.add_machine_stop_notifier(function ()
	if not done then
		out("# WARNING machine stopped before %.6f\n", dump_at)
		do_exit = false
		dump()
	end
end)
