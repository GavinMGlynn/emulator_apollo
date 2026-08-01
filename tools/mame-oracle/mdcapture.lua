-- Capture the boot PROM's console output from the oracle, in service mode.
--
-- Phase 1 wants a byte-exact MD transcript, and the route to one narrowed a
-- long way before this script existed. A DN3500 always has a display -- MAME's
-- "Graphics Controller" setting offers three displays and no *none* -- so its
-- console is never the serial port and no amount of tapping serial finds one.
-- What is left is `APOLLO_CONF_SERVICE_MODE`, a genuine two-value setting, and
-- that is what this sets before the machine runs.
--
-- Loaded as -autoboot_script, and it needs `-debug -debugger none` for the same
-- reason the other scripts here do (FINDINGS.md C5). Only one autoboot script
-- is honoured, so this does its own tap rather than composing with
-- writetrace.lua.
--
-- Determinism: nothing observes the host. The configuration is set from a
-- constant, not read from a cfg file, so a run does not depend on state left by
-- a previous one.
--
-- ## Configuration
--
--   APOLLO_MD_UNTIL   emulated seconds to run before stopping (default 10.0)
--   APOLLO_MD_SERVICE "0" to leave the machine in Normal mode, for the control
--                     run. Any transcript is worthless without one: output that
--                     appears in service mode and also in normal mode was not
--                     caused by service mode.

local until_s  = tonumber(os.getenv("APOLLO_MD_UNTIL") or "") or 10.0
local service  = (os.getenv("APOLLO_MD_SERVICE") or "1") ~= "0"

local installed = false
local finished  = false
local taps      = {}
local chars     = 0

local function out(fmt, ...)
	io.write(string.format(fmt, ...))
	io.flush()
end

-- Serial 1's two transmit buffers. Register 3 is channel A and register 11 is
-- channel B, and the SIO puts each register two bytes apart -- so 010406 and
-- 010416. MAME refuses a tap that is not dword-aligned, so the whole device
-- range is taken and the offsets are filtered here.
-- Both DUARTs, and *every* register rather than only the transmit buffers.
--
-- The first version of this filtered to the two transmit buffers, found
-- nothing, and could not say whether that meant the firmware sent no characters
-- or sent them somewhere else. Filtering to the answer you expect is how a
-- search misses the thing beside it, so this reports the lot and lets the
-- reader filter.
local SIO_BASE = {[0] = 0x010400, [1] = 0x010500}

-- Table 4-1's register names on write, which are not the read names: this part
-- has different registers at the same address in each direction, so a trace
-- labelled with read names would be wrong at exactly the interesting moments.
local WRITE_NAME = {
	[0] = "MR1A/MR2A", [1] = "CSRA", [2] = "CRA", [3] = "THRA",
	[4] = "ACR", [5] = "IMR", [6] = "CTUR", [7] = "CTLR",
	[8] = "MR1B/MR2B", [9] = "CSRB", [10] = "CRB", [11] = "THRB",
	[12] = "IVR", [13] = "OPCR", [14] = "OPR_SET", [15] = "OPR_CLEAR",
}

local function set_service_mode()
	local port = manager.machine.ioport.ports[":apollo_config"]
	if port == nil then
		-- Naming the ports that *do* exist, because "not found" alone cannot
		-- distinguish a wrong tag from a machine that has no such port.
		out("# no :apollo_config port. ports present:\n")
		for tag, _ in pairs(manager.machine.ioport.ports) do
			out("#   %s\n", tag)
		end
		return
	end
	for name, field in pairs(port.fields) do
		if name:find("Service") then
			field.user_value = service and 1 or 0
			out("# %s = %d\n", name, service and 1 or 0)
			return
		end
	end
	out("# no Service field found in :apollo_config\n")
end

local function install()
	if installed or finished then
		return
	end
	installed = true

	set_service_mode()
	out("# apollo md capture, service=%s, until=%.1fs\n",
	    service and "yes" or "no", until_s)

	local space = manager.machine.devices[":maincpu"].spaces["program"]
	for unit = 0, 1 do
		local base = SIO_BASE[unit]
		local label = unit + 1
		taps[#taps + 1] = space:install_write_tap(
			base, base + 0xFF, string.format("sio%d", label),
			function(offset, data, mask)
				-- Which byte lane carried the write decides which register it
				-- was, so the address alone is not enough on this 32-bit bus.
				for lane = 0, 3 do
					local shift = (3 - lane) * 8
					if (mask >> shift) & 0xFF ~= 0 then
						local addr = (offset & ~3) + lane
						local reg = ((addr - base) >> 1) & 15
						local byte = (data >> shift) & 0xFF
						chars = chars + 1
						out("W sio%d %-9s %02X  (%06X)\n",
						    label, WRITE_NAME[reg] or "?", byte, addr)
					end
				end
				-- Unchanged: an instrument that altered the data would change
				-- what it measured.
				return data
			end)
	end
end

emu.register_periodic(function()
	if finished then
		return
	end
	if not installed then
		install()
		return
	end
	if manager.machine.time.seconds >= until_s then
		out("# end after %d serial write(s)\n", chars)
		finished = true
		manager.machine:exit()
	end
end)
