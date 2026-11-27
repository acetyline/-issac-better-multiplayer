local role, mapPressed, send, take, reset = ...
local gt = { Name = "Better Multiplayer GoodTrip" }

-- In online co-op, ACTION_MAP (Tab) is local-only and not synced over lockstep.
-- Mods like Reverie (Broken Calendar / Hunger / Corrupt Heart) spawn entities on
-- ACTION_MAP inside PostPlayerUpdate, causing immediate RNG/entity desync.
-- Suppress ACTION_MAP queries during simulation updates in online rooms while
-- keeping rendering/UI and singleplayer completely unaffected.
local sim_depth = 0
local orig_is_action_pressed = Input.IsActionPressed
local orig_is_action_triggered = Input.IsActionTriggered
Input.IsActionPressed = function(action, controller)
    if sim_depth > 0 and action == ButtonAction.ACTION_MAP and role() ~= 0 then
        return false
    end
    return orig_is_action_pressed(action, controller)
end
Input.IsActionTriggered = function(action, controller)
    if sim_depth > 0 and action == ButtonAction.ACTION_MAP and role() ~= 0 then
        return false
    end
    return orig_is_action_triggered(action, controller)
end

local player = nil
local level = nil
local stage = 0
local room = nil
local crd = nil
local last_crd = nil
local crid = -1

local sfx = nil
local mmp = nil
local mic = nil
local gtui = nil
local selectSprite = nil
local cursor = nil
local trash = nil
local sprites_loaded = false

local function ensure_sprites()
    if sprites_loaded then return true end
    sfx = SFXManager()
    mmp = Sprite()
    mmp:Load("bm_goodtrip/minimap0.anm2", true)
    mic = Sprite()
    mic:Load("gfx/ui/minimap_icons.anm2", true)
    gtui = Sprite()
    gtui:Load("bm_goodtrip/gt_ui.anm2", true)
    selectSprite = Sprite()
    selectSprite:Load("bm_goodtrip/gt_ui.anm2", true)
    cursor = Sprite()
    cursor:Load("gfx/ui/cursor.anm2", true)
    cursor:SetFrame("Idle", 0)
    trash = Sprite()
    trash:Load("bm_goodtrip/gt_exit.anm2", true)
    sprites_loaded = true
    return true
end

local mouse_pressed = {false, false, false, false, false}
local key = {
    ButtonAction.ACTION_SHOOTUP,
    ButtonAction.ACTION_SHOOTLEFT,
    ButtonAction.ACTION_SHOOTRIGHT,
    ButtonAction.ACTION_SHOOTDOWN,
}
local dir = {Vector(0, -1), Vector(-1, 0), Vector(1, 0), Vector(0, 1)}
local icon_room = {"RoomOutline", "RoomVisited", "RoomUnvisited", "RoomCurrent"}
local icon_flag = {
    "1_IconNormal", "IconShop", "3_IconError", "IconTreasureRoom", "IconBoss",
    "IconMiniboss", "IconSecretRoom", "IconSuperSecretRoom", "IconArcade", "IconCurseRoom",
    "IconAmbushRoom", "IconLibrary", "IconSacrificeRoom", "IconDevilRoom", "IconAngelRoom",
    "16_IconDungeon", "17_IconBossRush", "IconIsaacsRoom", "IconBarrenRoom", "IconChestRoom",
    "IconDiceRoom", "22_IconBlackMarket", "23_IconGreedExit", "IconPlanetarium", "TeleporterRoom",
    "TeleporterRoom", "27_SecretExit", "28_Blue", "IconUltraSecretRoom",
}
local icon_flag2 = {"IconLockedRoom", "IconTreasureRoomGreed", "IconBossAmbushRoom", "IconTreasureRoomRed"}

local scpos = Vector(0, 0)
local grid_room = {}
local draw_room_id = {}
local draw_room_pos = {}
local draw_room_shape = {}
local draw_icon_pos = {
    Vector(0, 0), Vector(0, 0), Vector(0, 0), Vector(0, 3),
    Vector(0, 3), Vector(4, 0), Vector(4, 0), Vector(4, 3),
    Vector(8, 7), Vector(0, 7), Vector(8, 0), Vector(0, 0),
}
local ltroom = Vector(6, 6)
local rbroom = Vector(6, 6)
local mmp_pos0 = Vector(0, 0)
local mmp_ltpos_ = Vector(0, 0)
local mmp_ltpos = Vector(100, 100)
local mmp_rbpos_ = Vector(0, 0)
local mmp_rbpos = Vector(0, 0)
local d_pos = Vector(0, 0)
local mmp_pin = 0
local mouse_magnet = false
local mpos = Vector(0, 0)
local ui_timer = 0
local mmp_ctrl = false
local mmp_ctrl_pos = Vector(0, 0)
local mouse_in_ui = false
local prev_map_held = false
local eid_lang_applied = false

