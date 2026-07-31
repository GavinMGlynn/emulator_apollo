-- Test dump.lua against a mock machine.
--
-- dump.lua only ever touches the oracle through a handful of MAME Lua calls,
-- so those can be stubbed and the dumper exercised with no MAME at all. That
-- matters twice over: the format is pinned by a test that runs in CI, where
-- the oracle is never built, and a format regression is caught in a second
-- rather than after a multi-minute emulator run.
--
-- What this does NOT test is whether the values are right -- that is the
-- oracle's job, and is what FINDINGS.md campaigns are for. This tests that
-- what the dumper is handed comes out in the format oracle.py expects, byte
-- for byte.
--
--   lua tools/mame-oracle/test_dump.lua

local HERE = arg[0]:match("^(.*)/[^/]*$") or "."

local failures = 0

local function check(name, actual, expected)
	if actual ~= expected then
		failures = failures + 1
		io.stderr:write(string.format(
			"FAIL %s\n  expected: %s\n  actual:   %s\n",
			name, tostring(expected), tostring(actual)))
	else
		io.write(string.format("ok   %s\n", name))
	end
end

local function check_match(name, actual, pattern)
	if not tostring(actual):match(pattern) then
		failures = failures + 1
		io.stderr:write(string.format(
			"FAIL %s\n  no match for: %s\n  in: %s\n", name, pattern, tostring(actual)))
	else
		io.write(string.format("ok   %s\n", name))
	end
end

-- ---------------------------------------------------------------- the mocks

-- 36 bytes: two full 16-byte lines and a 4-byte tail, so the flush-on-partial
-- path is exercised rather than only the exact-multiple case.
local MEM_BASE, MEM_LEN = 0x1000, 0x24

local memory = {}
for offset = 0, MEM_LEN - 1 do
	-- A pattern with a printable run in it, so the |ascii| column is tested
	-- for both halves of its conditional rather than only the dots.
	memory[MEM_BASE + offset] = (offset >= 16 and offset < 24)
		and (0x41 + offset - 16) or (offset & 0xFF)
end

local exited = false

local fake_space = {
	read_u8 = function (_self, addr)
		return memory[addr] or 0
	end,
}

local fake_device = {
	state = {
		-- Deliberately NOT in alphabetical order: the dumper must sort, and a
		-- mock that is already sorted would let a missing sort pass.
		PC = { value = 0x00010000, datasize = 4, is_float = false },
		D0 = { value = 0xDEADBEEF, datasize = 4, is_float = false },
		SR = { value = 0x2700,     datasize = 2, is_float = false },
		FP0 = { value = 0.5,       datasize = 8, is_float = true },
	},
	spaces = { program = fake_space },
}

local machine_time = 0.0

-- dump.lua triggers on register_periodic rather than on frame notifications:
-- frames stop arriving partway through a real run (see FINDINGS.md), which
-- silently produced no dump at all for any later sampling point.
local periodic_notifier, stop_notifier

_G.emu = {
	romname = function () return "dn3500" end,
	register_periodic = function (fn) periodic_notifier = fn end,
	add_machine_stop_notifier = function (fn) stop_notifier = fn end,
}

_G.manager = {
	machine = {
		devices = { [":maincpu"] = fake_device },
		time = { as_double = function () return machine_time end },
		exit = function () exited = true end,
	},
}

-- dump.lua reads its configuration at load time, and Lua cannot set the
-- environment of its own process, so os.getenv is what gets stubbed.
local fake_env = {
	APOLLO_DUMP_AT = "1.000000",
	APOLLO_DUMP_CPU = ":maincpu",
	APOLLO_DUMP_MEM = string.format("program:0x%X+0x%X", MEM_BASE, MEM_LEN),
	APOLLO_DUMP_EXIT = "1",
}
local real_getenv = os.getenv
os.getenv = function (name) return fake_env[name] or real_getenv(name) end

-- Capture what the dumper writes instead of letting it reach stdout.
local captured = {}
local real_write = io.write
io.write = function (...)
	for _, piece in ipairs({ ... }) do captured[#captured + 1] = piece end
end

dofile(HERE .. "/dump.lua")

-- ------------------------------------------------------------------ the run

-- Before the dump point: nothing should be emitted.
machine_time = 0.5
periodic_notifier()
local premature = #captured

-- At the dump point.
machine_time = 1.0
periodic_notifier()

-- A second frame must not dump again.
machine_time = 1.5
periodic_notifier()

io.write = real_write
os.getenv = real_getenv

local output = table.concat(captured)
local lines = {}
for line in output:gmatch("[^\n]*\n?") do
	if line ~= "" then lines[#lines + 1] = (line:gsub("\n$", "")) end
end

-- -------------------------------------------------------------- the checks

check("nothing is dumped before the dump point", premature, 0)
check("the machine is asked to exit", exited, true)
check("the dump happens exactly once",
	select(2, output:gsub("# apollo oracle dump", "")), 1)

check("header names the machine", lines[1], "# apollo oracle dump")
check("machine line", lines[2], "machine dn3500")
check("time is the requested point, not the frame it landed on",
	lines[3], "at 1.000000")
check("cpu section names the tag", lines[4], "[cpu :maincpu]")

-- Registers, sorted by symbol: D0, FP0, PC, SR.
check("registers are sorted by symbol, not declaration order",
	lines[5], "D0       DEADBEEF")
check("a float register avoids the integer format",
	lines[6], "FP0      0.5")
check("a 4-byte register prints 8 digits", lines[7], "PC       00010000")
check("a 2-byte register prints 4 digits, from its own datasize",
	lines[8], "SR       2700")

check("memory section header", lines[9], "[mem :maincpu/program 00001000+24]")

-- 0x24 = 36 bytes -> 16 + 16 + 4.
check("first hex line",
	lines[10],
	"00001000  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  |................|")
check("second hex line shows the printable run",
	lines[11],
	"00001010  41 42 43 44 45 46 47 48 18 19 1A 1B 1C 1D 1E 1F  |ABCDEFGH........|")
-- Built with an explicit run of spaces rather than a hand-counted literal:
-- the hex column is 47 wide (16 bytes as "XX" joined by 15 spaces), so a
-- 4-byte tail pads by 36, plus the 2-space gutter.
check("a partial tail line is flushed with its own address",
	lines[12],
	"00001020  20 21 22 23" .. string.rep(" ", 36 + 2) .. "| !\"#|")

-- The property that actually matters for diffing: a short tail line still puts
-- its ascii column where the full lines put theirs.
check("a tail line stays aligned with full lines",
	lines[12]:find("|", 1, true), lines[10]:find("|", 1, true))
check("end marker", lines[13], "# end")
check("nothing follows the end marker", #lines, 13)

-- ------------------------------------------------- the stop-notifier path

-- A machine that stops before the dump point must still produce a dump,
-- so a short run is never silently mistaken for a passing one.
captured = {}
exited = false
io.write = function (...)
	for _, piece in ipairs({ ... }) do captured[#captured + 1] = piece end
end
stop_notifier()
io.write = real_write
check("a second dump is suppressed once one has been written",
	table.concat(captured), "")

io.write(string.format("\n%s: %d failure(s)\n",
	failures == 0 and "PASS" or "FAIL", failures))
os.exit(failures == 0 and 0 or 1)
