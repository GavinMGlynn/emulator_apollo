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
--   APOLLO_SYNC_VALUE  hex byte to match instead of counting -- **prefer this**.
--                      The tap is installed after this script's reset and so
--                      misses the writes before it (measured: the first four),
--                      which makes a count an offset nobody can verify. A
--                      posted code is the same number on both machines.
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
local wr_value = os.getenv("APOLLO_SYNC_VALUE")
wr_value = wr_value and tonumber(wr_value, 16) or nil
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
-- Returns true when the machine has to be reset for the setting to take, and
-- **false when it is already right** -- which is the case worth having. The
-- port is read at `MACHINE_RESET`, so setting it here needs a reset to take
-- effect, and a reset is expensive in ways that are not obvious: it invalidates
-- the debugger (`machine.debugger` is nil afterwards under `-debugger none`),
-- it re-runs this script against a machine that does not exist yet, and it
-- moves the point the run starts from.
--
-- All of that is avoidable. MAME **persists this port** to
-- `cfg/dn3500.cfg`, so seeding that directory with
-- `tools/mame-oracle/dn3500-normal.cfg` starts the machine in Normal mode with
-- nothing to change and nothing to reset. `state-compare.sh` does exactly that,
-- and this function then reports "already", which is the fast path.
local function set_mode()
	local port = manager.machine.ioport.ports[":apollo_config"]
	if port == nil then
		out("# no :apollo_config port\n")
		return false
	end
	local want = normal and 1 or 0
	for name, field in pairs(port.fields) do
		if name:find("Service") then
			local have = field.user_value
			if have == want then
				out("# %s already %s; no reset needed\n", name,
				    normal and "Normal" or "Service")
				return false
			end
			field.user_value = want
			out("# %s = %s (was %s); reset required\n", name,
			    normal and "Normal" or "Service", tostring(have))
			return true
		end
	end
	out("# no Service field in :apollo_config\n")
	return false
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
-- Returns false when the machine is not ready yet, which is **not** an error:
-- a hard reset tears the machine down and rebuilds it, and the first periodic
-- callback afterwards can see a `:maincpu` whose address spaces do not exist
-- yet. Treating that as fatal ended the run before the firmware had started.
-- A genuine failure -- an install that throws -- is still fatal, and the caller
-- tells them apart.
local function install_write_tap()
	local cpu = manager.machine.devices[":maincpu"]
	if cpu == nil then return false end
	local space = cpu.spaces["program"]
	if space == nil then return false end
	S.cpu = cpu
	S.seen = 0
	-- **The range must be aligned to the space's granularity.** This is a
	-- 32-bit space, so a tap has to start on a longword boundary and end with
	-- its low bits set: asking for `10100`-`10101` is refused with
	-- "end address has low bits unset, did you mean 10103?". The tap therefore
	-- covers the whole longword and the callback filters, rather than the range
	-- being narrowed to what is wanted.
	local lo = watch - (watch % 4)
	local hi = lo + 3
	S.tap = space:install_write_tap(lo, hi, "apollo_sync",
		function(offset, data, mask)
			if S.done then return end
			-- **The offset is the access's, not the byte's.** In a 32-bit space
			-- a byte write is reported at the aligned address with a mask
			-- selecting the lane, so testing `offset == watch` silently rejects
			-- every write to an odd address -- which is where this machine's
			-- DUART registers live. Accept anything in the longword and let the
			-- caller's value match discriminate; `APOLLO_SYNC_STRICT=1` restores
			-- the exact test for a register that really is longword-aligned.
			if os.getenv("APOLLO_SYNC_STRICT") ~= nil and offset ~= watch then
				return
			end
			S.seen = S.seen + 1
			-- The PC as well as the value: "which instruction wrote this" is
			-- the question a differential asks next about every difference it
			-- finds, and recovering it afterwards costs another whole run.
			-- Musashi's PC has already advanced past the opcode here, so this
			-- is *after* the writing instruction, not its address.
			out("# write %d at %08X: data %08X mask %08X pc %08X\n",
			    S.seen, offset, data, mask, current_pc() or 0)
			-- **Match the posted value, not the count, when one is given.**
			-- This tap is installed after the reset this script performs, so it
			-- misses the writes that happen in between -- measured as the first
			-- four, making the oracle's write N our write N+4. A fixed offset
			-- would be a constant nobody can check; the posted byte is the same
			-- number on both machines and needs no correction.
			if wr_value ~= nil then
				if (data & 0xFF) ~= wr_value then return end
			elseif S.seen < wr_count then
				return
			end
			-- **Mid-instruction.** The PC has not advanced and the rest of this
			-- instruction has not run. Said here as well as in the header
			-- because this is the line whose output someone will compare.
			manager.machine:apollo_dump_state(dump_to)
			finish(string.format(
				"dumped to %s inside write %d -- MID-INSTRUCTION, not aligned "
				.. "with a --boot-stop-on-watch-write dump", dump_to, S.seen))
		end)
	if wr_value ~= nil then
		out("# write tap on %08X, dumping on the write posting %02X\n",
		    watch, wr_value)
	else
		out("# write tap on %08X, dumping on write %d\n", watch, wr_count)
	end
	return true
