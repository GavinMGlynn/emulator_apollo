-- Measure the oracle's per-instruction time, by side-loading a probe into RAM
-- and single-stepping it.
--
-- Loaded by MAME as -autoboot_script, and it needs `-debug -debugger none`:
-- without `-debug` the `debug` field on a device is nil and `step()` cannot be
-- called at all, and `-debugger none` is what keeps the run headless. See
-- FINDINGS.md C5.
--
-- The contract is the same as dump.lua's: determinism. Nothing here observes
-- the host -- no wall clock, no os.time, no unsorted table walk.
--
-- Configuration arrives through environment variables, because -autoboot_script
-- takes a path and nothing else:
--
--   APOLLO_STEP_AT      emulated seconds at which to take the machine over
--                       (default 2.0 -- late enough that reset has settled)
--   APOLLO_STEP_ADDR    where to side-load, hexadecimal (default 0x01001000)
--   APOLLO_STEP_WORDS   the probe, comma-separated hexadecimal instruction
--                       words, e.g. "4E71,4E71"
--   APOLLO_STEP_COUNT   how many instructions to step (default 8)
--   APOLLO_STEP_CLOCK   CPU clock in Hz (default 25000000, the DN3500's)
--   APOLLO_STEP_REGS    registers to set before stepping, "A0=01002000,D1=3",
--                       values hexadecimal. Needed by any probe with a memory
--                       operand: without an address register pointing at
--                       writable RAM, a store measures a refused write rather
--                       than a real bus cycle, and the timing would be a
--                       plausible number for the wrong thing.
--
-- ## Why it must be loaded into RAM
--
-- A write to the boot PROM's address range reports success and does nothing --
-- FINDINGS.md C5 records reading back the PROM's own contents after writing
-- $4E71 to $1000. So the default address is in RAM, and the script *checks the
-- readback* and refuses rather than silently measuring the PROM.
--
-- ## Why the time is reported in attoseconds as well as clocks
--
-- `total_cycles` is not bound in this MAME build, so a cycle count has to come
-- from emulated time divided by the clock period. That is exact only if MAME
-- advances time in whole cycles. Printing the raw attoseconds beside the
-- derived clock count is what lets a reader see that it did, rather than take
-- the division on trust -- and a fractional result is the signal that this
-- whole approach needs revisiting.

local step_at    = tonumber(os.getenv("APOLLO_STEP_AT") or "") or 2.0
local step_addr  = tonumber(os.getenv("APOLLO_STEP_ADDR") or "", 16)
                   or tonumber(os.getenv("APOLLO_STEP_ADDR") or "") or 0x01001000
local words_spec = os.getenv("APOLLO_STEP_WORDS") or "4E71"
local step_count = tonumber(os.getenv("APOLLO_STEP_COUNT") or "") or 8
local clock_hz   = tonumber(os.getenv("APOLLO_STEP_CLOCK") or "") or 25000000
local regs_spec  = os.getenv("APOLLO_STEP_REGS") or ""

-- One clock in attoseconds. 1e18 attoseconds is one second.
local clock_attos = 1000000000000000000 // clock_hz

local phase = 0
local last = nil
local stepped = 0
-- `manager.machine:exit()` requests an exit; the periodic can still fire once
-- more before it takes effect, which printed a spurious extra row. A finished
-- flag is what makes the report end where it says it does.
local finished = false

local function out(fmt, ...)
	io.write(string.format(fmt, ...))
end

local function parse_words(spec)
	local words = {}
	for item in spec:gmatch("[^,]+") do
		local value = tonumber((item:gsub("%s", "")), 16)
		if value == nil then
			out("# ERROR unparsable word %q\n", item)
			return nil
		end
		words[#words + 1] = value
	end
	return words
end

-- "A0=01002000,D1=3" -> applied to the CPU's state. Unparsable entries are
-- reported rather than skipped: a probe that silently ran with an unset address
-- register would measure something, and the something would look reasonable.
local function apply_registers(cpu, spec)
	for item in spec:gmatch("[^,]+") do
		local name, value = item:match("^%s*(%w+)%s*=%s*(%x+)%s*$")
		if name == nil then
			out("# ERROR unparsable register assignment %q\n", item)
			return false
		end
		local entry = cpu.state[name]
		if entry == nil then
			out("# ERROR no such register %q\n", name)
			return false
		end
		entry.value = tonumber(value, 16)
		out("# set %s = %08X\n", name, entry.value)
	end
	return true
end

-- Emulated time as one integer, so a delta needs no carry handling.
local function now_attos()
	local t = manager.machine.time
	return t.seconds * 1000000000000000000 + t.attoseconds
end

emu.register_periodic(function()
	if finished or manager.machine.time.seconds < step_at then
		return
	end

	local cpu = manager.machine.devices[":maincpu"]
	local space = cpu.spaces["program"]

	if phase == 0 then
		phase = 1

		local words = parse_words(words_spec)
		if words == nil then
			finished = true
			manager.machine:exit()
			return
		end

		for i, word in ipairs(words) do
			space:write_u16(step_addr + (i - 1) * 2, word)
		end

		-- Refuse rather than measure the wrong thing: a write to ROM reports
		-- success and does nothing, so the readback is the only evidence the
		-- probe is actually there.
		local readback = space:read_u16(step_addr)
		if readback ~= words[1] then
			out("# ERROR side-load failed at %08X: wrote %04X, read %04X\n",
			    step_addr, words[1], readback)
			out("# (a write to the boot PROM's range succeeds and does nothing)\n")
			finished = true
			manager.machine:exit()
			return
		end

		out("# steptime addr=%08X clock=%d Hz clock_attos=%d\n",
		    step_addr, clock_hz, clock_attos)
		out("# words=%s count=%d\n", words_spec, step_count)
		out("# step pc_before pc_after attos clocks\n")

		if not apply_registers(cpu, regs_spec) then
			finished = true
			manager.machine:exit()
			return
		end

		cpu.state["PC"].value = step_addr
		last = nil
		cpu.debug:step()
		return
	end

	local current = now_attos()
	local pc = cpu.state["PC"].value

	if last ~= nil then
		local delta = current - last.attos
		-- Integer division and remainder, so a fractional result is visible as
		-- a non-zero remainder rather than rounded away.
		out("%d %08X %08X %d %d", stepped, last.pc, pc, delta,
		    delta // clock_attos)
		if delta % clock_attos ~= 0 then
			out(" FRACTIONAL")
		end
		out("\n")
		stepped = stepped + 1
	end

	last = {attos = current, pc = pc}

	if stepped >= step_count then
		out("# end\n")
		finished = true
		manager.machine:exit()
		return
	end
	cpu.debug:step()
end)