local function ensure_eid_chinese()
    if eid_lang_applied or not EID or not EID.Config then return end
    if EID.Config["Language"] ~= "zh_cn" then
        EID.Config["Language"] = "zh_cn"
        if EID.fixDefinedFont then
            pcall(EID.fixDefinedFont, EID, true)
        end
    end
    eid_lang_applied = true
end

local tele_maze = false
local secret_pre_room_id = {}
local prep_alarm = false
local n_room_num = 0

local hudoffset = 0
local keyboardmap = true
local tele_anime = RoomTransitionAnim.FADE
local tele_cd0 = 10
local tele_cd = 0

local function refresh_state()
    local game = Game()
    if game:GetNumPlayers() == 0 then
        player, level, room, crd = nil, nil, nil, nil
        return false
    end
    player = Isaac.GetPlayer(0)
    level = game:GetLevel()
    room = game:GetRoom()
    crd = level:GetCurrentRoomDesc()
    stage = level:GetStage()
    if not player or not level or not room or not crd or not crd.Data then
        return false
    end
    crid = crd.GridIndex
    return true
end

local function is_map_pressed(current_role)
    if current_role == 1 then
        return mapPressed()
    elseif current_role == 0 and player then
        return Input.IsActionPressed(ButtonAction.ACTION_MAP, player.ControllerIndex)
    end
    return false
end

local function check_pos_en_box(pos, ltpos, rbpos)
    return pos.X > ltpos.X and pos.X < rbpos.X and pos.Y > ltpos.Y and pos.Y < rbpos.Y
end

local function is_mouse_btn_triggered(m)
    if Input.IsMouseBtnPressed(m) then
        if not mouse_pressed[m + 1] then
            mouse_pressed[m + 1] = true
            return true
        end
    else
        mouse_pressed[m + 1] = false
    end
    return false
end

local function check_room_open()
    for i = 0, 7 do
        local door = room:GetDoor(i)
        if door and door:IsOpen() then
            return true
        end
    end
    return false
end

local function check_teleble(gid)
    if not level or not crd or not crd.Data then
        return false
    end
    if gid == -99 or (level:GetCurses() & LevelCurse.CURSE_OF_THE_LOST) ~= 0 then
        return false
    end
    local cid = crd.SafeGridIndex
    if grid_room[cid] == nil or not crd.Clear then
        return false
    elseif crd.Data.Type == 6 or crd.Data.Type == 11 then
        if not check_room_open() then
            return false
        end
    end
    if gid == false then
        return true
    end
    local trd = grid_room[gid]
    if trd == nil or not trd.Data then
        return false
    end
    if trd.ListIndex == crd.ListIndex or trd.VisitedCount == 0 or not trd.Clear then
        return false
    end
    return true
end

local function hurt(n)
    if player then
        player:TakeDamage(n, DamageFlag.DAMAGE_CURSED_DOOR | DamageFlag.DAMAGE_NO_PENALTIES, EntityRef(player), 0)
    end
end

local function tele_failed()
    if not sfx then sfx = SFXManager() end
    sfx:Play(187, 0.5, 0, false, 1)
end

local function pre_secret_room()
    if not room then return end
    for i = 0, 7 do
        local door = room:GetDoor(i)
        if door and door.Desc and door.Desc.Variant == 8 then
            local id = door.TargetRoomIndex
            if door.TargetRoomType == 10 then
                if not secret_pre_room_id[crid] then
                    secret_pre_room_id[crid] = id
                end
            elseif grid_room[id] and grid_room[id].VisitedCount == 0 then
                secret_pre_room_id[crid] = id
            elseif grid_room[id] then
                secret_pre_room_id[crid] = id
                break
            end
        end
    end
end

local function pre_secret_curse_room()
    if not room then return end
    for i = 0, 7 do
        local door = room:GetDoor(i)
        if door and door.Desc and door.Desc.Variant == 8 then
            local id = door.TargetRoomIndex
            if door.TargetRoomType == 7 then
                if secret_pre_room_id[id] and secret_pre_room_id[id] ~= crid then
                    secret_pre_room_id[crid] = id
                    break
                else
                    secret_pre_room_id[crid] = id
                end
            end
        end
    end
end

