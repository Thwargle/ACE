-- This is a configurable framework example, not a universal quest database.
-- Servers differ in wording and quest keys. Keep the original line for inspection.
local loaded, saved = false, nil
return function(s, p)
    if not loaded then saved = s.state; loaded = true end
    local changed = false
    local key = p.monster or ""
    saved.tasks = saved.tasks or {}
    local task = saved.tasks[key] or {count=0, goal=0, last="No server progress received yet"}
    for _, e in ipairs(s.chat_events) do
        -- Do not accept player speech/tells as authoritative quest progress.
        if e.sender == "" then
            local match = regexmatch(e.raw_text, p.pattern)
            if match and tonumber(match["1"]) and tonumber(match["2"]) then
                task.count, task.goal, task.last = tonumber(match["1"]), tonumber(match["2"]), e.raw_text
                saved.tasks[key] = task
                changed = true
            end
        end
    end
    local rows = {
        {id="progress", text=key .. ": " .. task.count .. " / " .. task.goal},
        {id="last", text=string.sub(task.last, 1, 500)},
        {id="quests", text="Request /myquests", action="command", command="/myquests"}
    }
    if s.chat_gap then rows[#rows+1]={id="gap",text="Chat overflow: refresh quest status; some messages were missed."} end
    if s.error ~= "" then rows[#rows+1]={id="error",text=string.sub(s.error,1,500)} end
    local markers, nearest, count = {}, nil, 0
    for _, o in ipairs(s.objects) do
        if o.monster and key ~= "" and string.contains(string.lower(o.name), string.lower(key)) then
            count = count + 1
            nearest = nearest or o -- objects are nearest first
            if count <= 24 then
                rows[#rows+1]={id="mob_"..o.id,text=o.name.." - "..math.floor(o.distance).." m",action="select",target=o.id}
                if #markers < 32 then markers[#markers+1]={id=o.id,shape="arrow",color="green"} end
            end
        elseif p.npc ~= "" and string.contains(string.lower(o.name), string.lower(p.npc)) and #markers < 32 then
            markers[#markers+1]={id=o.id,shape="!",color="yellow"}
        end
    end
    if nearest then rows[#rows+1]={id="navigate",text="Point arrow to nearest "..key,action="waypoint",target=nearest.id} end
    -- /plugin kill_tracker reset is local; nothing is sent to server chat.
    for _, e in ipairs(s.events) do
        if e.type == "command" and e.text == "reset" then saved.tasks[key]=nil; changed=true end
    end
    return {status=count.." matching monsters nearby",state=changed and saved or nil,ui=rows,markers=markers}
end