end

-- **Arm the tap before the machine runs.** `emu.register_periodic` does not
-- fire until the first timeslice, about 17 ms of emulated time -- roughly
-- 425,000 instructions of boot PROM. Anything the firmware does in that window
-- is invisible to a tap installed from there, and that is not a small window:
-- the DUART's interrupt mask is already programmed by 0.015 s, so a tap armed
-- at 17 ms sees *zero* writes to it and reads as "the oracle never writes this
-- register" when the truth is "the tap was late".
--
-- `emu.register_prestart` runs before reset, which is early enough. This is
-- also why the tap mode no longer needs a reset of its own to catch the first
-- posted codes.
if watch ~= nil then
	emu.register_prestart(function()
		if S.done or S.tap ~= nil then return end
		local ok, ready = pcall(install_write_tap)
		if ok and ready then S.prearmed = true end
	end)
end

emu.register_periodic(function()
	if S.done then return end

	-- ## The reset is *scheduled*, and the machine after it is a new one
	--
	-- `hard_reset()` does not happen when it is called. Callbacks keep running
	-- against the machine being torn down, so anything established in them is
	-- established on the wrong machine: a breakpoint set there is reported as
	-- set, and then `machine.debugger` is nil on the next call because the
	-- object it belonged to is gone.
	--
	-- Emulated time restarting is what says the new machine has arrived -- it
	-- is the one observable that cannot be true of the old one. Every handle is
	-- dropped at that point and re-established against the machine that will
	-- actually run.
	local t = manager.machine.time:as_double()
	if S.last_time ~= nil and t < S.last_time then
		out("# machine restarted (%.6fs -> %.6fs); re-arming\n", S.last_time, t)
		S.bp, S.tap, S.cpu, S.seen, S.hits = nil, nil, nil, 0, 0
		S.stepping, S.hit_pc = nil, nil
		S.base = t
	end
	S.last_time = t

	if not S.armed then
		S.armed = true
		S.need_reset = set_mode()
		S.base = manager.machine.time:as_double()
		if watch ~= nil then
			out("# state sync: %s at %08X, mode=%s\n",
			    wr_value ~= nil and string.format("posted %02X", wr_value)
			                     or string.format("write %d", wr_count),
			    watch, normal and "normal" or "service")
		else
			out("# state sync: stop at %08X, skip %d, mode=%s\n",
			    target, skip, normal and "normal" or "service")
		end
		-- **Hard, not soft.** A soft reset re-enters the firmware but leaves the
		-- CPU's data registers holding whatever the ~17 ms before this callback
		-- put there, so a dump taken afterwards differs from a power-on machine in
		-- registers the firmware has not yet written. That is a difference in the
		-- harness which reads exactly like a difference in the emulator -- it
		-- showed up as D4 = 02E003D8 against our 0, at a point where the two
		-- machines agreed on everything else. `screencap.lua` uses a soft reset
		-- because it only needs the configuration applied; a state comparison
		-- needs the machine to start where ours starts.
		if S.need_reset then
			manager.machine:hard_reset()
			return
		end
		-- Already in the wanted mode, so nothing is reset and the run starts
		-- here. This is the path a seeded cfg directory takes, and the only one
		-- on which the debugger survives.
		out("# no reset; running from the machine as started\n")
	end

	-- Write-tap mode needs no debugger at all, which is the point of it.
	if watch ~= nil then
		-- Attempted **once**. A failed install used to leave `S.tap` nil and be
		-- retried every poll, which buried the error in thousands of copies of
		-- itself and, worse, meant the give-up check below was never reached --
		-- so a broken run looked like a slow one.
		if S.tap == nil then
			local ok, ready = pcall(install_write_tap)
			if not ok then
				-- Threw: a real fault, such as a range the space refuses.
				finish(string.format("install_write_tap failed: %s",
				                     tostring(ready)))
				return
			end
			if not ready then
				-- Not built yet. Retried silently; the give-up below bounds it,
				-- so a machine that never comes up still ends saying so.
				if now_s() >= giveup_s then
					finish("the machine never presented a program space")
				end
				return
			end
		end
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
