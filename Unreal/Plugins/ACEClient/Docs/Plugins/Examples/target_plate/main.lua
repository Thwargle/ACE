local settings
local fields = {"opacity", "background_opacity", "vr_opacity", "vr_background_opacity"}
local labels = {"Desktop plate opacity (%)", "Desktop background opacity (%)", "VR plate opacity (%)", "VR background opacity (%)"}
local defaults = {1, 1, 1, 0.75}

return function(s, p)
    -- Each addon owns these controls and saves its own preferences. No other
    -- plugin's panel or the selected creature's mesh tint is affected.
    if not settings then
        settings = {}
        for i, key in ipairs(fields) do
            settings[key] = math.max(0, math.min(1, tonumber((s.state or {})[key]) or defaults[i]))
        end
    end
    local changed = false
    for _, event in ipairs(s.events or {}) do
        if event.type == "change" and settings[event.id] ~= nil then
            settings[event.id] = math.max(0, math.min(100, tonumber(event.value) or 100)) / 100
            changed = true
        end
    end
    local ui = {}
    for i, key in ipairs(fields) do
        ui[#ui + 1] = {id=key, type="slider", text=labels[i], min=0, max=100, step=5, value=math.floor(settings[key]*100+0.5)}
    end
    local target = s.selection
    if not target or target.dead or target.id == s.player then
        return {world_plates = {}, ui=ui, state=changed and settings or nil, status = "Select a world object"}
    end
    local color = target.object_type == "player" and "blue"
        or target.monster and "red" or "yellow"
    local role = target.title or target.creature_class or target.object_type
    if target.pk_status then role = role .. " · " .. target.pk_status end
    local effects = {}
    for _, effect in ipairs(s.target_effects[string.format("%.0f", target.id)] or {}) do
        if #effects == 16 then break end
        effects[#effects + 1] = {
            spell = effect.spell, icon = effect.icon, remaining = effect.remaining
        }
    end
    return {
        status = "Server-known vitals; timers estimate your own confirmed casts",
        ui = ui, state = changed and settings or nil,
        world_plates = {{
            id = target.id, name = target.name, role = role,
            level = target.level, color = color, selection_only = true,
            opacity = settings.opacity, background_opacity = settings.background_opacity,
            vr_opacity = settings.vr_opacity, vr_background_opacity = settings.vr_background_opacity,
            vitals = target.vitals, effects = effects
        }}
    }
end
