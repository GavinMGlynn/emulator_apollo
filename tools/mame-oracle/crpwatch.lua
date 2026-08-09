-- Log every change of the 68030's MMU root pointers in the oracle.
--
-- The question this exists for: our core executes **no** `PMOVE` from
-- Domain/OS, so the kernel runs its whole life on `SELF_TEST`'s tree and dies on
-- an address that tree never mapped. Either the oracle does the same and the
-- defect is elsewhere, or it loads `0105BC00` somewhere we do not -- and the
-- instruction that does it is the answer. `PROJECT_STATUS.md` names this as the
-- one measurement the investigation has not made.
--
-- ## Why this polls state instead of hooking an instruction
--
-- The previous attempt side-loaded a probe into the oracle's `PMOVE` path. It
-- was verified present in the binary, three boots ran, and it never fired --
-- and nothing could distinguish "the oracle does not do this" from "the probe
-- is not wired". Polling `CRP_APTR`/`CRP_LIMIT`, which MAME exposes as ordinary
-- CPU state registers (`m68kcpu.cpp`, `state_add(M68K_CRP_APTR, ...)`), cannot
-- fail that way: if the register changes, the value changes, whatever
-- instruction changed it and whether or not MAME's `PMOVE` path is what we
-- think it is.
--
-- **And it is proved rather than trusted.** `SELF_TEST` performs eight known
-- root-pointer loads before Domain/OS is reached -- our own core logs them, the
-- last being `CRP <- 00FF0002 01001400` from PC `01002324`. A run of this script
-- that does not report those has not shown the oracle to be quiet; it has shown
-- itself to be broken, and says so rather than producing a plausible silence.
--
-- ## Traps this inherits from screencap.lua, which cost a session each
--
--  * `machine.time.seconds` is the attotime's integer field. `as_double()` is
--    the instant.
--  * The autoboot script does not run until the first timeslice after reset,
--    roughly 100,000 instructions of boot PROM. That is early enough here:
--    every root-pointer load of interest is in `SELF_TEST`, which is loaded
--    from disk long afterwards.
--
-- ## Use
--
--   mame ... -autoboot_script tools/mame-oracle/crpwatch.lua
--
-- Writes `crpwatch.log` in the working directory, flushed on every line, and a
-- heartbeat every two emulated seconds so a silent run can be told from a dead
-- one. A killed run keeps everything written so far.

local LOG = os.getenv("APOLLO_CRP_LOG") or "crpwatch.log"
local HEARTBEAT_S = 2.0

local f = assert(io.open(LOG, "w"))
local cpu = manager.machine.devices[":maincpu"]

local function reg(name)
	local ok, v = pcall(function() return cpu.state[name].value end)
	if ok then return v end
	return nil
end

local last = {}
local next_beat = 0.0
local changes = 0

f:write("# crpwatch: root-pointer changes in the oracle\n")
f:write("# columns: time_s  register  value  pc\n")
f:flush()

-- Fail loudly at the start rather than quietly for ten minutes: if the state
-- names are not what this expects, nothing below can ever report anything.
do
	local missing = {}
	for _, n in ipairs({ "CRP_APTR", "CRP_LIMIT", "SRP_APTR", "SRP_LIMIT", "PC" }) do
		if reg(n) == nil then missing[#missing + 1] = n end
	end
	if #missing > 0 then
		f:write("FATAL missing CPU state register(s): " .. table.concat(missing, " ") .. "\n")
		f:write("FATAL this run proves nothing about the oracle\n")
		f:flush()
	else
		f:write("# all root-pointer state registers present\n")
		f:flush()
	end
end

emu.register_periodic(function()
	local t = manager.machine.time:as_double()
	local pc = reg("PC") or 0
	for _, n in ipairs({ "CRP_APTR", "CRP_LIMIT", "SRP_APTR", "SRP_LIMIT" }) do
		local v = reg(n)
		if v ~= nil and v ~= last[n] then
			if last[n] ~= nil then
				changes = changes + 1
			end
			last[n] = v
			f:write(string.format("%10.4f  %-9s %08X  pc %08X\n", t, n, v, pc))
			f:flush()
		end
	end
	if t >= next_beat then
		next_beat = t + HEARTBEAT_S
		f:write(string.format("%10.4f  heartbeat changes=%d pc %08X\n", t, changes, pc))
		f:flush()
	end
end)
