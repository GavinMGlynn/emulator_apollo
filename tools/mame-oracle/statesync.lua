-- Stop the oracle at a chosen program counter and dump its whole state there.
--
-- This is the oracle half of the state differential. Ours is
-- `apollo-headless --boot-stop-pc ADDR --boot-stop-pc-skip N --dump-state FILE`;
-- this produces the file to compare it with, at the same point in the same
-- program. `tools/state-diff.py` does the comparing, through
-- `tools/mame-oracle/state-map.txt`.
--
-- ## Why a breakpoint and not a read tap
--
-- Five earlier attempts to synchronise the two machines used **read** taps, and
-- all five failed for the same reason: `install_read_tap` sees bus reads, and
-- cannot tell an instruction fetch from a data read of the same address. Gating
-- one on the PC then fails the other way -- by the time the tap fires the PC has
-- moved, so an exact comparison never matches and a windowed one matches the
-- wrong things. A read tap is the wrong instrument for "stop at this
-- instruction".
--
-- That is not an argument against the **write** tap this file also offers. A
-- write to a diagnostic register is an unambiguous event rather than a guess at
-- where execution is, and it is the mode below.
--
-- MAME already has the right one. `-debug -debugger none` gives a machine with
-- no debugger UI but a working `device_debug`, so `bpset` stops exactly at an
-- instruction boundary at exactly the requested address, which is what a state
-- comparison needs and what a tap cannot offer.
--
-- **`-debugger none` is what makes this usable headlessly**: `machine.debugger`
-- is nil unless `-debug` is given, and every other debugger module wants a
-- window. When a breakpoint hits there is no UI to resume from, so this script
-- polls `execution_state` from a periodic callback and drives it itself.
--
-- ## Determinism, and the one thing not claimed
--
-- Nothing here reads the host or posts input. `-debug` installs an instruction
-- hook, which costs speed; it is not known to change emulated behaviour, and
-- the check that it does not is to take one dump with it and one without at an
-- earlier point and compare -- worth doing once before any conclusion rests on
-- a `-debug` run.
--
-- ## Two ways to say where, and they are not equivalent
--
-- `APOLLO_SYNC_PC` is the exact one: a breakpoint at an instruction boundary,
-- aligned with ours by the single step described below.
--
-- `APOLLO_SYNC_WRITE` is the one that needs no debugger: a **write tap** on a
-- physical address, which is how the two machines can be synchronised on a
-- *posted diagnostic code* -- a whole-boot marker that means the same thing on
-- both machines however differently they got there. `PROJECT_STATUS.md` records
-- that `-debug -debugger none` has been seen to stop the DN3500 booting past
-- the boot PROM, and this mode is the answer if that turns out to still hold.
--
-- **The tap fires inside the bus cycle, not at an instruction boundary.** Our
-- `--boot-stop-on-watch-write` completes the instruction that did the write;
-- this stops in the middle of it, with the PC not yet advanced. The two dumps
-- are therefore *not* aligned, and comparing them would report every register
-- the rest of that instruction touches. Whichever side is adjusted, it must be
-- adjusted deliberately -- see STATE_DIFF.md. This mode is for the case where
-- the exact one is unavailable, and it says so rather than pretending.
--
-- ## Configuration
--
--   APOLLO_SYNC_PC     hex address to stop at, e.g. "3C43DD80"
--   APOLLO_SYNC_WRITE  hex *physical* address to tap instead, e.g. "10100"
--   APOLLO_SYNC_COUNT  which write to dump on, 1-based (default 1)
--   APOLLO_SYNC_SKIP   ignore this many hits first (default 0), for an address
--                      inside a loop or a routine called more than once
--   APOLLO_SYNC_DUMP   where to write the state dump (default "oracle.state")
--   APOLLO_SYNC_MODE   "normal" (default) or "service", as `screencap.lua`
--   APOLLO_SYNC_GIVEUP emulated seconds after which to give up and say so
--                      (default 120). A run that never reaches the address must
--                      end saying that, not sit there looking busy.

