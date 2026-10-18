-- Copy this folder into Saved/ClientPlugins/Installed; refresh and enable it.
-- Adapt the literals and commands to the server you play on.
local next_poll, next_send, serial, quest_rows = 0, 0, 0, 0
local pending, recent, result = {}, {}, ''
return function(s, p)
  local now, patch, requests = s.time or 0, {}, {}
  for _, e in ipairs(s.events or {}) do
    if e.type == 'change' and e.id == 'poll' then
      p.poll_quests = e.value
      patch.poll_quests = e.value
      next_poll = 0
    elseif e.type == 'click' and #pending < 16 then
      local commands = {quests='/myquests', b='/b', bonus='/bonus', fship='/fship'}
      if commands[e.id] then pending[#pending+1] = commands[e.id] end
    elseif e.type == 'result' then
      result = e.ok and 'Command sent' or (e.error or 'Command rejected')
    end
  end
  for _, e in ipairs(s.chat_events or {}) do
    local raw = e.raw_text or ''
    if e.color == 0 and (e.sender or '') == '' and string.contains(raw:sub(1,4096), ' solves (') then
      quest_rows = quest_rows + 1 -- Replace this counter with your quest parser.
    else
      recent[#recent+1] = (e.text or raw):sub(1,480)
      if #recent > 8 then table.remove(recent,1) end
    end
  end
  if p.poll_quests and now >= next_poll and #pending < 16 then
    pending[#pending+1] = '/myquests'
    next_poll = now + 120
  end
  if #pending > 0 and now >= next_send then
    serial = serial + 1
    requests[1] = {id='send_'..serial, service='chat', operation='send', text=table.remove(pending,1)}
    next_send = now + 1.1
  end
  return {
    -- Installed synchronously before service requests are sent. No first-line flash.
    chat_filters={{id='quests', contains=' solves (', sender='', chat_type=0, window=0}},
    profile_patch=patch, requests=requests,
    ui={
      {id='poll',type='checkbox',text='Read quests every two minutes',value=p.poll_quests == true},
      {id='commands',type='row',children={
        {id='quests',type='button',text='Read quests'},
        {id='b',type='button',text='/b'},
        {id='bonus',type='button',text='/bonus'},
        {id='fship',type='button',text='/fship'}
      }},
      {id='parsed',text='Quest rows read: '..quest_rows},
      {id='result',text=result},
      {id='recent',type='section',text='Recent chat',children={{id='lines',text=table.concat(recent,'\n')}}}
    }
  }
end
