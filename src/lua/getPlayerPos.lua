-- getPlayerPos.lua: display the player's current (x, y, z) position in a
-- message box. Run directly, or via the F5 selector menu (F5.lua).
--
-- Position comes from the hero entity: QuestManager.HeroEntity:GetPosition()
-- (equivalently GetPlayerHero():GetPosition() or Debug.GetHero():GetPosition()).
--
-- GetPosition() returns a CVector3 userdata. Its numeric fields are not
-- accessible from Lua, but tostring(vec) yields "CVector3(x,y, z)", so we
-- parse that.

local function parse_vector3(text)
  -- CVector3(201.295682,91.704979, 52.825989) -> x, y, z
  local x, y, z = text:match("CVector3%((%-?%d+%.?%d*),%s*(%-?%d+%.?%d*),%s*(%-?%d+%.?%d*)")
  if not x then return nil end
  return tonumber(x), tonumber(y), tonumber(z)
end

local function player_position()
  local hero = QuestManager.HeroEntity
  if not hero then
    hero = (Debug and Debug.GetHero) and Debug.GetHero() or GetPlayerHero()
  end
  if not hero then return nil end
  local ok, pos = pcall(function() return hero:GetPosition() end)
  if not ok then return nil end
  return parse_vector3(tostring(pos))
end

local x, y, z = player_position()
if x then
  GUI.DisplayMessageBox(
      "Player position\nX: " .. string.format("%.3f", x) ..
      "\nY: " .. string.format("%.3f", y) ..
      "\nZ: " .. string.format("%.3f", z))
else
  GUI.DisplayMessageBox("Could not get player position")
end