local pc_text  = os.getenv("APOLLO_SYNC_PC")
local wr_text  = os.getenv("APOLLO_SYNC_WRITE")
local wr_count = tonumber(os.getenv("APOLLO_SYNC_COUNT") or "1") or 1
local skip     = tonumber(os.getenv("APOLLO_SYNC_SKIP") or "0") or 0
local dump_to  = os.getenv("APOLLO_SYNC_DUMP") or "oracle.state"
local normal   = (os.getenv("APOLLO_SYNC_MODE") or "normal") ~= "service"
local giveup_s = tonumber(os.getenv("APOLLO_SYNC_GIVEUP") or "120") or 120

local function out(fmt, ...)
	io.write(string.format(fmt, ...))
	io.flush()
end

local G = _G
G.APOLLO_SYNC = G.APOLLO_SYNC or { armed = false, base = 0, hits = 0, done = false }
local S = G.APOLLO_SYNC

-- `:apollo_config` is read at MACHINE_RESET, so setting it from the first
-- periodic callback changes nothing until the machine is reset again. Same
-- trap, same fix, as `screencap.lua` -- and the guard has to live in `_G`
-- because the reset re-runs this chunk with fresh locals.
local function set_mode()
	local port = manager.machine.ioport.ports[":apollo_config"]
	if port == nil then
		out("# no :apollo_config port\n")
		return
	end
	for name, field in pairs(port.fields) do
		if name:find("Service") then
			field.user_value = normal and 1 or 0
			out("# %s = %s\n", name, normal and "Normal" or "Service")
			return
		end
	end
	out("# no Service field in :apollo_config\n")
end

local function now_s() return manager.machine.time:as_double() - S.base end

-- The CPU's program counter, by whichever name this core exposes. `CURPC` is
-- the one that means "where execution is now" on the cores that have both;
-- asking for a name that is absent returns nil rather than raising, so this
-- reports nil and lets the caller say so instead of dying inside a callback.
local function current_pc()
	if S.cpu == nil then return nil end
	local ok, entries = pcall(function() return S.cpu.state end)
	if not ok or entries == nil then return nil end
	for _, name in ipairs({ "CURPC", "PC" }) do
		local entry = entries[name]
		if entry ~= nil then return entry.value end
	end
	return nil
end

local function finish(why)
	if S.done then return end
	S.done = true
	out("# %s\n", why)
	manager.machine:exit()
end

if pc_text == nil and wr_text == nil then
	out("# neither APOLLO_SYNC_PC nor APOLLO_SYNC_WRITE is set\n")
	return
end
local target = pc_text and tonumber(pc_text, 16) or nil
if pc_text ~= nil and target == nil then
	out("# APOLLO_SYNC_PC is not hexadecimal: %s\n", pc_text)
	return
end
local watch = wr_text and tonumber(wr_text, 16) or nil
if wr_text ~= nil and watch == nil then
	out("# APOLLO_SYNC_WRITE is not hexadecimal: %s\n", wr_text)
	return
end

-- The tap mode. Installed once, after the reset this script performs, on the
-- CPU's program space -- which on a machine whose MMU lives inside the CPU is
-- the **physical** side, the same addresses `--boot-watch-write` takes.
local function install_write_tap()
	local cpu = manager.machine.devices[":maincpu"]
	if cpu == nil then finish("no :maincpu") return end
	S.cpu = cpu
	local space = cpu.spaces["program"]
	if space == nil then finish("no program space on :maincpu") return end
	S.seen = 0
	-- A word write to the diagnostic register covers both bytes, so the tap is
	-- two bytes wide and counts one event per access rather than per byte.
	S.tap = space:install_write_tap(watch, watch + 1, "apollo_sync",
		function(offset, data, mask)
			if S.done then return end
			S.seen = S.seen + 1
			out("# write %d to %08X: data %04X mask %04X\n",
			    S.seen, watch, data & 0xFFFF, mask & 0xFFFF)
			if S.seen < wr_count then return end
			-- **Mid-instruction.** The PC has not advanced and the rest of this
			-- instruction has not run. Said here as well as in the header
			-- because this is the line whose output someone will compare.
			manager.machine:apollo_dump_state(dump_to)
			finish(string.format(
				"dumped to %s inside write %d -- MID-INSTRUCTION, not aligned "
				.. "with a --boot-stop-on-watch-write dump", dump_to, S.seen))
		end)
	out("# write tap on %08X, dumping on write %d\n", watch, wr_count)