local function check_curse_room(gid)
    local trd = grid_room[gid]
    if not trd or not trd.Data then return end
    if crd.Data.Type == 10 then
        if secret_pre_room_id[crid] and (secret_pre_room_id[crid] == gid
            or (secret_pre_room_id[secret_pre_room_id[crid]] and secret_pre_room_id[secret_pre_room_id[crid]] ~= crid)) then
            return
        end
        hurt(1)
    elseif trd.Data.Type == 10 and not player:IsFlying() then
        if secret_pre_room_id[gid] and (secret_pre_room_id[gid] == crid
            or (secret_pre_room_id[secret_pre_room_id[gid]] and secret_pre_room_id[secret_pre_room_id[gid]] ~= gid)) then
            return
        end
        hurt(1)
    end
end

local function can_execute_teleport(gid)
    if not grid_room[gid] or not grid_room[gid].Data then
        return false
    end
    for _, en in pairs(Isaac.GetRoomEntities()) do
        if en.Type == 867 then
            return false
        end
    end
    if crd.Data.Name == "Mom" then
        return false
    elseif grid_room[gid].Data.Type == 11 and not grid_room[gid].ChallengeDone then
        if stage % 2 == 0 and stage ~= 10 then
            if player:GetHearts() + player:GetSoulHearts() + player:GetBlackHearts() > 2 then
                return false
            end
        else
            if player:GetHearts() + player:GetSoulHearts() + player:GetBlackHearts() < player:GetMaxHearts() then
                return false
            end
        end
    end
    return true
end

local function execute_teleport(gid)
    if not refresh_state() then return end
    if crd.Data.Type == 7 or (crd.Data.Type == 8 and Game():IsGreedMode()) then
        pre_secret_room()
    elseif crd.Data.Type == 10 then
        pre_secret_curse_room()
    end
    if not can_execute_teleport(gid) then
        tele_failed()
        return
    end

    local flat = player:HasTrinket(151) or player:HasCollectible(276) or player:HasCollectible(663)
    if not flat then
        check_curse_room(gid)
    end

    level.EnterDoor = -1
    level.LeaveDoor = -1

    if (level:GetCurses() & LevelCurse.CURSE_OF_MAZE) ~= 0 then
        level:RemoveCurses(LevelCurse.CURSE_OF_MAZE)
        tele_maze = true
    end

    if crd.Data.Type == 7 and secret_pre_room_id[crid] and grid_room[secret_pre_room_id[crid]] then
        if grid_room[secret_pre_room_id[crid]].ListIndex == grid_room[gid].ListIndex then
            gid = secret_pre_room_id[crid]
        else
            if grid_room[gid].Data.Type == 10 and secret_pre_room_id[gid] and secret_pre_room_id[gid] == crid then
            else
                if grid_room[secret_pre_room_id[crid]].Data.Type == 10 and not flat then
                    hurt(1)
                end
                Game():ChangeRoom(secret_pre_room_id[crid], -1)
            end
        end
    end
    if grid_room[gid] and grid_room[gid].Data and grid_room[gid].Data.Type == 7 then
        if secret_pre_room_id[gid] and grid_room[secret_pre_room_id[gid]] then
            if grid_room[secret_pre_room_id[gid]].ListIndex == grid_room[crid].ListIndex then
                if grid_room[crid] and grid_room[crid].Data and grid_room[crid].Data.Shape > 3 then
                    Game():ChangeRoom(secret_pre_room_id[gid], -1)
                end
            else
                if crd.Data.Type == 10 and secret_pre_room_id[crid] and secret_pre_room_id[crid] == gid then
                else
                    if grid_room[secret_pre_room_id[gid]].Data.Type == 10 and not player:IsFlying() and not flat then
                        hurt(1)
                    end
                    Game():ChangeRoom(secret_pre_room_id[gid], -1)
                end
            end
        end
    end

    Game():StartRoomTransition(gid, Direction.NO_DIRECTION, tele_anime, player, -1)
    tele_cd = tele_cd0
end

local function request_teleport(gid, current_role)
    if not check_teleble(gid) or tele_cd >= 1 then
        return
    end
    local trd = grid_room[gid]
    if not trd then return end
    local target = trd.SafeGridIndex
    local from = crd.SafeGridIndex
    if target < 0 or target >= 169 or from < 0 or from >= 169 or target == from then
        return
    end
    if not can_execute_teleport(target) then
        tele_failed()
        return
    end
    if current_role == 0 then
        execute_teleport(target)
    elseif current_role == 1 then
        if send(from, target) then
            tele_cd = tele_cd0
        end
    end
end

