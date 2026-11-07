-- Copy this folder into Saved/ClientPlugins/Installed, Refresh, then enable.
-- This optional example never uses or moves an item and never requests combat.
local settings
local specs = {
    panel_scale={"Panel scale", 0.5, 1.5, 0.05, 1},
    panel_x={"Panel X offset", -40, 40, 1, 0},
    panel_y={"Panel Y offset", -30, 30, 1, 0},
    scale={"Desktop plate scale", 0.25, 4, 0.05, 1},
    offset_x={"Desktop plate X", -200, 200, 1, 0},
    offset_y={"Desktop plate Y", -200, 200, 1, 0},
    vr_scale={"VR plate scale", 0.25, 4, 0.05, 1},
    vr_offset_x={"VR plate X", -200, 200, 1, 0},
    vr_offset_y={"VR plate Y", -200, 200, 1, 0}
}
local colors = {
    panel_color={"Panel gauge", "#32C87FFF"},
    color={"Plate name and brackets", "#FFC040FF"},
    text_color={"Plate text and numbers", "#FFFFFFFF"},
    background_color={"Plate backing", "#17202EFF"},
    health_color={"Health bar", "#E02040FF"},
    stamina_color={"Stamina bar", "#E8BA20FF"},
    mana_color={"Mana bar", "#3080FFFF"}
}
local function valid_color(v)
    -- The sandbox deliberately omits unbounded Lua pattern functions.
    if type(v) ~= "string" or (#v ~= 7 and #v ~= 9) or v:sub(1,1) ~= "#" then return false end
    for i=2,#v do
        local c = v:byte(i)
        if not ((c >= 48 and c <= 57) or (c >= 65 and c <= 70) or (c >= 97 and c <= 102)) then return false end
    end
    return true
end
local function control(key)
    if colors[key] then
        return {id=key, type="color", text=colors[key][1], value=settings[key]}
    end
    local spec = specs[key]
    return {id=key, type="slider", text=spec[1], min=spec[2], max=spec[3], step=spec[4], value=settings[key]}
end
return function(s, p)
    if not settings then
        settings = {}
        local saved = s.state or {}
        for key, spec in pairs(specs) do
            settings[key] = math.max(spec[2], math.min(spec[3], tonumber(saved[key]) or spec[5]))
        end
        for key, spec in pairs(colors) do settings[key] = valid_color(saved[key]) and saved[key] or spec[2] end
    end
    local changed = false
    for _, e in ipairs(s.events or {}) do
        if e.type == "change" then
            if colors[e.id] and valid_color(e.value) then settings[e.id] = e.value; changed = true end
            local spec = specs[e.id]
            if spec and type(e.value) == "number" then
                settings[e.id] = math.max(spec[2], math.min(spec[3], e.value)); changed = true
            end
        end
    end
    local plate_controls = {}
    for _, key in ipairs({"color", "text_color", "background_color", "health_color", "stamina_color", "mana_color"}) do
        plate_controls[#plate_controls+1] = control(key)
    end
    local placement = {}
    for _, key in ipairs({"scale", "offset_x", "offset_y", "vr_scale", "vr_offset_x", "vr_offset_y"}) do
        placement[#placement+1] = control(key)
    end
    local items = {{id="items_hint", text="Select a carried item. Open its pack in inventory to see the highlight."}}
    local count = 0
    for _, item in ipairs(s.inventory or {}) do
        if item.container and item.container ~= 0 and count < 24 then
            count = count+1
            items[#items+1] = {id="item_"..string.format("%.0f", item.id), type="button",
                text=item.name, action="select_item", target=item.id, container=item.container}
        end
    end
    local plates, target = {}, s.selection
    if target and not target.dead and target.id ~= s.player and target.object_type ~= "item" then
        local plate = {id=target.id, name=target.name, role=target.object_type,
            level=target.level, vitals=target.vitals, selection_only=true, refresh=false,
            opacity=1, background_opacity=1}
        for _, key in ipairs({"color", "text_color", "background_color", "health_color", "stamina_color", "mana_color",
            "scale", "offset_x", "offset_y", "vr_scale", "vr_offset_x", "vr_offset_y"}) do plate[key] = settings[key] end
        plates[1] = plate
    end
    return {state=changed and settings or nil, world_plates=plates, ui={
        {id="settings", type="tabs", children={
            {id="panel", type="column", text="Panel", children={control("panel_color"), control("panel_scale"), control("panel_x"), control("panel_y")}},
            {id="plate_colors", type="column", text="Plate colors", children=plate_controls},
            {id="placement", type="column", text="Plate position", children=placement},
            {id="items", type="column", text="Items", children=items}
        }},
        -- Reserve space around render transforms; they do not enlarge layout.
        {id="preview_space", type="overlay", height=130, children={
            {id="preview", type="column", padding=50, scale=settings.panel_scale,
                offset_x=settings.panel_x, offset_y=settings.panel_y, children={
                {id="sample", type="progress", text="Panel preview", value=0.7, color=settings.panel_color}
            }}
        }}
    }}
end
