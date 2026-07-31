-- Measure the Apollo core-board registers by exercising them, not by reading
-- anyone's source.
--
-- `008778-03` Table 2-8 gives these registers an address each and says nothing
-- about their bits. The architecture handbook that carries the bit layouts
-- (`019411-A00` patches its Chapter 4) is not in `docs/references/`, and
-- `CLAUDE.md`'s rule is that behaviour comes from a manual or from the oracle.
-- With no manual, this is the oracle route: drive each bit and record what the
-- machine does with it.
--
-- That the oracle is GPL and this core is MIT is exactly why this is a probe
-- and not a transcription. `CLAUDE.md` says "build and instrument `ext/mame`",
-- and instrumenting means running it and writing down what happened. What comes
-- out of this script is a measurement we made, reproducible by anyone with the
-- same checkout, in the same form as every other figure in this project.
--
-- Loaded by MAME as -autoboot_script, and it needs `-debug -debugger none` for
-- the same reason steptime.lua does: without `-debug` the `debug` field on a
-- device is nil. See FINDINGS.md C5.
--
-- Determinism, as in dump.lua and steptime.lua: nothing here observes the host.
-- No wall clock, no os.time, no unsorted table walk. The register list is an
-- array and is walked in order.
--
-- ## What is measured, and why each step is there
--
-- For every register, and for every bit position in turn:
--
--   1. read the register, so the original value can be put back
--   2. write a value with just that bit set, and read back
--   3. write a value with just that bit clear (all others set), and read back
--   4. restore the original
--
-- Step 3 is not redundant with step 2. A bit that reads back set after (2) and
-- also set after (3) is stuck high, not writable, and a probe that only ever
-- wrote ones would call it writable. Likewise a write-1-to-clear bit answers
-- the two steps differently from a plain read/write bit. Both patterns exist in
-- registers of this kind and neither is visible from one direction alone.
--
-- Restoring in (4) matters more here than it would elsewhere: these are control
-- registers on a running machine, and at least one bit on this board is
-- documented to reset devices. A probe that left a register full of ones would
-- be measuring a different machine by the time it reached the next row.
--
-- ## Configuration
--
--   APOLLO_REG_AT     emulated seconds at which to start (default 2.0, late
--                     enough that reset has settled -- the same figure
--                     steptime.lua uses and for the same reason)
--   APOLLO_REG_LIST   registers to probe, "name@hex:width,...". Width is 8, 16
--                     or 32. Defaults to the six core-board registers of
--                     `008778-03` Table 2-8 that have no published bit layout.
--   APOLLO_REG_WIDTH  default width when a list entry omits one (default 16)
--
-- A register is probed in one pass and restored before the next begins, so a
-- destructive bit cannot silently poison later rows. If one turns out to
-- disturb the machine anyway, probe it alone with APOLLO_REG_LIST.

local probe_at    = tonumber(os.getenv("APOLLO_REG_AT") or "") or 2.0
local default_w   = tonumber(os.getenv("APOLLO_REG_WIDTH") or "") or 16

-- `008778-03` Table 2-8, the DS4000/DS3500 64 MB physical allocation. These are
-- the registers Phase 3 names that Table 2-8 locates and no manual here lays
-- out. Addresses are the base of each range.
local DEFAULT_LIST =
	"cpu_status@010000:16," ..
	"cpu_control@010100:16," ..
	"cache_control@010200:16," ..
	"task_alias@010300:16," ..
	"latch_page_on_parity@011300:16," ..
	"master_request@011600:16"

local list_spec = os.getenv("APOLLO_REG_LIST") or DEFAULT_LIST

local finished = false
local started = false

local function out(fmt, ...)
	io.write(string.format(fmt, ...))
end

