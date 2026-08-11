-- Stop the oracle at a chosen program counter and dump its whole state there.
--
-- This is the oracle half of the state differential. Ours is
-- `apollo-headless --boot-stop-pc ADDR --boot-stop-pc-skip N --dump-state FILE`;
-- this produces the file to compare it with, at the same point in the same
-- program. `tools/state-diff.py` does the comparing, through
-- `tools/mame-oracle/state-map.txt`.
--
-- ## Why a breakpoint and not a tap
--
-- Five earlier attempts to synchronise the two machines used memory taps, and
-- all five failed for the same reason: `install_read_tap` sees **bus reads**,
-- and cannot tell an instruction fetch from a data read of the same address.
-- Gating a tap on the PC then fails the other way -- by the time the tap fires
-- the PC has moved, so an exact comparison never matches and a windowed one
-- matches the wrong things. A tap is the wrong instrument for "stop here".
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
-- ## Configuration
--
--   APOLLO_SYNC_PC     hex address to stop at, e.g. "3C43DD80" (required)
--   APOLLO_SYNC_SKIP   ignore this many hits first (default 0), for an address
--                      inside a loop or a routine called more than once
--   APOLLO_SYNC_DUMP   where to write the state dump (default "oracle.state")
--   APOLLO_SYNC_MODE   "normal" (default) or "service", as `screencap.lua`
--   APOLLO_SYNC_GIVEUP emulated seconds after which to give up and say so
--                      (default 120). A run that never reaches the address must
--                      end saying that, not sit there looking busy.

local pc_text  = os.getenv("APOLLO_SYNC_PC")
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

if pc_text == nil then
	out("# APOLLO_SYNC_PC is not set; nothing to stop at\n")
	return
end
local target = tonumber(pc_text, 16)
if target == nil then
	out("# APOLLO_SYNC_PC is not hexadecimal: %s\n", pc_text)
	return
end

emu.register_periodic(function()
	if S.done then return end

	if not S.armed then
		S.armed = true
		set_mode()
		S.base = manager.machine.time:as_double()
		out("# state sync: stop at %08X, skip %d, mode=%s\n",
		    target, skip, normal and "normal" or "service")
		manager.machine:soft_reset()
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
