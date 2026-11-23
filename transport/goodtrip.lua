local role, mapPressed, send, take, upvalue, reset = ...
local mod = assert(gt, "GoodTrip mod")
local render, teleport, tab = mod.step, mod.teleport_to_grid_index, mod.tab_action
assert(type(render) == "function" and type(tab) == "function"
    and type(upvalue(teleport, "tele_anime")) == "number", "GoodTrip callback layout")

local mapWasPressed = false
local function restoreAfter(fn, modInstance, restore)
    local ok, message = pcall(fn, modInstance)
    restore()
    if not ok then error(message, 0) end
end

function mod:teleport_to_grid_index(index)
    local currentRole = role()
    if currentRole == 0 then return teleport(self, index) end
    if currentRole == 1 then send(index, upvalue(teleport, "tele_anime")) end
end

function mod:tab_action()
    if role() == 0 then return tab(self) end
    local player = upvalue(tab, "player")
    local renderPlayer = setmetatable({ SetShootingCooldown = function() end }, {
        __index = function(_, key)
            local value = player[key]
            if type(value) == "function" then
                return function(_, ...) return value(player, ...) end
            end
            return value
        end,
    })
    upvalue(tab, "player", renderPlayer)
    restoreAfter(tab, self, function() upvalue(tab, "player", player) end)
end

mod:RemoveCallback(ModCallbacks.MC_POST_RENDER, render)
mod:AddCallback(ModCallbacks.MC_POST_RENDER, function(self)
    local currentRole = role()
    if currentRole == 0 then return render(self) end
    if currentRole ~= 1 or Isaac.GetPlayer(0) == nil then return end

    local input, actions = Input, ButtonAction
    local held, wasHeld = mapPressed(), mapWasPressed
    mapWasPressed = held
    Input = setmetatable({
        IsActionPressed = function(action, controller)
            if action == actions.ACTION_MAP then return held end
            return input.IsActionPressed(action, controller)
        end,
        IsActionTriggered = function(action, controller)
            if action == actions.ACTION_MAP then return held and not wasHeld end
            if action == actions.ACTION_RESTART then return false end
            return input.IsActionTriggered(action, controller)
        end,
    }, { __index = input })
    restoreAfter(render, self, function() Input = input end)
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function(self)
    local index, animation = take()
    if index == nil or role() == 0 then return end
    self:get_grid_room()
    if not self:check_teleble(index) then return end
    local previous = upvalue(teleport, "tele_anime", animation)
    restoreAfter(function() teleport(self, index) end, self, function()
        upvalue(teleport, "tele_anime", previous)
    end)
end)

local function resetRoom()
    mapWasPressed = false
    reset()
end
mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, resetRoom)
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, resetRoom)