-- "name@hex:width" entries, in the order given. Order is part of the output's
-- determinism, so this returns an array and never a keyed table.
local function parse_list(spec)
	local entries = {}
	for item in spec:gmatch("[^,]+") do
		local name, addr, width = item:match("^%s*([%w_]+)@(%x+):?(%d*)%s*$")
		if name == nil then
			out("# ERROR unparsable register entry %q\n", item)
			return nil
		end
		width = tonumber(width) or default_w
		if width ~= 8 and width ~= 16 and width ~= 32 then
			out("# ERROR bad width %d for %q\n", width, name)
			return nil
		end
		entries[#entries + 1] = {
			name = name,
			address = tonumber(addr, 16),
			width = width,
		}
	end
	return entries
end

local function reader(space, width)
	if width == 8 then return function(a) return space:read_u8(a) end end
	if width == 16 then return function(a) return space:read_u16(a) end end
	return function(a) return space:read_u32(a) end
end

local function writer(space, width)
	if width == 8 then return function(a, v) space:write_u8(a, v) end end
	if width == 16 then return function(a, v) space:write_u16(a, v) end end
	return function(a, v) space:write_u32(a, v) end
end

-- Classify one bit from the two probes. Named rather than returned as flags,
-- because the point of the exercise is a vocabulary a reader can act on.
local function classify(bit_set_after_one, bit_set_after_zero)
	if bit_set_after_one and not bit_set_after_zero then
		return "rw" -- follows what was written
	end
	if not bit_set_after_one and not bit_set_after_zero then
		return "ro0" -- reads zero whatever is written
	end
	if bit_set_after_one and bit_set_after_zero then
		return "ro1" -- reads one whatever is written
	end
	-- Set after writing zero and clear after writing one: the bit inverts what
	-- was written, or writing one clears it. Both are real behaviours and this
	-- probe cannot tell them apart in one pass; say so rather than pick.
	return "inv/w1c"
end

emu.register_periodic(function()
	if finished or started then
		return
	end
	if manager.machine.time.seconds < probe_at then
		return
	end
	started = true

	local entries = parse_list(list_spec)
	if entries == nil then
		finished = true
		manager.machine:exit()
		return
	end

	local cpu = manager.machine.devices[":maincpu"]
	local space = cpu.spaces["program"]

	out("# apollo core-board register probe\n")
	out("# source: measurement against the oracle; no published bit layout exists\n")
	out("# addresses from 008778-03 Table 2-8\n")
	out("# columns: register address width initial bit classification\n")

	for i = 1, #entries do
		local e = entries[i]
		local read = reader(space, e.width)
		local write = writer(space, e.width)
		local mask = (e.width == 32) and 0xFFFFFFFF or ((1 << e.width) - 1)

		local original = read(e.address) & mask
		out("%s %06X %d initial=%0" .. (e.width // 4) .. "X\n",
		    e.name, e.address, e.width, original)

		for bit = 0, e.width - 1 do
			local one = 1 << bit

			write(e.address, one)
			local after_one = read(e.address) & mask
			write(e.address, mask & ~one)
			local after_zero = read(e.address) & mask
			-- Put it back before moving to the next bit, so a bit that
			-- disturbs the machine disturbs it for as short a time as
			-- possible.
			write(e.address, original)

			local kind = classify((after_one & one) ~= 0, (after_zero & one) ~= 0)
			out("%s bit%02d %s wrote1=%0" .. (e.width // 4) ..
			    "X wrote0=%0" .. (e.width // 4) .. "X\n",
			    e.name, bit, kind, after_one, after_zero)
		end

		-- And leave it as we found it, in case the per-bit restore above was
		-- itself altered by a side effect.
		write(e.address, original)
		local final = read(e.address) & mask
		if final ~= original then
			-- Not a failure of the probe: a register whose value moved on its
			-- own is telling us something, and hiding it would be worse.
			out("%s NOTE did-not-restore original=%0" .. (e.width // 4) ..
			    "X final=%0" .. (e.width // 4) .. "X\n",
			    e.name, original, final)
		end
	end

	out("# end\n")
	finished = true
	manager.machine:exit()
end)