local function get_grid_room()
    grid_room = {}
    if not level then return end
    local all_room = level:GetRooms()
    for i = 0, all_room.Size - 1 do
        local des = all_room:Get(i)
        if des then
            local gid = des.GridIndex
            for jx = 0, 1 do
                for jy = 0, 1 do
                    local tgid = gid + jx + jy * 13
                    if tgid >= 0 and tgid < 169 then
                        local tdes = level:GetRoomByIdx(tgid, -1)
                        if tdes and tdes.ListIndex == des.ListIndex then
                            grid_room[tgid] = des
                        end
                    end
                end
            end
        end
    end
end

local function get_corner_room(num)
    local corner_room = Vector(6, 6)
    local fx = {1, -1, 1, -1}
    local fy = {1, 1, -1, -1}
    local ffx = fx[num]
    local ffy = fy[num]
    for i = 6 - 6 * ffx, 6 - ffx, ffx do
        for j = 0, 12 do
            local r = grid_room[i + j * 13]
            if r and r.DisplayFlags > 0 then
                corner_room.X = i
                break
            end
        end
        if corner_room.X ~= 6 then break end
    end
    for j = 6 - 6 * ffy, 6 - ffy, ffy do
        for i = 0, 12 do
            local r = grid_room[i + j * 13]
            if r and r.DisplayFlags > 0 then
                corner_room.Y = j
                break
            end
        end
        if corner_room.Y ~= 6 then break end
    end
    return corner_room
end

local function get_pos_grid_index(pos)
    local rtroom = get_corner_room(2)
    local ltx = scpos.X - (rtroom.X + 1) * 17 - 5 - hudoffset * 2.4
    local lty = -(rtroom.Y) * 15 + 5 + hudoffset * 1.3
    if pos.X > ltx and pos.Y > lty and pos.X < ltx + 222 and pos.Y < lty + 196 then
        return math.floor((pos.X - ltx) / 17) + math.floor((pos.Y - lty) / 15) * 13
    end
    return -99
end

local function get_pos_grid_index_mmp(pos)
    if check_pos_en_box(pos, mmp_ltpos + Vector(1, 1), mmp_rbpos + Vector(11, 10)) then
        return math.floor((pos.X - mmp_pos0.X - 2) / 8) + math.floor((pos.Y - mmp_pos0.Y - 2) / 7) * 13
    end
    return -99
end

local function prep_minimap()
    draw_room_id = {}
    draw_room_pos = {}
    draw_room_shape = {}
    ltroom = get_corner_room(1)
    rbroom = get_corner_room(4)
    mmp_ltpos_ = Vector(ltroom.X * 8, ltroom.Y * 7)
    mmp_rbpos_ = Vector(rbroom.X * 8, rbroom.Y * 7)
    mmp_pos0 = mmp_ltpos - mmp_ltpos_
    mmp_rbpos = mmp_pos0 + mmp_rbpos_
    if mmp_ctrl and crid >= 0 then
        local gx = crid % 13
        local gy = (crid - gx) / 13
        mmp_ctrl_pos = mmp_pos0 + Vector(gx * 8 + 6, gy * 7 + 5)
    end
    for i = 0, 12 do
        for j = 0, 12 do
            local drd = grid_room[i * 13 + j]
            if drd and drd.Data and drd.DisplayFlags > 0 then
                if drd.Data.Type == 5 and drd.Data.Shape > 3 and stage == 12 then
                    local near_room = {
                        grid_room[i * 13 + j - 13] ~= nil,
                        grid_room[i * 13 + j - 1] and j > 0,
                        grid_room[i * 13 + j + 1] and j < 12,
                        grid_room[i * 13 + j + 13] ~= nil,
                    }
                    if (near_room[1] and near_room[4])
                        or (near_room[2] and near_room[3])
                        or (drd.Data.Shape == 6 and (near_room[1] or near_room[4]))
                        or (drd.Data.Shape == 4 and (near_room[2] or near_room[3])) then
                        table.insert(draw_room_id, i * 13 + j)
                        table.insert(draw_room_shape, 1)
                        table.insert(draw_room_pos, Vector(mmp_pos0.X + 8 * j, mmp_pos0.Y + 7 * i))
                    end
                elseif drd.Data.Shape == RoomShape.ROOMSHAPE_LTL then
                    table.insert(draw_room_id, i * 13 + j)
                    table.insert(draw_room_shape, drd.Data.Shape)
                    table.insert(draw_room_pos, Vector(mmp_pos0.X + 8 * (j - 1), mmp_pos0.Y + 7 * i))
                else
                    table.insert(draw_room_id, i * 13 + j)
                    table.insert(draw_room_shape, drd.Data.Shape)
                    table.insert(draw_room_pos, Vector(mmp_pos0.X + 8 * j, mmp_pos0.Y + 7 * i))
                end
            end
        end
    end
