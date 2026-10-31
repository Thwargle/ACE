local function band(a,b) return math.floor(a or 0)&math.floor(b or 0) end
local function literal_pattern(text,pattern) return regextest(text,pattern) end
local function text_match(text,c)
 local pattern=c.pattern or '';local mode=c.match_mode or 'regex'
 if mode=='regex' then return literal_pattern(text,c.ignore_case and ('(?i:'..pattern..')') or pattern) end
 if c.ignore_case~=false then text=string.lower(text);pattern=string.lower(pattern) end
 if mode=='exact' then return text==pattern end
 if mode=='prefix' then return text:sub(1,#pattern)==pattern end
 if mode=='any' or mode=='all' then
  local found=false;local start=1
  while start<=#pattern do
   local finish=pattern:findplain('|',start) or #pattern+1
   local first,last=start,finish-1;start=finish+1
   while first<=last and pattern:byte(first)<=32 do first=first+1 end
   while last>=first and pattern:byte(last)<=32 do last=last-1 end
   local term=pattern:sub(first,last)
   if term~='' then
    local match=text:findplain(term)~=nil;found=true
    if mode=='any' and match then return true end
    if mode=='all' and not match then return false end
   end
  end
  return mode=='all' and found
 end
 return text:findplain(pattern)~=nil
end
local function hsv(rgb)
 local r,g,b=(rgb>>16)&255,(rgb>>8)&255,rgb&255
 local hi,lo=math.max(r,g,b),math.min(r,g,b);local d=hi-lo;local h=0
 if d>0 then
  if hi==r then h=60*((g-b)/d%6) elseif hi==g then h=60*((b-r)/d+2) else h=60*((r-g)/d+4) end
 end
 return h,hi==0 and 0 or d/hi,hi/255
end
-- Classic profile calculations use the item's innate cantrips, not the player's
-- temporary buffs. The identifiers below are the retail spell/quality mapping.
local innate_bonus={
 [2598]={218103842,2},[2586]={218103842,4},[4661]={218103842,7},[6089]={218103842,10},
 [2604]={28,20},[2592]={28,40},[4667]={28,60},[6095]={28,80},
 [3251]={152,.01},[3250]={152,.03},[4670]={152,.05},[6098]={152,.07},
 [2603]={167772172,.03},[2591]={167772172,.05},[4666]={167772172,.07},[6094]={167772172,.09},
 [2600]={29,.03},[3985]={29,.04},[2588]={29,.05},[4663]={29,.07},[6091]={29,.09},
 [3201]={144,1.05},[3199]={144,1.1},[3202]={144,1.15},[3200]={144,1.2},[6086]={144,1.25},[6087]={144,1.3}
}
local function buffed_quality(item,key,float)
 local raw=(item[float and 'float_properties' or 'int_properties'] or {})[tostring(math.floor(key))]
 if raw==nil then return 0 end
 for _,id in ipairs(item.spell_ids or {}) do local bonus=innate_bonus[id];if bonus and bonus[1]==key then raw=key==144 and raw*bonus[2] or raw+bonus[2] end end
 return raw
end
local function condition_match(c,item,s,scan)
 if c.field=='legacy' then
  local t,a=c.key,c.args
  if t==6 then return false end -- Original Classic rule is retired and always fails.
  if t==7 then return item.object_class==a[1] end
  if t==17 then local palette=(item.palettes or {})[math.floor(a[1])+1];return palette~=nil and band(palette,0xffffff)==band(math.floor(a[2]),0xffffff) end
  if t==14 or t==15 or t==16 then
   local h,sat,v=hsv((a[1]<<16)|(a[2]<<8)|a[3]);local allowed={}
   if t==15 then for _,slot in ipairs(c.slots or {}) do allowed[slot+1]=true end end
   for index,rgb in ipairs(item.palette_colors or {}) do
    if t==14 or (t==16 and index==a[6]+1) or (t==15 and allowed[index]) then
     local h2,s2,v2=hsv(rgb)
     -- Classic compares absolute hue, without wrapping 0/360, and Euclidean S/V distance.
     if math.abs(h2-h)<=a[4] and (s2-sat)^2+(v2-v)^2<=a[5]^2 then return true end
    end
   end
   return false
  end
  if t==1001 then return (s.main_pack_slots or 0)>=a[1] end
  if t==1002 then return (s.level or 0)>=a[1] end
  if t==1003 then return (s.level or 0)<=a[1] end
  if t==1000 then local skill=(s.skills or {})[tostring(math.floor(a[2]))];return skill~=nil and skill.current>=a[1] end
  if t==1004 then local skill=(s.skills or {})[tostring(math.floor(a[1]))];return skill~=nil and skill.base>=a[2] and skill.base<=a[3] end
  if not item.identified then return nil end
  if t==10 then return item.object_class==1 and (item.damage or 0)*(1-(item.variance or 0))>=a[1] end
  if t==2007 then local total=0;for _,id in ipairs({370,371,372,373,374,375,376,379}) do total=total+((item.int_properties or {})[tostring(id)] or 0) end;return total>=a[1] end
  if t==2003 or t==2005 then return buffed_quality(item,a[2],t==2005)>=a[1] end
  if t==2001 then return item.object_class==9 and buffed_quality(item,218103842,false)+(buffed_quality(item,167772174,true)-1)*100/3+buffed_quality(item,204,false)>=a[1] end
  if t==2000 or t==2006 or t==2008 then
   if item.object_class~=1 then return false end
   local variance=(item.float_properties or {})['167772171'] or 0;local damage=buffed_quality(item,218103842,false)
   if t==2000 then return damage*(1-variance/2)>=a[1] end
   local props=item.int_properties or {};local slots=math.max(0,10-(props['171'] or 0))-((props['179'] or 0)==0 and 1 or 0)
   if (props['131'] or 0)==0 then slots=0 end
   -- Expected damage with retail critical chance/multiplier. Reserve one tinker
   -- for an imbue, then prioritize requested defenses before iron/granite.
   local defense,offense=buffed_quality(item,29,true),buffed_quality(item,167772172,true)
   local function dot(d,v) return d*(.9*(2-v)/2+.2) end
   for _=1,math.min(10,slots) do
    if t==2008 and defense<a[2] then defense=defense+.01
    elseif t==2008 and offense<a[3] then offense=offense+.01
    elseif dot(damage+25,variance)>=dot(damage+24,variance*.8) then damage=damage+1
    else variance=variance*.8 end
   end
   return dot(damage+24,variance)>=a[1] and (t~=2008 or (defense>=a[2] and offense>=a[3]))
  end
  error('Unknown legacy loot requirement')
 end
 if c.field=='spells' then
  if not item.identified then return nil end
  local count=scan.spell_count or 0
  local names=item.spell_names or {}
  for index=scan.spell_index or 1,#names do
   if not workavailable() then scan.spell_count=count;scan.spell_index=index;return 'pending' end
   local name=names[index]
   if literal_pattern(name,c.pattern or '') and ((c.exclude or '')=='' or not literal_pattern(name,c.exclude)) then
    count=count+1
    if count>=(c.value or 1) then scan.spell_count=nil;scan.spell_index=nil;return true end
   end
  end
  scan.spell_count=nil;scan.spell_index=nil
  return count>=(c.value or 1)
 end
 local props=item[c.field..'_properties'] or {};local value=props[tostring(math.floor(c.key))]
 if c.field=='string' and c.key==1 then value=value or item.name or '' end
 if value==nil and not item.identified then return nil end
 if c.field=='string' then return text_match(value or '',c) end
 if value==nil then if c.missing_zero then value=0 else return false end end
 if c.op=='in' then for _,v in ipairs(c.values or {}) do if value==v then return true end end;return false end
 local n=c.value or 0
 return (c.op=='ge' and value>=n) or (c.op=='le' and value<=n) or (c.op=='eq' and value==n) or (c.op=='ne' and value~=n) or (c.op=='bits' and (math.floor(value)&math.floor(n))~=0)
end
-- Check public fields before appraising; native rules require missing properties to exist.

local function rule_match(rule,item,s,scan)
 if rule.enabled==false then return false end
 local wanted=rule.name or ''
 if wanted~='' then
  local name=string.lower(item.name or '');local mode=rule.name_mode or 'prefix'
  if mode=='any' or mode=='all' or mode=='regex' then
   if not text_match(item.name or '',{pattern=wanted,match_mode=mode,ignore_case=true}) then return false end
  end
  wanted=string.lower(wanted)
  if mode=='exact' and name~=wanted then return false end
  if mode=='prefix' and name:sub(1,#wanted)~=wanted then return false end
  if mode=='contains' and not name:contains(wanted) then return false end
 end
 if (rule.material or 0)~=0 and item.material~=rule.material then return false end
 if (rule.type or 0)~=0 and band(item.type,rule.type)==0 then return false end
 local missing=false
 for _,field in ipairs({'value','workmanship','burden','rating'}) do
  local lo,hi=rule['min_'..field] or 0,rule['max_'..field] or 0
  if lo>0 or hi>0 then
   local value=item[field=='rating' and 'damage_rating' or field]
   if value==nil then missing=true
   elseif value<lo or (hi>0 and value>hi) then return false end
  end
 end
 local conditions=rule.conditions or {}
 local any=rule.conditions_mode=='any';local unknown=scan.unknown or false;local matched=scan.matched or false
 -- Evaluate cheap properties first. OR can resolve true despite an unknown
 -- alternative; AND can resolve false. Preserve that state across work-budget yields.
 for phase=scan.phase or 1,2 do
 for index=scan.condition or 1,#conditions do
  local c=conditions[index];local expensive=c.field=='spells' or c.field=='string'
  if expensive==(phase==2) then
  if not workavailable() then scan.phase=phase;scan.condition=index;scan.unknown=unknown;scan.matched=matched;return 'pending' end
  local match=condition_match(conditions[index],item,s,scan)
  if match=='pending' then scan.phase=phase;scan.condition=index;scan.unknown=unknown;scan.matched=matched;return 'pending'
  elseif match==false and not any then return false
  elseif match==true and any then matched=true;break
  elseif match==nil then unknown=true end
  end
 end
 scan.condition=nil
 if matched then break end
 end
 if any and #conditions>0 and not matched and not unknown then return false end
 missing=missing or (unknown and not (any and matched))
 if missing then if item.identified then return false else return nil end end
 return true
end
return rule_match