end

emu.register_periodic(function()
	if S.done then return end

	if not S.armed then
		S.armed = true
		set_mode()
		S.base = manager.machine.time:as_double()
		if watch ~= nil then
			out("# state sync: write %d to %08X, mode=%s\n",
			    wr_count, watch, normal and "normal" or "service")
		else
			out("# state sync: stop at %08X, skip %d, mode=%s\n",
			    target, skip, normal and "normal" or "service")
		end
		manager.machine:soft_reset()
		return
	end

	-- Write-tap mode needs no debugger at all, which is the point of it.
	if watch ~= nil then
		if S.tap == nil then install_write_tap() end
		if now_s() >= giveup_s then
			finish(string.format(
				"gave up after %.1fs emulated with %d write(s) to %08X",
				now_s(), S.seen or 0, watch))
		end
		return
	end

	local debugger = manager.machine.debugger
	if debugger == nil then
		finish("no debugger: run with -debug -debugger none")
		return
	end

	if S.bp == nil then
		local cpu = manager.machine.devices[":maincpu"]
		if cpu == nil then
			finish("no :maincpu")
			return
		end
		S.cpu = cpu
		S.bp = cpu.debug:bpset(target, "", "")
		out("# breakpoint %s set at %08X\n", tostring(S.bp), target)
		debugger.execution_state = "run"
		return
	end

	-- ## The two machines stop on opposite sides of the same instruction
	--
	-- MAME's breakpoint stops **before** executing the instruction at the
	-- address: it halts with PC == target. This core's `--boot-stop-pc` stops
	-- **after** executing it -- `--boot-stop-pc 653A` ends with PC = 6542, the
	-- next instruction.
	--
	-- Diffing those two states directly compares a machine that has run the
	-- instruction against one that has not, so every register the instruction
	-- touched differs. That is a difference in the *harness*, and it would read
	-- exactly like a difference in the emulator.
	--
	-- The fix belongs here rather than in the core: a stop-after rule is a
	-- reasonable thing for a boot harness to have, and changing it to suit a
	-- comparison would be the tail wagging the dog. So the oracle takes one
	-- step after the hit and lands where we do.
	if S.stepping then
		-- `step` is asynchronous, and the first poll after asking for one can
		-- still show the old PC -- so the test is that the PC actually moved,
		-- not that the debugger is stopped. Waiting on the state alone dumps
		-- the pre-step machine and looks like it worked.
		local pc = current_pc()
		if debugger.execution_state == "stop" and pc ~= nil and pc ~= S.hit_pc then
			out("# stepped %08X -> %08X -- dumping\n", S.hit_pc, pc)
			manager.machine:apollo_dump_state(dump_to)
			finish(string.format("dumped to %s at PC %08X", dump_to, pc))
		end
		return
	end

	-- A breakpoint hit shows up as the debugger stopping. There is no UI to
	-- notice it, so the poll is the notice.
	if debugger.execution_state == "stop" then
		S.hits = S.hits + 1
		if S.hits <= skip then
			out("# hit %d at %.6fs, skipping\n", S.hits, now_s())
			debugger.execution_state = "run"
			return
		end
		S.hit_pc = current_pc()
		out("# hit %d at %08X, %.6fs emulated -- stepping one to match ours\n",
		    S.hits, S.hit_pc or target, now_s())
		S.stepping = true
		S.cpu.debug:step(1)
		debugger.execution_state = "run"
		return
	end

	if now_s() >= giveup_s then
		finish(string.format(
			"gave up after %.1fs emulated without reaching %08X (%d hit(s) seen)",
			now_s(), target, S.hits))
	end
end)
