return function(s,p)
    local rows={}
    for _,o in ipairs(s.objects) do
        if not o.dead and (p.filter=="" or string.contains(string.lower(o.name),string.lower(p.filter))) then
            rows[#rows+1]={id=tostring(o.id),text=o.name.." - "..math.floor(o.distance).." m",action="select",target=o.id}
            if #rows==100 then break end
        end
    end
    return {status=#rows.." nearby objects (click to select)",ui=rows}
end
