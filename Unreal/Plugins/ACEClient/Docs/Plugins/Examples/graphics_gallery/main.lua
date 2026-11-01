-- Copy this entire folder into Saved/ClientPlugins/Installed, Refresh and enable.
-- The client animates images between these half-second Lua callbacks.
local finish = 0
return function(s, p)
    local requests = {}
    if finish == 0 then finish = s.time + 30 end
    for _, e in ipairs(s.events or {}) do
        if e.type == "click" and e.id == "restart" then finish = s.time + 30 end
        if e.type == "click" and e.id == "health" then
            requests[#requests + 1] = {id="health_notice", service="notice", operation="show",
                window=0, color="green", text=string.format("Health: %d / %d", s.health, s.max_health)}
        end
    end
    local left = math.max(0, finish - s.time)
    return {requests=requests, ui={
        {id="intro", text="Native graphics • Live health • Countdown", size=18, bold=true},
        {id="effects", type="row", compact=true, children={
            {id="pulse", type="image", asset="sigil.png", width=72, height=72,
                effect="pulse", period=2, min_opacity=0.25, glow=2, glow_color="#49BFFF"},
            {id="spin", type="image", asset="sigil.png", width=72, height=72,
                effect="rotate", period=8, tooltip="Rotation runs every rendered frame."},
            {id="blink", type="image", asset="sigil.png", width=72, height=72,
                effect="blink", period=1, min_opacity=0.15}
        }},
        {id="gauge_frame", type="frame", width=310, height=48, color="#142638E6", children={
            {id="health", type="progress", value=s.health/math.max(1, s.max_health),
                text=string.format("Health  %d / %d", s.health, s.max_health),
                color="#32C87F", align="center", outline=1, clickable=true,
                tooltip="Click to report health in main chat."}
        }},
        {id="timer", type="overlay", width=310, height=48, children={
            {id="timer_fill", type="progress", value=left/30, color="#3C82C8"},
            {id="timer_text", text=string.format("%.1f seconds", left), align="right",
                outline=1, padding=10}
        }},
        {id="restart", type="button", text="Restart countdown", hide_when_pinned=true},
        {id="hint", text="Pin the window to hide Restart; Quick-show restores it.", size=12}
    }}
end