end

local function draw_minimap_ui()
    if not (keyboardmap and check_teleble(false)) then
        ui_timer = 0
        return
    elseif ui_timer < 10 then
        ui_timer = ui_timer + 1
    end
    gtui:SetFrame("ui1", ui_timer)
    gtui:Render(Vector(mmp_ltpos.X, mmp_ltpos.Y), Vector(0, 0), Vector(0, 0))
    gtui:SetFrame("ui3", ui_timer)
    gtui:Render(Vector(mmp_rbpos.X, mmp_ltpos.Y), Vector(0, 0), Vector(0, 0))
    gtui:SetFrame("ui7", ui_timer)
    gtui:Render(Vector(mmp_ltpos.X, mmp_rbpos.Y), Vector(0, 0), Vector(0, 0))
    gtui:SetFrame("ui9", ui_timer)
    gtui:Render(Vector(mmp_rbpos.X, mmp_rbpos.Y), Vector(0, 0), Vector(0, 0))
    for i = ltroom.X, rbroom.X do
        gtui:SetFrame("ui2", ui_timer)
        gtui:Render(mmp_pos0 + Vector(i * 8, ltroom.Y * 7), Vector(0, 0), Vector(0, 0))
        gtui:SetFrame("ui8", ui_timer)
        gtui:Render(mmp_pos0 + Vector(i * 8, rbroom.Y * 7), Vector(0, 0), Vector(0, 0))
    end
    for j = ltroom.Y, rbroom.Y do
        gtui:SetFrame("ui4", ui_timer)
        gtui:Render(mmp_pos0 + Vector(ltroom.X * 8, j * 7), Vector(0, 0), Vector(0, 0))
        gtui:SetFrame("ui6", ui_timer)
        gtui:Render(mmp_pos0 + Vector(rbroom.X * 8, j * 7), Vector(0, 0), Vector(0, 0))
    end
    gtui:SetFrame("ui5", ui_timer)
    for i = ltroom.X, rbroom.X do
        for j = ltroom.Y, rbroom.Y do
            gtui:Render(mmp_pos0 + Vector(i * 8, j * 7), Vector(0, 0), Vector(0, 0))
        end
    end
    if mmp_pin == 1 then
        gtui:SetFrame("pin1", ui_timer)
    else
        gtui:SetFrame("pin0", ui_timer)
    end
    gtui:Render(mmp_ltpos, Vector(0, 0), Vector(0, 0))
end

