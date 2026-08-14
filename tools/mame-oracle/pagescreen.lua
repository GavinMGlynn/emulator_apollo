-- `screencap.lua`, plus a watch on two page-table descriptors.
--
-- ## The question
--
-- Our core livelocks: the process faults fetching `0081B14A`, whose page has no
-- translation, Domain/OS answers with `FIM_$UII` and *delivers* the fault
-- rather than paging it in, and after fifty deliveries the user stack runs off
-- its page and the kernel dies. The oracle boots the same volume to `login:`.
-- So either it makes page `0081B000` resident and ours fails to -- a loader or
-- pager defect on our side -- or it never executes there at all, and our
-- program flow diverged earlier. Those need opposite fixes.
--
-- ## Why it is composed onto screencap.lua
--
-- Two runs have already proved nothing by not booting. A bare autoboot script
-- leaves the machine in **Service** mode (`PORT_CONFSETTING(0x00, "Service")`
-- is MAME's default, so a run that ignores the port is a service-mode run
-- whatever it intended) and sitting at SELF_TEST's "DO YOU WISH TO CONTINUE
-- (Y,N)?" with `PC 00002670`. `screencap.lua` is the part that knows to set
-- Normal, `soft_reset()` so the firmware re-runs with it, and press keys by
-- `PORT_NAME` -- `apollo_kbd.cpp` defines no `PORT_CHAR`, so MAME's natural
-- keyboard silently does nothing. Return is `Unnamed Key`, not `Numpad Enter`,
-- which Domain/OS ignores.
--
-- ## Why a memory watch and not a PC sample
--
-- Sampling `PC` cannot show that an address is *never* executed. A descriptor
-- is a state: once a page is made resident the longword changes and stays
-- changed, so any poll rate sees it.
--
-- `0129C1B0` is the page-table entry for `0081B000`, from our side's
-- `--dump-walk 0081B14A` reporting `STOPPED after 3 level(s), last descriptor
-- at 0129C1B0`. `0129C1A0` is the entry for `0081A000` in the same table, which
-- our side does make resident: it is the **control**. Both holding the memory
-- test's address-in-address pattern with `PC` still in the PROM means this run
-- did not boot and proves nothing -- which is exactly what the first two
-- attempts reported, and why the control is here.
--
-- The 68030 translates inside the CPU, so the device's `program` space carries
-- physical addresses, which is what a page-table entry is.
--
-- ## Use
--
--   APOLLO_SNAP_MODE=normal APOLLO_SNAP_KEYS="Y@45,Y@500,Unnamed Key@502" \
--   APOLLO_SNAP_UNTIL=900 APOLLO_PAGE_LOG=... \
--   mame apollo dn3500 ... -autoboot_script tools/mame-oracle/pagescreen.lua
--
-- `screencap.lua` resets the machine and the reset re-runs this script with
-- fresh locals, so the watch's own guard lives in `_G` or it registers twice.

local HERE = "/home/gavin/Development/emulators/apollo/tools/mame-oracle/"
dofile(HERE .. "screencap.lua")

local G = _G
if not G.APOLLO_PAGEWATCH then
	G.APOLLO_PAGEWATCH = true

	local LOG = os.getenv("APOLLO_PAGE_LOG") or "pagewatch.log"
	local HEARTBEAT_S = 10.0

	local WATCH = {
		{ name = "3B5AC000", addr = 0x012953C0 }, -- the page the storm needs
		{ name = "3B5AB000", addr = 0x012953B0 }, -- the control: the module runs here
	}

	local f = assert(io.open(LOG, "w"))
	local cpu = manager.machine.devices[":maincpu"]

	local space = nil
	do
		local ok, s = pcall(function() return cpu.spaces["program"] end)
		if ok then space = s end
	end

	local function reg(name)
		local ok, v = pcall(function() return cpu.state[name].value end)
		if ok then return v end
		return nil
	end

	local function peek(addr)
		if space == nil then return nil end
		local ok, v = pcall(function() return space:read_u32(addr) end)
		if ok then return v end
		return nil
	end

	f:write("# pagescreen: page-table descriptors, with screencap driving the boot\n")
	f:write("# columns: time_s  page  descriptor  pc\n")

	do
		local fatal = {}
		if space == nil then fatal[#fatal + 1] = "no program space" end
		if reg("PC") == nil then fatal[#fatal + 1] = "no PC state register" end
		for _, w in ipairs(WATCH) do
			if peek(w.addr) == nil then
				fatal[#fatal + 1] = string.format("cannot read %08X", w.addr)
			end
		end
		if #fatal > 0 then
			f:write("FATAL " .. table.concat(fatal, "; ") .. "\n")
			f:write("FATAL this run proves nothing about the oracle\n")
		else
			f:write("# program space readable, both descriptors reachable\n")
		end
		f:flush()
	end

	local last = {}
	local next_beat = 0.0
	local changes = 0

	emu.register_periodic(function()
		local t = manager.machine.time:as_double()
		local pc = reg("PC") or 0
		for _, w in ipairs(WATCH) do
			local v = peek(w.addr)
			if v ~= nil and v ~= last[w.name] then
				if last[w.name] ~= nil then changes = changes + 1 end
				last[w.name] = v
				f:write(string.format("%10.4f  %-8s %08X  pc %08X\n", t, w.name, v, pc))
				f:flush()
			end
		end
		if t >= next_beat then
			next_beat = t + HEARTBEAT_S
			f:write(string.format("%10.4f  heartbeat changes=%d B000=%08X A000=%08X pc %08X\n",
				t, changes, last["0081B000"] or 0, last["0081A000"] or 0, pc))
			f:flush()
		end
	end)
end
