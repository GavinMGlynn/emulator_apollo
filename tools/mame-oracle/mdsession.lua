-- Hold a long interactive session with the boot PROM's Mnemonic Debugger.
--
-- Distinct from `mdcapture.lua`, which taps every serial register write to
-- produce a byte-exact register trace. This script deliberately taps *nothing*,
-- because the two jobs conflict: an expect-style driver has to read the console
-- stream, `apollo_stdio_device::rcv_complete` puts that stream on the process's
-- stdout one raw character at a time, and a register trace interleaved into it
-- cannot be parsed back out -- raw console bytes carry no framing to split on.
--
-- So the division is: `mdcapture.lua` when the question is *what bytes moved*,
-- this when the question is *what the machine says and what to send it next*.
--
-- Everything this script prints itself goes to **stderr**, for the same reason.
-- stdout belongs to the machine.
--
-- What it does is the two things a session cannot start without:
--
--   1. Sets every `apollo_config` field explicitly. The install procedure
--      records these as Machine Configuration menu settings (`FINDINGS.md`
--      C47), and a headless run has no menu. Set from this file rather than
--      from a cfg the last run wrote, so a session does not depend on state
--      left by a previous one.
--   2. Presses and releases one key, which is what prompts the firmware's
--      autobaud (`FINDINGS.md` C45). The DUART's transmitter is not enabled
--      until the firmware has found a rate, so a machine nobody touches sits
--      silent however long it is watched.
--
-- Settings are chosen by **name**, against the machine's own settings table,
-- rather than by writing the constants from `apollo.h`. MAME's `user_value` is
-- a masked field value and not an index -- `APOLLO_CONF_25_YEARS_AGO` is
-- `0x0080`, not `1` -- so a script assigning `1` would silently select the
-- wrong setting on every field whose mask is not bit 0. Naming the setting and
-- reading the name back also makes the log a record of what the machine did
-- rather than of what it was asked to do.
--
-- Loaded as `-autoboot_script`, and -- unlike every other script in this
-- directory -- it does **not** need `-debug -debugger none`. C5's table is
-- specific about what that pair buys: `cpu.debug` and therefore `step()`.
-- Nothing here steps. Ports, fields and images are bound in a plain run, so a
-- session that asked for the debugger would be paying for a facility it never
-- calls, on a run that lasts for a whole install.
--
-- ## Configuration
--
--   APOLLO_MD_POST     key to press once, by its PORT_NAME, to prompt the
--                      autobaud (default "Numpad Enter")
--   APOLLO_MD_POST_AT  emulated seconds to wait before pressing (default 4.0)
--   APOLLO_MD_HOLD     emulated seconds to hold it down (default 0.2)
--   APOLLO_MD_DISPLAY  Graphics Controller setting name. **Unset means this
--                      script leaves the setting alone**, so the machine keeps
--                      MAME's own default for the driver -- `dn3500` is
--                      `dn3500_15i`, a 15-inch monochrome. This line used to
--                      say the default was "8-Plane Color", which the code
--                      below does not do: it only writes `CONFIG` when the
--                      variable is set. A run's own header prints which it
--                      got ("left at its default"), and that print is the
--                      thing to believe
--   APOLLO_MD_UNTIL    emulated seconds after which to exit. Default 0, meaning
--                      never -- an install runs for as long as it runs, and the
--                      driver, which can see the console, is what ends it.
--   APOLLO_MD_ACTIVITY seconds of emulated time between activity reports, or 0
--                      for none (the default). A stage like `ex invol` can run
--                      for ten minutes without printing anything, because the
--                      cartridge is genuinely that slow, and a silent console
--                      cannot distinguish that from a machine going nowhere.
--                      This reports the program counter and counts writes to
--                      the tape and disk controllers, which can.
--   APOLLO_MD_ACTIVITY_READS
--                      "1" to count reads as well. Off by default because it
--                      costs about 30% of the run's speed: a cartridge read
--                      spins on a status register 65 million times an emulated
--                      minute, and each one becomes a Lua call.
--   APOLLO_MD_SWAPFILE a file the driver writes to ask for a media change, and
--                      beside which this writes `<name>.ack` when it is done.
--                      `MINST` takes four cartridges in turn, so an install
--                      that cannot change one is an install that stops after
--                      the first.
--   APOLLO_MD_SWAPEVERY
--                      emulated seconds between checks of that file
--                      (default 0.25)

