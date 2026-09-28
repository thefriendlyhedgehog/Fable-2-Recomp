-- F5 selector menu.
--
-- Pressing F5 (host keyboard) re-runs this file. It opens a native menu box
-- listing available Lua scripts; pick one with up/down + A to run it.
--
-- Implementation model (matches the in-game Debug Menu / MultipageMenu):
-- a SINGLE persistent per-frame script is registered with the
-- GeneralScriptManager. The top-level below runs on every F5 press, but it
-- only registers the script the FIRST time; each subsequent press just sets a
-- "show" flag that the per-frame coroutine picks up on the next frame. This
-- avoids re-registering a new script (and re-entering the menu box) on every
-- press, which is what crashed the game on the second F5.
--
-- To add a script, add a { name = "...", path = "..." } to F5_MENU_ITEMS.
-- `path` is relative to the game VFS root (data/), forward slashes, e.g.
-- "scripts/recomp/myScript.lua". The file must also be a line in
-- data/dir.manifest (the build auto-maintains that for src/lua/*.lua).

local F5_MENU_ITEMS = {
  { name = "Get Player Position", path = "scripts/recomp/getPlayerPos.lua" },
  { name = "Get Player Position", path = "scripts/recomp/getPlayerPos.lua" },
}

-- Run a script from the game VFS. The game's custom loadfile resolves paths
-- relative to the data/ root (io is disabled), so there is no "data/" prefix.
-- Returns (chunk, nil) on success or (nil, msg) when not found / compile error.
local function run_script(path)
  local chunk, err = loadfile(path)
  if type(chunk) ~= "function" then
    local why = (err and err ~= "") and tostring(err)
        or "file not found in the game VFS (is it in data/dir.manifest?)"
    pcall(GUI.DisplayMessageBox, "F5: could not load " .. path .. "\n" .. why)
    return
  end
  local ok, runerr = pcall(chunk)
  if not ok then
    pcall(GUI.DisplayMessageBox, "F5: error running " .. path .. "\n" .. tostring(runerr))
  end
end

-- Shared state that persists across F5 presses (the game uses one Lua state,
-- so a global table survives across separate runs of this file).
local st = rawget(_G, "__f5_menu_state")
if not st then
  st = { show = false, registered = false, menu_open = false }
  rawset(_G, "__f5_menu_state", st)
end
-- Ignore an F5 press while the menu is already up: re-opening the native menu
-- box on top of the live one (and re-running the script loader mid-modal) is
-- what hard-crashed the game. The press is simply dropped.
if not st.menu_open then
  st.show = true -- this F5 press requests the menu
end

if not st.registered then
  st.registered = true

  -- Open the native menu box and wait for the selection. Returns the 1-based
  -- item index, or 0 if cancelled. Uses the game's idiom: capture the most
  -- recent message id BEFORE opening, then poll for the MENUBOX message; yield
  -- once more after the selection so the menu box fully closes first.
  local function open_menu()
    st.menu_open = true
    local args = { "F5 Menu" }
    for _, item in ipairs(F5_MENU_ITEMS) do
      table.insert(args, item.name)
    end
    local last_id = MessageEvents.GetMostRecentMessageID()
    GUI.DisplayMenuBox(unpack(args))
    while true do
      local posted, message =
          MessageEvents.IsMessagePosted(EMessageEventType.MESSAGE_EVENT_MENUBOX, last_id)
      if posted then
        local choice = message:GetExtraDataAsNumber() -- 1-based; 0 = cancelled.
        coroutine.yield() -- let the menu box finish closing.
        st.menu_open = false
        return choice
      end
      coroutine.yield()
    end
  end

  -- Persistent per-frame body, driven as a coroutine by the GeneralScriptManager.
  local function update()
    local cooldown = 0
    while true do
      if cooldown > 0 then
        -- The game's menu box needs a few frames to fully tear down after a
        -- selection/cancel before it can be re-opened without an error. Hold
        -- off on re-opening until the cooldown elapses.
        cooldown = cooldown - 1
      elseif st.show then
        st.show = false
        local choice = open_menu()
        if choice >= 1 and choice <= #F5_MENU_ITEMS then
          run_script(F5_MENU_ITEMS[choice].path)
        end
        cooldown = 30
      end
      coroutine.yield()
    end
  end

  local menutab = {}
  setmetatable(menutab, menutab)
  menutab.__index = _G
  menutab._G = _G
  menutab.Update = update
  GeneralScriptManager.AddScript(menutab)
end
