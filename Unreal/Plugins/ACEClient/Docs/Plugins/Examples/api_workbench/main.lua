-- API 2 example: the stock policy, without the reserved 'ucm' plugin ID.
-- Copy this whole folder into Saved/ClientPlugins/Installed and enable it.
-- All activities initially off; Start deliberately takes gameplay ownership.
local last_result = ''
local pending, next_service = {}, 0
local function checkbox(id, text, value)
  return {id=id,type='checkbox',text=text,value=value}
end
return function(s,p)
  local patch, requests = {}, {}
  for _,e in ipairs(s.events or {}) do
    if e.type=='change' then
      if e.id=='combat' then patch.combat=e.value and 'auto' or 'off'
      elseif e.id=='range' then patch.approach_range=e.value
      elseif e.id=='name' then patch.library_name=e.value
      elseif e.id=='buffing' or e.id=='looting' or e.id=='navigation' or e.id=='recovery' then patch[e.id]=e.value end
    elseif e.type=='click' then
      if e.id=='save' or e.id=='load' then
        requests[#requests+1]={id=e.id,service='profiles',operation=e.id,name=p.library_name or 'Default',group='Setups'}
      elseif e.id=='record' then requests[#requests+1]={id='record',service='route',operation='record',kind='walk'}
      elseif e.id=='force' then requests[#requests+1]={id='force',service='force_buff',operation='start'} end
    elseif e.type=='result' then last_result=e.ok and (e.id..' completed') or e.error end
  end
  for key,value in pairs(patch) do p[key]=value end
  for _,request in ipairs(requests) do
    if #pending<32 then pending[#pending+1]=request end
  end
  requests={}
  if (s.time or 0)>=next_service and #pending>0 then
    for _=1,math.min(4,#pending) do requests[#requests+1]=table.remove(pending,1) end
    next_service=(s.time or 0)+0.6
  end
  -- This is the same module used by UCM. Replace this call to write your own policy.
  local out=builtinpolicy(s,p) or {}
  -- Optional live window/bar title; a PNG can be supplied as icon='workbench.png'.
  out.presentation={title='API Workbench'}
  out.profile_patch=patch
  out.requests=requests
  out.ui={
    {id='title',text='Automation workbench',size=24,bold=true},
    {id='vitals',type='progress',text='Health '..tostring(s.health)..' / '..tostring(s.max_health),value=(s.health or 0)/math.max(1,s.max_health or 1),color='green'},
    {id='pages',type='tabs',children={
      {id='activities',type='column',text='Activities',children={
        checkbox('combat','Combat',p.combat~='off'),checkbox('buffing','Buffing',p.buffing),
        checkbox('recovery','Vital recovery',p.recovery),checkbox('looting','Looting',p.looting),
        checkbox('navigation','Follow route',p.navigation),
        {id='range',type='slider',text='Target acquisition range (metres)',min=1,max=240,step=1,value=p.approach_range or 60},
        {id='quick',type='row',children={{id='force',type='button',text='Force buff'},{id='record',type='button',text='Record walk point'}}},
        {id='target',type='button',text='Select nearest monster',action='select',target=s.nearest or 0,enabled=(s.nearest or 0)~=0}
      }},
      {id='profiles',type='column',text='Profiles',children={
        {id='name',type='input',text='Setup name',value=p.library_name or 'Default'},
        {id='files',type='row',children={{id='save',type='button',text='Save setup'},{id='load',type='button',text='Load setup'}}},
        {id='result',text=last_result}
      }}
    }}
  }
  return out
end