local function draw_minimap()
    mmp:SetFrame(icon_room[1], 0)
    for i = 1, #draw_room_id do
        local rd = grid_room[draw_room_id[i]]
        if rd and rd.Data then
            if rd.Data.Shape == RoomShape.ROOMSHAPE_LTL then
                mmp:Render(draw_room_pos[i] + Vector(8, 0), Vector(0, 0), Vector(0, 0))
            else
                mmp:Render(draw_room_pos[i], Vector(0, 0), Vector(0, 0))
            end
        end
    end
    for i = 1, #draw_room_id do
        local rd = grid_room[draw_room_id[i]]
        if rd and rd.Data then
            if rd.ListIndex < n_room_num then
                mmp.Color = Color(1, 1, 1, 1, 0, 0, 0)
            else
                mmp.Color = Color(1, 0.3, 0.3, 1, 0, 0, 0)
            end
            if rd.SafeGridIndex == draw_room_id[i] or (rd.Data.Type == 5 and stage == 12) then
                if crd.ListIndex == rd.ListIndex then
                    mmp:SetFrame(icon_room[4], draw_room_shape[i] - 1)
                    mmp:Render(draw_room_pos[i], Vector(0, 0), Vector(0, 0))
                elseif rd.VisitedCount > 0 and rd.Clear then
                    mmp:SetFrame(icon_room[2], draw_room_shape[i] - 1)
                    mmp:Render(draw_room_pos[i], Vector(0, 0), Vector(0, 0))
                elseif rd.Data.Type ~= 7 and rd.Data.Type ~= 8 then
                    mmp:SetFrame(icon_room[3], draw_room_shape[i] - 1)
                    mmp:Render(draw_room_pos[i], Vector(0, 0), Vector(0, 0))
                end
                mmp.Color = Color(1, 1, 1, 1, 0, 0, 0)
                if rd.Data.Type > 1 and rd.DisplayFlags > 1 and (rd.DisplayFlags ~= 3 or (rd.Data.Type ~= 6 and rd.Data.Type ~= 13)) then
                    local icon_name = nil
                    if (rd.Data.Type == 2 or rd.Data.Type == 12 or (rd.Data.Type > 17 and rd.Data.Type < 22)) and rd.DisplayFlags == 3 then
                        icon_name = icon_flag2[1]
                    elseif rd.Data.Type == 4 then
                        if Game():IsGreedMode() and rd.GridIndex == 98 then
                            icon_name = icon_flag2[2]
                        elseif player:HasTrinket(146) then
                            icon_name = icon_flag2[4]
                        else
                            icon_name = icon_flag[4]
                        end
                    elseif rd.Data.Type == 11 and stage % 2 == 0 and stage ~= 10 then
                        icon_name = icon_flag2[3]
                    elseif rd.Data.Type ~= 23 then
                        icon_name = icon_flag[rd.Data.Type]
                    end
                    if icon_name then
                        mic:SetFrame(icon_name, 0)
                        local offset = draw_icon_pos[draw_room_shape[i]] or Vector(0, 0)
                        mic:Render(draw_room_pos[i] + offset, Vector(0, 0), Vector(0, 0))
                    end
                end
            end
        end
    end

    local checkid = mmp_ctrl and get_pos_grid_index_mmp(mmp_ctrl_pos) or get_pos_grid_index_mmp(mpos)
    if grid_room[checkid] then
        checkid = grid_room[checkid].SafeGridIndex
        for i = 1, #draw_room_id do
            if checkid == draw_room_id[i] then
                if check_teleble(checkid) then
                    selectSprite:SetFrame("select", draw_room_shape[i])
                else
                    selectSprite:SetFrame("select_false", draw_room_shape[i])
                end
                selectSprite:Render(draw_room_pos[i], Vector(0, 0), Vector(0, 0))
                break
            end
        end
    end

    if mmp_ctrl then
        cursor:Render(mmp_ctrl_pos, Vector(0, 0), Vector(0, 0))
    end
end

local function mmp_ctrl_move()
    local dif = {
        mmp_ctrl_pos.Y - mmp_ltpos.Y + 2,
        mmp_ctrl_pos.X - mmp_ltpos.X + 2,
        mmp_rbpos.X - mmp_ctrl_pos.X + 14,
        mmp_rbpos.Y - mmp_ctrl_pos.Y + 13,
    }
    for i = 1, 4 do
        if Input.IsActionPressed(key[i], player.ControllerIndex) and dif[i] > 0 then
            mmp_ctrl_pos = mmp_ctrl_pos + dir[i]
        end
    end
end

local function mouse_action(current_role)
    if is_mouse_btn_triggered(0) then
        local mgid = get_pos_grid_index(mpos)
        if check_teleble(mgid) and tele_cd < 1 then
            request_teleport(mgid, current_role)
        elseif keyboardmap then
            mgid = get_pos_grid_index_mmp(mpos)
            if check_teleble(mgid) and tele_cd < 1 then
                request_teleport(mgid, current_role)
            elseif check_pos_en_box(mpos, mmp_ltpos + Vector(-6, -15), Vector(mmp_rbpos.X + 18, mmp_ltpos.Y - 1)) then
                if check_pos_en_box(mpos, mmp_ltpos + Vector(-3, -13), mmp_ltpos + Vector(5, -4)) then
                    mmp_pin = mmp_pin == 1 and 0 or 1
                elseif mmp_pin == 0 then
                    mouse_magnet = true
                    d_pos = mmp_ltpos - mpos
                end
            end
        end
    end
    if not keyboardmap then return end
    local cp = scpos / 2
    if Input.IsMouseBtnPressed(0) then
        if mouse_magnet then
            mmp_ltpos = mpos + d_pos
            mmp_pos0 = mmp_ltpos - mmp_ltpos_
            mmp_rbpos = mmp_pos0 + mmp_rbpos_
            prep_minimap()
            if current_role == 0 then
                player:SetShootingCooldown(2)
            end
            if check_pos_en_box(mpos, cp + Vector(-16, -16), cp + Vector(16, 116)) then
                trash:SetFrame("trash", 1)
            else
                trash:SetFrame("trash", 0)
            end
            trash:Render(cp, Vector(0, 0), Vector(0, 0))
        end
    else
        if mouse_magnet then
            mouse_magnet = false
            if check_pos_en_box(mpos, cp + Vector(-16, -16), cp + Vector(16, 16)) then
                keyboardmap = false
            end
        end
        if mmp_ltpos.X < 5 then
            mmp_ltpos.X = 5
            mmp_pos0 = mmp_ltpos - mmp_ltpos_
            mmp_rbpos = mmp_pos0 + mmp_rbpos_
            prep_minimap()
        elseif mmp_rbpos.X > scpos.X - 17 then
            mmp_rbpos.X = scpos.X - 17
            mmp_pos0 = mmp_rbpos - mmp_rbpos_
            mmp_ltpos = mmp_pos0 + mmp_ltpos_
            prep_minimap()
        end
        if mmp_ltpos.Y < 14 then
            mmp_ltpos.Y = 14
            mmp_pos0 = mmp_ltpos - mmp_ltpos_
            mmp_rbpos = mmp_pos0 + mmp_rbpos_
            prep_minimap()
        elseif mmp_rbpos.Y > scpos.Y - 16 then
            mmp_rbpos.Y = scpos.Y - 16
            mmp_pos0 = mmp_rbpos - mmp_rbpos_
            mmp_ltpos = mmp_pos0 + mmp_ltpos_
            prep_minimap()
        end
    end
