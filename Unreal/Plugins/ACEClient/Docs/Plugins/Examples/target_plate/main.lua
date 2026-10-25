return function(s, p)
    local target = s.selection
    if not target or target.dead or target.id == s.player then
        return {world_plates = {}, status = "Select a world object"}
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
        world_plates = {{
            id = target.id, name = target.name, role = role,
            level = target.level, color = color, selection_only = true,
            vitals = target.vitals, effects = effects
        }}
    }
end