-- The install procedure's Machine Configuration, as `FINDINGS.md` C47 records
-- it: "25 Years Ago" on, everything else off.
--
-- "Normal/Service" is not one of the "everything else": its two settings are
-- Normal and Service, neither of which is Off, and Normal is what an install
-- wants -- Service mode is for the PROM's own diagnostics. Worth stating
-- because its *default* is Service, so leaving it alone is a choice too.
--
-- "Graphics Controller" likewise has no Off. A DN3500 always has a display
-- (`mdcapture.lua`'s header records the search that established it), so the
-- only question is which, and the default is what the wiki's procedure ran.
local CONFIG = {
	["Normal/Service"]    = "Normal",
	["German Keyboard"]   = "Off",
	["30 Years Ago ..."]  = "Off",
	["25 Years Ago ..."]  = "On",
	["Node ID from Disk"] = "Off",
	["Trap Trace"]        = "Off",
	["FPU Trace"]         = "Off",
	["Disk Trace"]        = "Off",
	["Network Trace"]     = "Off",
}

-- `none` presses nothing. A key press interrupts the boot, so a session that
-- wants to watch the machine come up on its own must be able to say so; every
-- other caller gets the default and is unaffected.
local post_text = os.getenv("APOLLO_MD_POST") or "Numpad Enter"
if post_text == "none" then
	post_text = nil
end
local post_at_s = tonumber(os.getenv("APOLLO_MD_POST_AT") or "") or 4.0
local hold_s    = tonumber(os.getenv("APOLLO_MD_HOLD") or "") or 0.2
local until_s   = tonumber(os.getenv("APOLLO_MD_UNTIL") or "") or 0.0

local display   = os.getenv("APOLLO_MD_DISPLAY")
if display ~= nil and display ~= "" then
	CONFIG["Graphics Controller"] = display
end

local activity_s = tonumber(os.getenv("APOLLO_MD_ACTIVITY") or "") or 0.0
local count_reads = (os.getenv("APOLLO_MD_ACTIVITY_READS") or "") == "1"

local posted    = false
local released  = false
local installed = false
local finished  = false

-- The two controllers a long stage waits on, from `008778-03` Table 2-9 and
-- this project's own measurements of both placements: the Archive SC-499
-- cartridge tape at `050000` and the OMTI 8621 fixed disk at `04D000`. Read and
-- write are counted apart, because they answer different questions -- a stage
-- that only ever reads the tape is loading, and one that writes the disk is
-- doing the thing the install exists for.
local WATCH = {
	{ name = "tape", base = 0x050000, last = 0x050FFF, reads = 0, writes = 0 },
	{ name = "disk", base = 0x04D000, last = 0x04DFFF, reads = 0, writes = 0 },
}
local activity_taps = {}
local reported_at   = 0.0

local swapfile   = os.getenv("APOLLO_MD_SWAPFILE")
local swap_every = tonumber(os.getenv("APOLLO_MD_SWAPEVERY") or "") or 0.25
local swap_seen  = -1
local swap_at    = 0.0

-- stderr, always: stdout is the machine's console and nothing else may be on
-- it. A note that lands in the console stream is indistinguishable from output
-- the firmware produced, which is precisely the confusion this file exists to
-- avoid.
local function note(fmt, ...)
	io.stderr:write(string.format(fmt, ...))
	io.stderr:flush()
end

-- Set one configuration field by the *name* of the setting wanted.
--
-- Reports what it found on failure rather than only that it failed: a field
-- name that has moved and a setting name that has moved need different fixes,
-- and "not found" alone cannot tell them apart.
local function set_setting(field, field_name, wanted)
	for value, name in pairs(field.settings) do
		if name == wanted then
			field.user_value = value
			note("# %s = %s (0x%04X)\n", field_name, name, value)
			return true
		end
	end
	note("# %s: no setting named %q. settings present:\n", field_name, wanted)
	for value, name in pairs(field.settings) do
		note("#   0x%04X %s\n", value, name)
	end
	return false
end

local function configure()
	local port = manager.machine.ioport.ports[":apollo_config"]
	if port == nil then
		note("# no :apollo_config port. ports present:\n")
		for tag, _ in pairs(manager.machine.ioport.ports) do
			note("#   %s\n", tag)
		end
		return
	end

	-- Driven from the machine's fields rather than from CONFIG's keys, so a
	-- field this table does not name is *reported* rather than silently left at
	-- whatever the default is. A configuration that is only half stated is one
	-- whose other half moves when MAME's does.
	for name, field in pairs(port.fields) do
		local wanted = CONFIG[name]
		if wanted ~= nil then
			set_setting(field, name, wanted)
		else
			note("# %s: not set by this script, left at its default\n", name)
		end
	end
end

-- Press a key by its `PORT_NAME`, across whichever of the keyboard's ports
-- carries it. Directly, rather than through MAME's natural keyboard:
-- `apollo_kbd.cpp` defines no `PORT_CHAR` entries at all, so the translation
-- layer that turns a character into a key press has nothing to work with and
-- silently does nothing (`FINDINGS.md` C40).
local held_field = nil

local function press_key(name)
	for tag, port in pairs(manager.machine.ioport.ports) do
		if tag:find("kbd") then
			for field_name, field in pairs(port.fields) do
				if field_name == name then
					held_field = field
					field:set_value(1)
					note("# pressed %q on %s at %.1fs\n", name, tag,
					     manager.machine.time.seconds)
					return
				end
			end
		end
	end
	note("# key %q not found; keyboard fields present:\n", name)
	for tag, port in pairs(manager.machine.ioport.ports) do
		if tag:find("kbd") then
			for field_name, _ in pairs(port.fields) do
				note("#   %s %s\n", tag, field_name)
			end
		end
	end
end

local function release_key()
	if held_field ~= nil then
		held_field:set_value(0)
		note("# released at %.1fs\n", manager.machine.time.seconds)
		held_field = nil
	end
end

-- Counters only, never a per-access line. A controller polled by a driver
-- produces thousands of accesses a second, so a trace would bury the console
-- and slow the run it is measuring; a count answers the only question being
-- asked, which is whether the number is moving.
--
-- **Reads are off by default, and that is measured rather than cautious.** A
-- cartridge read spins on the status register: 65 million reads of `050000` per
-- emulated minute, against nine thousand writes. One Lua callback each costs
-- about 30% of the run's speed -- 0.55x against the 0.78x the same machine
-- manages untapped -- which on a ten-minute stage is four minutes of nothing,
-- added by the thing watching for nothing happening.
--
-- What is left is enough. Writes are rare and are the *interesting* half
-- anyway: a stage writing the disk is doing the work, where reads only say the
-- bus is busy. And the program counter, sampled at report time, costs one read
-- per report and distinguishes a machine looping in a driver from one stopped
-- at a single address -- which is the actual question.
local function watch_activity()
	local space = manager.machine.devices[":maincpu"].spaces["program"]
	for _, region in ipairs(WATCH) do
		if count_reads then
			activity_taps[#activity_taps + 1] = space:install_read_tap(
				region.base, region.last, region.name .. "_r",
				function(offset, data, mask)
					region.reads = region.reads + 1
					return data
				end)
		end
		activity_taps[#activity_taps + 1] = space:install_write_tap(
			region.base, region.last, region.name .. "_w",
			function(offset, data, mask)
				region.writes = region.writes + 1
				-- Unchanged: an instrument that altered the data would change
				-- what it measured.
				return data
			end)
	end
	note("# activity reported every %.1f emulated seconds%s\n", activity_s,
	     count_reads and " (counting reads: expect ~30% slower)" or "")
end

local function report_activity()
	local pc = manager.machine.devices[":maincpu"].state["PC"].value
	local line = string.format("# %8.1fs  PC %08X", manager.machine.time.seconds,
	                           pc)
	for _, region in ipairs(WATCH) do
		if count_reads then
			line = line .. string.format("  %s %d r / %d w",
			                             region.name, region.reads,
			                             region.writes)
		else
			line = line .. string.format("  %s %d w", region.name,
			                             region.writes)
		end
	end
	note("%s\n", line)
end

-- Change a mounted medium while the machine runs.
--
-- This is safe on the cartridge tape for a stated reason rather than because it
-- appeared to work: `sc499_ctape_image_device` derives from
-- `microtape_image_device` and so from `magtape_image_device`, whose
-- `is_reset_on_load()` returns **false**. A device that reset on load would
-- throw away the running install at the moment it asked for the next tape,
-- which is the worst possible time.
--
-- The protocol is two files, because the driver and this script share nothing
-- else -- MAME's stdout belongs to the console and its stderr is not readable
-- by the driver. The request carries a sequence number so that the same path
-- can be asked for twice, and the acknowledgement carries the same number back
-- so the driver waits for *its* swap rather than the previous one.
local function do_swap(sequence, name, path)
	local target = nil
	for _, image in pairs(manager.machine.images) do
		if image.instance_name == name then
			target = image
		end
	end

	local status
	if target == nil then
		local present = {}
		for _, image in pairs(manager.machine.images) do
			present[#present + 1] = image.instance_name
		end
		status = "no image named " .. name .. "; present: " ..
		         table.concat(present, " ")
	else
		target:unload()
		if path == "" then
			status = "ok (unloaded)"
		else
			-- `load` returns nil on success and a message otherwise. Reported
			-- rather than swallowed: a tape that failed to mount looks exactly
			-- like a tape that mounted and holds nothing the installer wants.
			local failure = target:load(path)
			if failure == nil then
				status = "ok"
			else
				status = "load failed: " .. tostring(failure)
			end
		end
	end

	note("# swap %d %s %s -> %s\n", sequence, name, path, status)

	local ack = io.open(swapfile .. ".ack", "w")
	if ack ~= nil then
		ack:write(string.format("%d\n%s\n", sequence, status))
		ack:close()
	end
end

local function poll_swap()
	local handle = io.open(swapfile, "r")
	if handle == nil then
		return
	end
	local sequence = handle:read("l")
	local name     = handle:read("l")
	local path     = handle:read("l")
	handle:close()

	if sequence == nil or name == nil then
		return
	end
	sequence = tonumber(sequence)
	if sequence == nil or sequence <= swap_seen then
		return
	end
	swap_seen = sequence
	do_swap(sequence, name, path or "")
end

local function install()
	if installed or finished then
		return
	end
	installed = true

	configure()

	-- **Setting the configuration is not enough, and this file did not do the
	-- other half.** `:apollo_config` is read at `MACHINE_RESET`, so a value
	-- written from a periodic callback arrives *after* the machine has already
	-- reset with the old one. The oracle **ships in Service** --
	-- `PORT_CONFSETTING(0x00, "Service")` is the default -- so every run this
	-- harness has ever made was a service-mode machine, whatever the
	-- `Normal/Service = Normal` line above claimed. That is why `--stage watch`
	-- parked in the boot PROM's console-selection poll at `000794` and a single
	-- key press dropped it into the Mnemonic Debugger at `00267E`: both are
	-- service-mode behaviour, and neither is the auto-boot the stage's comment
	-- describes. `screencap.lua` had this right and said so; this file has
	-- carried the bug beside it.
	--
	-- The guard lives in `_G` because the reset re-runs this script with fresh
	-- locals, and a local guard resets the machine for ever.
	if _G.apollo_mdsession_configured == nil then
		_G.apollo_mdsession_configured = true
		note("# configuration applied; soft reset so the firmware runs with it\n")
		manager.machine:soft_reset()
		return
	end

	-- Which devices the machine actually has. A build flag confirmed only by
	-- the absence of compile errors is not confirmation that the device it
	-- guards was instantiated, and those two look identical from outside --
	-- `APOLLO_XXL` guards the stdio terminal this whole session speaks through.
	for tag, _ in pairs(manager.machine.devices) do
		if tag:find("stdio") or tag:find("kbd") then
			note("# device %s\n", tag)
		end
	end

	-- Which media are mounted, by filename. The install is a sequence of tape
	-- swaps, so a stage that reads the wrong cartridge is a live failure mode,
	-- and its symptom -- a utility that is simply not found -- looks nothing
	-- like a mounting problem.
	for _, image in pairs(manager.machine.images) do
		note("# image %s: %s\n", image.instance_name,
		     image.filename or "(empty)")
	end

	if activity_s > 0 then
		watch_activity()
	end

	-- Named at startup, because "the swap never happened" has two very
	-- different causes -- the channel was never armed, or it was armed and the
	-- request was not seen -- and they look identical from the driver's side.
	note("# swap channel: %s\n", swapfile or "(none)")

	note("# apollo md session ready\n")
end


-- ## Root-pointer watch, opt-in with APOLLO_CRP_LOG
--
-- Our core executes exactly one `PMOVE ,CRP` in the whole Domain/OS kernel
-- image and its gate is never false, so the machine runs on `SELF_TEST`'s tree
-- and dies on an address that tree never mapped. The question this answers is
-- whether the oracle installs `0105BC00` and where.
--
-- It **polls CPU state** rather than hooking an instruction path. MAME exposes
-- the 68030's root pointers as ordinary state registers (`m68kcpu.cpp`,
-- `state_add(M68K_CRP_APTR, ...)`), so if the register changes the value
-- changes, whatever instruction changed it. An earlier attempt side-loaded a
-- probe into the oracle's `PMOVE` path, was verified present in the binary, ran
-- three boots and never fired -- and nothing could tell "the oracle does not do
-- this" from "the probe is not wired". This cannot fail that way, and it says
-- so out loud: it checks the registers exist before the run and reports
-- `SELF_TEST`'s own loads, which are known to happen, so a quiet log is
-- evidence rather than an absence of evidence.
local crp_log = os.getenv("APOLLO_CRP_LOG")
local crp_file, crp_last, crp_changes = nil, {}, 0
if crp_log ~= nil and crp_log ~= "" then
	crp_file = io.open(crp_log, "w")
	local cpu = manager.machine.devices[":maincpu"]
	local missing = {}
	for _, n in ipairs({ "CRP_APTR", "CRP_LIMIT", "SRP_APTR", "SRP_LIMIT", "PC" }) do
		local ok = pcall(function() return cpu.state[n].value end)
		if not ok then missing[#missing + 1] = n end
	end
	if #missing > 0 then
		crp_file:write("FATAL missing CPU state: " .. table.concat(missing, " ") ..
		               "\nFATAL this run proves nothing\n")
		crp_file:flush()
		crp_file = nil
	else
		crp_file:write("# time_s  register  value  pc\n")
		crp_file:flush()
	end
end

-- ## Address-space-cache tap, opt-in with APOLLO_ASID_TAP=<hex physical address>
--
-- The root-pointer poll above says *what* the oracle installs and roughly when.
-- What it cannot say is what the kernel was **asked** for, and that is now the
-- question: our machine's only root-pointer install is skipped because the space
-- requested equals the one its cache says is current, so the oracle must be
-- asked for a different space first.
--
-- The kernel records the space it is switching to eight instructions before the
-- `PMOVE`, with `MOVE.W D0,$3C43FB14`. Every successful switch therefore writes
-- that word, and a write tap on it yields the sequence of requests directly --
-- without hooking an instruction path, which is the thing that failed before.
--
-- The address is passed in rather than hardcoded because it is *physical*: MAME
-- taps the CPU's address space, and the kernel's own logical `3C43FB14` resolves
-- through the MMU. On this image it lands at `01042714`.
--
-- **Two modes, and the second exists because the first needs an address we got
-- wrong.** `APOLLO_ASID_TAP=<hex>` taps that physical address. `APOLLO_ASID_PC=
-- <hex logical>` instead taps a wide range and keeps only writes *made by that
-- instruction*, which needs no translation at all: the PC is the same logical
-- number on both machines, and where the data page landed stops mattering. The
-- first mode caught nothing in a run that installed the root pointer ten times,
-- because the address had been read off a `--dump-logical` window header and
-- offset into -- and only a window's first address has been through the MMU.
local asid_tap_at = tonumber(os.getenv("APOLLO_ASID_TAP") or "", 16)
local asid_pc = tonumber(os.getenv("APOLLO_ASID_PC") or "", 16)
local asid_pc_hi = tonumber(os.getenv("APOLLO_ASID_PC_HI") or "", 16)
local asid_lo = tonumber(os.getenv("APOLLO_ASID_LO") or "", 16) or 0x01000000
local asid_hi = tonumber(os.getenv("APOLLO_ASID_HI") or "", 16) or 0x010FFFFF
local asid_file = nil
-- **Held in `_G`, not in a local.** `install_read_tap`/`install_write_tap` return
-- a handler object whose *lifetime is the tap*: when Lua collects it the tap is
-- removed. A local in this chunk is collectable, and the symptom is a tap that
-- logs a handful of accesses in the first timeslice and never fires again -- which
-- is exactly what three measurements here reported as "caught nothing", each of
-- them a dead instrument rather than a quiet machine. `screencap.lua` says the
-- same thing about its own guard: it lives in `_G` or it does not survive.
_G.apollo_asid_tap = _G.apollo_asid_tap or nil
local asid_seen = 0
if asid_pc ~= nil then
	asid_pc_hi = asid_pc_hi or (asid_pc + 10)
	asid_file = io.open((os.getenv("APOLLO_CRP_LOG") or "crpwatch.log") .. ".asid", "w")
	local ok, err = pcall(function()
		local sp = manager.machine.devices[":maincpu"].spaces["program"]
		local cpu = manager.machine.devices[":maincpu"]
		_G.apollo_asid_tap = sp:install_write_tap(asid_lo, asid_hi, "asid-by-pc",
			function(offset, data, mask)
				-- A small window, not equality: during a write MAME's PC may
				-- already have advanced past the storing instruction.
				local pc = cpu.state["PC"].value
				-- **Prove the instrument fires.** The first writes are logged
				-- whatever their PC, so a run that reports no match is telling
				-- us about the machine and not about itself. Two earlier taps
				-- produced a plausible silence and cost a run each.
				if asid_seen < 12 then
					asid_seen = asid_seen + 1
					asid_file:write(string.format("%10.4f  sample %08X = %08X  pc %08X\n",
						manager.machine.time:as_double(), offset, data, pc))
					asid_file:flush()
				end
				-- A *range*, because which instruction in the routine does the
				-- recording is not reliably known: the end-of-boot dump this
				-- was read from disagrees with the executed trace at
				-- 3C43DD88, the page having been reused by then. Matching the
				-- whole routine does not depend on that reading.
				if pc >= asid_pc and pc <= asid_pc_hi then
					asid_file:write(string.format("%10.4f  write %08X = %08X  pc %08X\n",
						manager.machine.time:as_double(), offset, data, pc))
					asid_file:flush()
				end
				return data
			end)
	end)
	if not ok then
		asid_file:write("FATAL could not install the by-PC tap: " .. tostring(err) ..
		                "\nFATAL this run proves nothing about the requests\n")
	else
		asid_file:write(string.format("# by-PC tap: writes from pc %08X over %08X-%08X\n",
			asid_pc, asid_lo, asid_hi))
	end
	asid_file:flush()
elseif asid_tap_at ~= nil then
	asid_file = io.open((os.getenv("APOLLO_CRP_LOG") or "crpwatch.log") .. ".asid", "w")
	local ok, err = pcall(function()
		local sp = manager.machine.devices[":maincpu"].spaces["program"]
		-- The tap is on the CPU's 32-bit space, so the range must cover whole
		-- long words: MAME refuses `x..x+1` with "end address has low bits
		-- unset", which is a good error and was reported rather than swallowed.
		local lo = asid_tap_at & ~3
		_G.apollo_asid_tap = sp:install_write_tap(lo, lo + 3,
			"asid-cache",
			function(offset, data, mask)
				asid_file:write(string.format("%10.4f  write %08X = %04X  pc %08X\n",
					manager.machine.time:as_double(), offset, data & 0xFFFF,
					manager.machine.devices[":maincpu"].state["PC"].value))
				asid_file:flush()
				return data
			end)
	end)
	if not ok then
		asid_file:write("FATAL could not install the tap: " .. tostring(err) ..
		                "\nFATAL this run proves nothing about the requests\n")
		asid_file:flush()
	else
		asid_file:write(string.format("# write tap on %08X installed\n", asid_tap_at))
		asid_file:flush()
	end
end

-- ## The address-space switch hook, opt-in with APOLLO_SWITCH_PC=<hex logical>
--
-- The question every other instrument here has failed to answer: **what address
-- space does the oracle ask for, and when?** The root-pointer poll says what was
-- installed and locates the instruction only to within a few hundred bytes; the
-- write taps cannot see a `PMOVE`, which writes a register and no memory.
--
-- This taps **reads** over RAM and keeps only those whose PC lies in the switch
-- routine -- an instruction fetch from inside it -- then reads the argument off
-- the stack with `readv_u16`, which translates through the MMU as the program
-- would. So it needs no physical address, which matters because the two
-- machines page the kernel into different frames.
--
-- It logs its first fires whatever their PC, for the reason every instrument in
-- this file now does: an instrument that has not been seen to fire cannot make
-- its silence mean anything, and three before it produced a plausible nothing.
local switch_pc = tonumber(os.getenv("APOLLO_SWITCH_PC") or "", 16)
local switch_file, switch_hits = nil, 0
_G.apollo_switch_tap = _G.apollo_switch_tap or nil
local switch_next_sample = 0.0
if switch_pc ~= nil then
	switch_file = io.open((os.getenv("APOLLO_CRP_LOG") or "crpwatch.log") .. ".switch", "w")
	local ok, err = pcall(function()
		local cpu = manager.machine.devices[":maincpu"]
		local sp = cpu.spaces["program"]
		_G.apollo_switch_tap = sp:install_read_tap(asid_lo, asid_hi, "switch-entry",
			function(offset, data, mask)
				local pc = cpu.state["PC"].value
				-- **Proof of life, sampled across the run rather than at its
				-- start.** A counter that fills in the first millisecond proves
				-- the tap fires in the *firmware's* regime and says nothing
				-- about the kernel's, which is where the question lives. One
				-- read logged every two emulated seconds makes a gap visible.
				local now = manager.machine.time:as_double()
				if now >= switch_next_sample then
					switch_next_sample = now + 2.0
					switch_file:write(string.format("%10.4f  alive: read %08X  pc %08X\n",
						now, offset, pc))
					switch_file:flush()
				end
				-- **The operand fetch of a root-pointer load.** `PMOVE (A0),CRP`
				-- reads its 64-bit operand from memory before writing the
				-- register, so the value passes through this tap and its PC is
				-- the installing instruction -- which is the one thing the
				-- root-pointer poll cannot give, locating only to a few hundred
				-- bytes. Matched on the value rather than the address, because
				-- the descriptor's address is not known on this machine.
				if (data == 0x01001400 or data == 0x0105BC00 or data == 0x00FF0002)
				   and switch_hits < 60 then
					switch_hits = switch_hits + 1
					switch_file:write(string.format(
						"%10.4f  ROOTVALUE %08X at %08X  pc %08X\n",
						manager.machine.time:as_double(), data, offset, pc))
					switch_file:flush()
				end
				if pc >= switch_pc and pc <= switch_pc + 0x80 and switch_hits < 60 then
					switch_hits = switch_hits + 1
					local a7 = cpu.state["A7"].value
					local arg = -1
					local got = pcall(function() arg = sp:readv_u16(a7 + 4) end)
					switch_file:write(string.format(
						"%10.4f  ENTRY pc %08X  a7 %08X  arg %s\n",
						manager.machine.time:as_double(), pc, a7,
						got and string.format("%04X", arg) or "unreadable"))
					switch_file:flush()
				end
				return data
			end)
	end)
	if not ok then
		switch_file:write("FATAL could not install the switch tap: " .. tostring(err) ..
		                  "\nFATAL this run proves nothing about the requests\n")
	else
		switch_file:write(string.format("# switch tap: reads with pc in %08X-%08X\n",
			switch_pc, switch_pc + 0x80))
	end
	switch_file:flush()
end

-- ## Display-memory tap, opt-in with APOLLO_VRAM_TAP=<hex address>
--
-- The boot PROM runs a display memory test that this core's monochrome path
-- fails and the oracle passes, at `006B9A`/`006BBE`. Both machines run the same
-- PROM over the same words, so the comparison that settles it is **what each
-- reads at the same address under the same instruction** -- not another theory
-- about the model.
--
-- A *narrow* tap, unlike the switch tap that made a run unusable: eight bytes
-- rather than all of RAM, so the callback fires on the handful of accesses that
-- matter instead of on every fetch. It logs the first accesses with their PC
-- and value, which is exactly what `--dump-mem` gives on our side.
local vram_tap_at = tonumber(os.getenv("APOLLO_VRAM_TAP") or "", 16)
local vram_file, vram_hits = nil, 0
_G.apollo_vram_tap = _G.apollo_vram_tap or nil
if vram_tap_at ~= nil then
	vram_file = io.open((os.getenv("APOLLO_CRP_LOG") or "crpwatch.log") .. ".vram", "w")
	local ok, err = pcall(function()
		local cpu = manager.machine.devices[":maincpu"]
		local sp = cpu.spaces["program"]
		_G.apollo_vram_tap = sp:install_read_tap(vram_tap_at, vram_tap_at + 7,
			"vram", function(offset, data, mask)
				if vram_hits >= 40 then return end
				vram_hits = vram_hits + 1
				vram_file:write(string.format("%10.4f  read %08X = %04X  pc %08X\n",
					manager.machine.time:as_double(), offset, data,
					cpu.state["PC"].value))
				vram_file:flush()
			end)
	end)
	vram_file:write(ok and "# vram tap installed\n"
	                   or ("# FATAL tap failed: " .. tostring(err) .. "\n"))
	vram_file:flush()
end

-- ## Full-state dump, opt-in with APOLLO_STATE_DUMP=<path>
--
-- The oracle half of the emulator/oracle differential. `machine:apollo_dump_state`
-- walks `save_manager`'s own registry -- the same `m_entry_list` the serialiser
-- uses -- so a field MAME saves is a field this prints, with the name MAME
-- registered it under. Requires the temporary instrumentation in `ext/mame`.
--
-- `APOLLO_STATE_DUMP_AT` is the emulated second to dump at, so both machines can
-- be stopped at a comparable instant rather than at whatever moment a run ends.
local state_dump_path = os.getenv("APOLLO_STATE_DUMP")
local state_dump_at = tonumber(os.getenv("APOLLO_STATE_DUMP_AT") or "") or 0.0
-- **The sync point that actually works.** Emulated seconds are the same unit on
-- both machines but not the same *place*: two cores agree on a second only if
-- they execute identically, which is the thing under test. A shared **event** is
-- what makes two dumps comparable, and the cheapest one is the fetch of a chosen
-- instruction -- our side stops with `--boot-stop-pc`, this side taps the four
-- bytes that instruction is read from. Narrow, so it costs nothing; the earlier
-- whole-RAM tap made a run unusable.
local state_dump_fetch = tonumber(os.getenv("APOLLO_STATE_DUMP_ON_FETCH") or "", 16)
local state_dumped = false

-- ## Answering a prompt on the keyboard, opt-in with APOLLO_MD_ANSWER
--
-- The oracle's console is its *display*, so a run that sends nothing sits at
-- whatever the firmware last asked. `--stage watch` reaches
--
--     DO YOU WISH TO CONTINUE (Y,N)? _
--
-- and stops there for ever -- which is why a tap on Domain/OS's entry never
-- fired: the machine never loads it. This core's own boot answers the same
-- question with `y` over the serial console (`tools/boot-domainos.script`);
-- there is nobody typing here.
--
-- A comma-separated list of `PORT_NAME`s, pressed in order once the emulated
-- clock passes `APOLLO_MD_ANSWER_AT`. Note the *name*: `apollo_kbd.cpp` gives
-- RETURN no `PORT_NAME` at all (line 77 is `PORT_CODE(KEYCODE_ENTER)` and
-- nothing else), so it cannot be pressed this way -- use `Numpad Enter`, which
-- is named and is what this harness already presses for the autobaud.
local answer_keys = {}
for k in string.gmatch(os.getenv("APOLLO_MD_ANSWER") or "", "([^,]+)") do
	answer_keys[#answer_keys + 1] = k
end
local answer_at = tonumber(os.getenv("APOLLO_MD_ANSWER_AT") or "") or 0.0
local answer_next, answer_down_at = 1, nil

local function answer_poll()
	if #answer_keys == 0 or answer_next > #answer_keys then return end
	local now = manager.machine.time:as_double()
	if now < answer_at then return end
	if answer_down_at == nil then
		press_key(answer_keys[answer_next])
		answer_down_at = now
		return
	end
	-- One key at a time, held then released, because a keyboard cannot report
	-- two presses in the same instant and the firmware reads one character per
	-- poll of its receiver.
	if now >= answer_down_at + hold_s then
		release_key()
		answer_down_at = nil
		answer_next = answer_next + 1
	end
end

local function state_dump_now(why)
	if state_dump_path == nil or state_dumped then return end
	state_dumped = true
	local ok, err = pcall(function()
		manager.machine:apollo_dump_state(state_dump_path)
	end)
	note("# state dump (%s) at %.4fs -> %s%s\n", why,
	     manager.machine.time:as_double(), state_dump_path,
	     ok and "" or (" FAILED: " .. tostring(err)))
end

_G.apollo_fetch_tap = _G.apollo_fetch_tap or nil
if state_dump_fetch ~= nil then
	local ok, err = pcall(function()
		local sp = manager.machine.devices[":maincpu"].spaces["program"]
		local cpu = manager.machine.devices[":maincpu"]
		_G.apollo_fetch_tap = sp:install_read_tap(state_dump_fetch,
			state_dump_fetch + 3, "state-dump-fetch",
			function()
				-- **A read tap cannot tell a fetch from a data read**, and the
				-- boot PROM reads main memory long before it executes any of
				-- it: the first version of this fired at 0.03 emulated seconds,
				-- during a memory test, and dumped a machine 160 M
				-- instructions before the point it was meant to capture. So the
				-- PC decides: the address is only *executed* when the program
				-- counter is there.
				-- **A window, not an equality.** MAME's `PC` may already have
				-- advanced by the time a tap callback runs -- the switch tap in
				-- this same file matches a range for exactly that reason, and
				-- an exact test here installed cleanly, never fired, and left a
				-- run that looked like the address was never executed. Sixteen
				-- bytes is wide enough for the fetch and narrow enough that no
				-- unrelated code sits in it.
				local pc = cpu.state["PC"].value
				if pc >= state_dump_fetch - 8 and pc <= state_dump_fetch + 8 then
					state_dump_now("fetch")
				end
			end)
	end)
	if not ok then note("# fetch tap failed: %s\n", tostring(err)) end
end

local function state_dump_poll()
	if state_dump_path == nil or state_dumped then return end
	if state_dump_fetch ~= nil then return end
	if manager.machine.time:as_double() < state_dump_at then return end
	state_dumped = true
	local ok, err = pcall(function()
		manager.machine:apollo_dump_state(state_dump_path)
	end)
	note("# state dump at %.4fs -> %s%s\n", manager.machine.time:as_double(),
	     state_dump_path, ok and "" or (" FAILED: " .. tostring(err)))
end

local function crp_poll()
	if crp_file == nil then return end
	local cpu = manager.machine.devices[":maincpu"]
	local t = manager.machine.time:as_double()
	local pc = cpu.state["PC"].value
	for _, n in ipairs({ "CRP_APTR", "CRP_LIMIT", "SRP_APTR", "SRP_LIMIT" }) do
		local v = cpu.state[n].value
		if v ~= crp_last[n] then
			if crp_last[n] ~= nil then crp_changes = crp_changes + 1 end
			crp_last[n] = v
			crp_file:write(string.format("%10.4f  %-9s %08X  pc %08X\n", t, n, v, pc))
			crp_file:flush()
		end
	end
end

emu.register_periodic(function()
	crp_poll()
	answer_poll()
	state_dump_poll()
	if finished then
		return
	end
	if not installed then
		install()
		return
	end

	if not posted and manager.machine.time.seconds >= post_at_s then
		posted = true
		if post_text then
			press_key(post_text)
		else
			note("# not pressing a key: APOLLO_MD_POST=none\n")
		end
	end

	-- Held for a moment and then released, because a key that is never released
	-- is not a keystroke: the Apollo keyboard is a scanning device and reports
	-- transitions, so a permanently-down key produces one event and then looks
	-- like a stuck key rather than typing.
	if posted and not released and
	   manager.machine.time.seconds >= post_at_s + hold_s then
		released = true
		release_key()
	end

	-- Polled on emulated time rather than every callback: this is a file read,
	-- and `register_periodic` fires far too often to spend one on each.
	if swapfile ~= nil and swapfile ~= "" and
	   manager.machine.time.seconds - swap_at >= swap_every then
		swap_at = manager.machine.time.seconds
		poll_swap()
	end

	if activity_s > 0 and
	   manager.machine.time.seconds - reported_at >= activity_s then
		reported_at = manager.machine.time.seconds
		report_activity()
	end

	if until_s > 0 and manager.machine.time.seconds >= until_s then
		note("# end at %.1f emulated seconds\n", until_s)
		finished = true
		manager.machine:exit()
	end
end)