end

local function tab_action(current_role)
    local cp = Isaac.WorldToRenderPosition(Vector(320, 280))
    scpos = cp + cp
    mpos = Isaac.WorldToScreen(Input.GetMousePosition(true))

    if keyboardmap and check_teleble(false) then
        if not (Input.IsActionPressed(ButtonAction.ACTION_UP, player.ControllerIndex)
            or Input.IsActionPressed(ButtonAction.ACTION_LEFT, player.ControllerIndex)
            or Input.IsActionPressed(ButtonAction.ACTION_RIGHT, player.ControllerIndex)
            or Input.IsActionPressed(ButtonAction.ACTION_DOWN, player.ControllerIndex)) then
            if not check_pos_en_box(mpos, mmp_ltpos + Vector(-8, -18), mmp_rbpos + Vector(20, 20)) then
                if not mmp_ctrl then
                    mmp_ctrl = true
                    local gx = crid % 13
                    local gy = (crid - gx) / 13
                    mmp_ctrl_pos = mmp_pos0 + Vector(gx * 8 + 6, gy * 7 + 5)
                else
                    mmp_ctrl_move()
                    if current_role == 0 then
                        player:SetShootingCooldown(2)
                    end
                end
            else
                mmp_ctrl = false
            end
            draw_minimap_ui()
        else
            mmp_ctrl = false
            if mmp_pin == 1 or check_pos_en_box(mpos, mmp_ltpos + Vector(-8, -18), mmp_rbpos + Vector(20, 20)) then
                draw_minimap_ui()
            else
                ui_timer = 0
            end
        end
        draw_minimap()
    end
    mouse_action(current_role)
end

local function on_render()
    local current_role = role()
    if current_role == 2 then
        mmp_ctrl = false
        mouse_magnet = false
        ui_timer = 0
        prev_map_held = false
        return
    end
    local game = Game()
    if game:IsPaused() or not refresh_state() then
        mmp_ctrl = false
        mouse_magnet = false
        ui_timer = 0
        prev_map_held = false
        return
    end
    ensure_sprites()
    ensure_eid_chinese()

    local held = is_map_pressed(current_role)
    local map_triggered = held and not prev_map_held
    prev_map_held = held

    if map_triggered
        or Input.IsActionTriggered(ButtonAction.ACTION_ITEM, player.ControllerIndex)
        or Input.IsActionTriggered(ButtonAction.ACTION_PILLCARD, player.ControllerIndex) then
        get_grid_room()
        if keyboardmap then
            prep_minimap()
        end
    end

    if held then
        tab_action(current_role)
    elseif keyboardmap then
        if mmp_pin == 1 and crd.Clear and check_teleble(false) then
            mpos = Isaac.WorldToScreen(Input.GetMousePosition(true))
            mouse_in_ui = check_pos_en_box(mpos, mmp_ltpos + Vector(-8, -18), mmp_rbpos + Vector(20, 20))
            if mouse_in_ui then
                if is_mouse_btn_triggered(0) then
                    if check_pos_en_box(mpos, mmp_ltpos + Vector(-3, -13), mmp_ltpos + Vector(5, -4)) then
                        mmp_pin = 0
                    else
                        local mgid = get_pos_grid_index_mmp(mpos)
                        if check_teleble(mgid) and tele_cd < 1 then
                            request_teleport(mgid, current_role)
                        end
                    end
                end
                draw_minimap_ui()
            else
                ui_timer = 0
            end
            draw_minimap()
        else
            ui_timer = 0
        end

        if mmp_ctrl and check_teleble(false) then
            mmp_ctrl = false
            local mgid = get_pos_grid_index_mmp(mmp_ctrl_pos)
            if check_teleble(mgid) and tele_cd < 1 then
                request_teleport(mgid, current_role)
            end
        else
            mmp_ctrl = false
        end
    end

    if prep_alarm then
        if keyboardmap then
            prep_minimap()
        end
        prep_alarm = false
    end
    if tele_cd > 0 then
        tele_cd = tele_cd - 1
    end
