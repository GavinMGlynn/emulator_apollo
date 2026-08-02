-- Side-load a probe into the oracle's RAM and report what it did.
--
-- The other half of `tools/mame-oracle/encoder.py`. The encoder hand-assembles
-- instruction words; `apollo-headless --probe-file` runs them on this project's
-- core; this writes the *same* words into MAME's RAM and runs them there. The
-- two sides therefore execute one program rather than two transcriptions of it,
-- which is the only way "runs identically under both" means anything.
--
-- ## Why not the Mnemonic Debugger
--
-- MD was the original injection route and `docs/references/MD.md` records its
-- grammar and its byte-exact output. It is still the right tool for poking a
-- *running* machine. But for a probe it is the wrong one: it needs firmware, a
-- console, an autobaud and a parser, and every one of those is a way for a
-- comparison to fail for reasons that have nothing to do with the instruction
-- under test. `FINDINGS.md` C4 reached the same conclusion from the other end
-- and the plan already says to build the side-loading path first.
--
-- ## What must be true for this to work
--
-- Writes land only in RAM. `FINDINGS.md` C5 measured that a write to the boot
-- PROM's range silently does nothing and reports success, so the load address
-- must be in main memory -- `01000000` and up on a DN3500. A probe loaded low
-- would run the PROM while the harness believed it ran the probe.
--
-- Stepping needs `-debug -debugger none`: without `-debug` the `debug` field is
-- nil and `step()` cannot be called at all (C5's table).
--
-- ## Configuration
--
--   APOLLO_PROBE_WORDS  space-separated hex words, the encoder's output
--   APOLLO_PROBE_LOAD   where to write them (hex, must be RAM)
--   APOLLO_PROBE_ENTRY  where to start (hex, defaults to the load address)
--   APOLLO_PROBE_STACK  initial A7 (hex)
--   APOLLO_PROBE_READ   an address to report as a long word afterwards (hex)
--   APOLLO_PROBE_LIMIT  maximum instructions to step (default 64)
--   APOLLO_PROBE_AT     emulated seconds to wait before loading (default 3.0),
--                       so the firmware has finished touching memory

local function env(name, default)
	local value = os.getenv(name)
	if value == nil or value == "" then return default end
	return value
end

local function hex(name, default)
	local value = env(name, nil)
	if value == nil then return default end
	return tonumber(value, 16)
end

local words_text = env("APOLLO_PROBE_WORDS", "")
local load_at    = hex("APOLLO_PROBE_LOAD", 0x01001000)
local entry_at   = hex("APOLLO_PROBE_ENTRY", load_at)
local stack_at   = hex("APOLLO_PROBE_STACK", 0x01002000)
local read_at    = hex("APOLLO_PROBE_READ", nil)
local limit      = tonumber(env("APOLLO_PROBE_LIMIT", "64"))
local at_seconds = tonumber(env("APOLLO_PROBE_AT", "3.0"))

local words = {}
for token in words_text:gmatch("%S+") do
	words[#words + 1] = tonumber(token, 16)
end

-- Stepping is **asynchronous**: `cpu.debug:step()` schedules the step and the
-- scheduler runs it after this callback returns, so the result must be read on
-- the *next* callback. `steptime.lua` is built the same way, and reading the PC
-- straight after `step()` gives the value from before it -- which looks exactly
-- like an instruction that did not execute.
local phase = "wait"
local executed = 0
local last_pc = nil
local stalled = 0
local steps = 0
local reported = false

local function out(fmt, ...)
	io.write(string.format(fmt, ...))
	io.flush()
end

local function state_value(cpu, name)
	local entry = cpu.state[name]
	if entry == nil then
		out("# no register %q. present:", name)
		for key, _ in pairs(cpu.state) do out(" %s", key) end
		out("\n")
		return nil
	end
	return entry.value
end

local function report(cpu, space, why)
	if reported then return end
	reported = true
	out("words     %d\n", #words)
	out("ran       %d\n", executed)
	out("stopped   %s\n", why)
	out("d0        %08X\n", state_value(cpu, "D0") or 0)
	out("pc        %08X\n", state_value(cpu, "PC") or 0)
	if read_at ~= nil then
		out("read      %08X %08X\n", read_at, space:read_u32(read_at))
	end
	manager.machine:exit()
end

emu.register_periodic(function()
	local cpu = manager.machine.devices[":maincpu"]
	local space = cpu.spaces["program"]

	if phase == "wait" then
		if manager.machine.time.seconds < at_seconds then return end
		if #words == 0 then
			out("# no probe words given\n"); manager.machine:exit(); return
		end

		for i, word in ipairs(words) do
			space:write_u16(load_at + (i - 1) * 2, word)
		end
		local verified = true
		for i, word in ipairs(words) do
			if space:read_u16(load_at + (i - 1) * 2) ~= word then verified = false end
		end
		out("# loaded %d words at %08X, readback %s\n", #words, load_at,
		    verified and "ok" or "MISMATCH -- not RAM?")
		if not verified then manager.machine:exit(); return end

		if cpu.debug == nil then
			out("# no cpu.debug: rerun with -debug -debugger none\n")
			manager.machine:exit(); return
		end

		cpu.state["SP"].value = stack_at
		cpu.state["PC"].value = entry_at
		last_pc = entry_at
		phase = "step"
		cpu.debug:step()
		return
	end

	if phase == "step" then
		local pc = state_value(cpu, "PC")
		if pc == nil then report(cpu, space, "no-pc"); return end

		-- An instruction is counted when the PC *moves*. The settling step
		-- after the take-over and the stall steps that detect the halt are the
		-- harness's, not the program's, and counting them made this side report
		-- 6 where the program is 3 instructions long.
		if pc ~= last_pc then executed = executed + 1 end

		-- A STOP does not advance past itself, so a PC that has not moved is
		-- how a stopped machine looks from outside without asking the debugger
		-- for a halt reason.
		-- Two consecutive unchanged PCs, not one. The first step after the
		-- take-over can leave the PC where it was put -- the debugger completes
		-- whatever the CPU had already begun -- and treating that as a halt
		-- reports a probe that never ran.
		steps = steps + 1
		if pc == last_pc then
			stalled = stalled + 1
			if stalled >= 2 then report(cpu, space, "STOPPED"); return end
		else
			stalled = 0
		end
		last_pc = pc
		if steps >= limit then report(cpu, space, "LIMIT"); return end
		cpu.debug:step()
		return
	end
end)