end

local function on_update()
    local from, to = take()
    if from and role() ~= 0 then
        if refresh_state() then
            get_grid_room()
            if crd.SafeGridIndex == from and check_teleble(to) then
                execute_teleport(to)
            end
        end
    end
end

local function on_new_room()
    mmp_ctrl = false
    mouse_magnet = false
    reset()
    last_crd = crd
    if not refresh_state() then return end
    get_grid_room()
    if keyboardmap then
        prep_alarm = true
        prep_minimap()
    end
    if tele_maze then
        level:AddCurse(LevelCurse.CURSE_OF_MAZE, false)
        tele_maze = false
    end
    if last_crd and last_crd.Data then
        if last_crd.Data.Type == 7 or (last_crd.Data.Type == 8 and Game():IsGreedMode()) then
            if not secret_pre_room_id[last_crd.GridIndex] then
                if level:GetRoomByIdx(last_crd.GridIndex + 1, -1).ListIndex == crd.ListIndex then
                    secret_pre_room_id[last_crd.GridIndex] = last_crd.GridIndex + 1
                elseif level:GetRoomByIdx(last_crd.GridIndex - 1, -1).ListIndex == crd.ListIndex then
                    secret_pre_room_id[last_crd.GridIndex] = last_crd.GridIndex - 1
                elseif level:GetRoomByIdx(last_crd.GridIndex + 13, -1).ListIndex == crd.ListIndex then
                    secret_pre_room_id[last_crd.GridIndex] = last_crd.GridIndex + 13
                elseif level:GetRoomByIdx(last_crd.GridIndex - 13, -1).ListIndex == crd.ListIndex then
                    secret_pre_room_id[last_crd.GridIndex] = last_crd.GridIndex - 13
                end
            end
        end
    end
    if crd.Data.Type == 7 or (crd.Data.Type == 8 and Game():IsGreedMode()) then
        pre_secret_room()
    elseif crd.Data.Type == 10 then
        pre_secret_curse_room()
    end
end

local function on_new_level()
    if not refresh_state() then return end
    get_grid_room()
    n_room_num = level:GetRooms().Size
    secret_pre_room_id = {}
    if keyboardmap then
        prep_alarm = true
        prep_minimap()
    end
end

local function on_game_started()
    keyboardmap = true
    mmp_ctrl = false
    mouse_magnet = false
    prev_map_held = false
    eid_lang_applied = false
    tele_cd = 0
    tele_maze = false
    reset()
    on_new_level()
    on_new_room()
    ensure_eid_chinese()
end

local function enter_sim()
    sim_depth = sim_depth + 1
end

local function leave_sim()
    if sim_depth > 0 then
        sim_depth = sim_depth - 1
    end
end

local function reset_sim()
    sim_depth = 0
end

Isaac.AddPriorityCallback(gt, ModCallbacks.MC_POST_UPDATE, -1000000, enter_sim)
Isaac.AddPriorityCallback(gt, ModCallbacks.MC_POST_UPDATE, 1000000, leave_sim)
Isaac.AddPriorityCallback(gt, ModCallbacks.MC_POST_PLAYER_UPDATE, -1000000, enter_sim)
Isaac.AddPriorityCallback(gt, ModCallbacks.MC_POST_PLAYER_UPDATE, 1000000, leave_sim)
Isaac.AddPriorityCallback(gt, ModCallbacks.MC_POST_RENDER, -1000000, reset_sim)
Isaac.AddPriorityCallback(gt, ModCallbacks.MC_POST_GAME_STARTED, CallbackPriority.LATE, on_game_started)
Isaac.AddCallback(gt, ModCallbacks.MC_POST_RENDER, on_render)
Isaac.AddPriorityCallback(gt, ModCallbacks.MC_POST_UPDATE, CallbackPriority.EARLY, on_update)
Isaac.AddCallback(gt, ModCallbacks.MC_POST_NEW_ROOM, on_new_room)
Isaac.AddCallback(gt, ModCallbacks.MC_POST_NEW_LEVEL, on_new_level)
