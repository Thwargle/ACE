-- UCM policy: all effects go through the public host's validated intents.
local state, entered, point, locked_target = nil, 0, 1, nil
local route_direction=1
local route_join_pending,route_join_scan=true,nil
local route_trail,route_walking,route_returning={},false,false
local route_teleport,route_revision,blocked_serial=nil,nil,nil
local route_blocks=0
local navigation_paused,door_attempts=nil,{}
local retries, until_time, corpses, item_attempts = {}, {}, {}, {}
local route_pending, loot_pending, recovery_pending, buff_item_pending = nil, nil, nil, nil
local monster_failures,monster_blacklist,combat_serial={},{},0
local ghost_attempts,ghost_hp={},{ }
local corpse_pending=nil
local mana_refill_pending=nil
local mana_fill_pending,pet_refill_pending=nil,nil
local helper_pending=nil
local debuff_pending=nil
local debuff_scan=nil
local purchase_pending=nil
local note_pending=nil
local pea_pending=nil
local pet_pending=nil
local dispel_pending=nil
local last_cast, unavailable_spells = nil, {}
local forced_buff=nil
local buff_skips={}
local other_job=nil
local loot_scan={}
local inventory_salvage_scan={}
local inventory_salvage_blocked={}
local loot_jobs={}
local loot_job_pending=nil
local combine_pending=nil
-- An operational failure belongs to one activity, never the whole scheduler.
-- Cooldowns prevent a failing high-priority activity starving combat/recovery.
local activity,activity_time,activity_pauses='meta',0,{}
local function activity_ready(name)
 local until_at=activity_pauses[name]
 if until_at and activity_time<until_at then return false end
 activity_pauses[name]=nil;activity=name;return true
end
local function failed(reason)
 return {action='activity_failed',activity=activity,status=reason}
end
local follow_path,follow_target,follow_teleport={},nil,nil
local skill_categories = {[17]=45,[19]=47,[21]=3,[23]=46,[25]=5,[27]=9,[29]=10,[31]=44,[33]=12,[35]=13,
 [37]=6,[39]=7,[41]=15,[43]=31,[45]=32,[47]=33,[49]=34,[51]=16,[53]=14,[55]=29,[57]=18,[59]=30,[61]=28,
 [63]=27,[65]=20,[67]=21,[69]=22,[71]=35,[73]=23,[75]=36,[77]=24,[205]=19,[216]=39,[218]=37,[221]=38,
 [593]=41,[645]=43,[665]=52,[668]=49,[671]=50,[674]=48,[677]=51,[696]=54}
-- VT BuffController starts Creature mastery, Focus, Self/Willpower,
-- Mana Conversion and Life mastery. Finish the other magic schools before
-- ordinary buffs; category IDs preserve this order across every spell tier.
local casting_buff_order={43,9,11,51,47,45,49,645}
local casting_buff_family={};for _,category in ipairs(casting_buff_order) do casting_buff_family[category]=true end
local elements={32,4,16,64,8,2,1}
local combat_school_skill={[1]=34,[2]=33,[3]=32,[4]=31,[5]=43}
local function trained_skill(s,id)
 local skill=s.skills and s.skills[tostring(math.floor(id or 0))]
 return skill and (skill.training or 0)>=2 and (skill.current or 0) or 0
end
local function pct(v,m) return m>0 and v*100/m or 0 end
local function result(a,status) return {action=a,status=status} end
local function pause_navigation(reason)
 navigation_paused='Navigation paused: '..reason..'. UCM remains active; enable Follow route to retry'
 route_pending=nil;route_trail={};route_returning=false;route_walking=false
 route_join_pending=true;route_join_scan=nil
 return result('pause_navigation',navigation_paused)
end
local function contains(list,id) for _,v in ipairs(list or {}) do if v==id then return true end end return false end
local function vt_string(value) if type(value)=='number' then return string.format('%.15g',value) end;return tostring(value) end
local function allowed(list,id) return not list or #list==0 or contains(list,id) end
local function band(a,b) return (a or 0)&b end
local function salvage_candidate(item)
 local material=item.material or 0
 return not item.equipped and not item.retained and item.resource_available~=false
  and item.object_class~=39 and item.object_class~=40 and item.object_class~=10
  and item.object_class~=38 and material>0 and material<=77
  and material~=3 and material~=9 and material~=56 and material~=65 and material~=72
end
local function salvage_protected(item)
 return ((item.int_properties or {})['171'] or 0)>0 or ((item.string_properties or {})['8'] or '')~=''
end
local function world(v) local lb=math.floor(v.cell/65536);return -(math.floor(lb/256)*192+v.x),(lb%256)*192+v.y,v.z end
local function distance(a,b) local x,y,z=world(a);local X,Y,Z=world(b);return math.sqrt((x-X)^2+(y-Y)^2+(z-Z)^2) end
local function element(spell)
 local c=spell.category
 if c>=117 and c<=130 then return elements[(c-117)%7+1] end
 if c>=243 and c<=249 then return elements[c-242] end
 if c>=222 and c<=228 then return elements[c-221] end
 if c==640 or c==639 or c==636 or c==637 or c==638 or c==641 then return 128 end
 return 0
end
local function resistance(t,damage) return t and t.resists and t.resists[tostring(math.floor(damage))] or 1 end
local function cast(s,v,target,status)
 last_cast={id=v.id,category=v.category,target=target,serial=s.action_serial or 0}
 return {action='cast',spell=v.id,target=target,status=status}
end
-- Retail portal.dat puts Heal Self (including Adja's Intervention) in category
-- 67, also used by the timed Healing skill enchantment. Duration separates them.
-- Conversion categories identify the consumed vital, not the restored vital;
-- tier VII has distinct names and tier VIII adds "Incantation of".
local conversion_aliases={['Meditative Trance']='Stamina to Mana',['Rushed Recovery']='Stamina to Health',
 ['Energize Vitality']='Mana to Health',['Energize Vigor']='Mana to Stamina',['Cannibalize']='Health to Mana',['Self Sacrifice']='Health to Stamina'}
local vital_ids={Health=2,Stamina=4,Mana=6}
local function recovery_spell(v)
 if not v.caster_target or (v.duration or 0)~=0 then return end
 local c=v.category
 if c==67 or c==79 or c==257 then return 2,0 end
 if c==81 or c==407 then return 4,0 end
 if c==83 or c==259 then return 6,0 end
 local name=conversion_aliases[v.name] or v.name or ''
 if name:sub(1,15)=='Incantation of ' then name=name:sub(16) end
 for source,from in pairs(vital_ids) do for destination,to in pairs(vital_ids) do if from~=to then
  local prefix=source..' to '..destination
  if name==prefix or name:sub(1,#prefix+1)==prefix..' ' then return to,from end
 end end end
end
-- Host regex matching has bounded input, stack, match time and per-tick work.
local function literal_pattern(text,pattern) return regextest(text,pattern) end
local catalog_snapshot,catalog_margin,catalog_by_id,catalog_supplied,catalog_usable
local function index_spells(s,margin)
 if catalog_snapshot~=s or (margin and catalog_margin~=margin) then
  catalog_snapshot=s;catalog_margin=margin or 30
  catalog_by_id,catalog_supplied,catalog_usable=spellindex(s.spells or {},s.inventory or {},s.time,catalog_margin,unavailable_spells,s.components_required~=false)
 end
 return catalog_by_id,catalog_usable
end
local function spell_supplied(s,spell)
 index_spells(s)
 return catalog_supplied[spell.id]==true
end
local function buff_element_enabled(p,spell)
 local c=spell.category;local letter,kind
 if c>=101 and c<=113 and c%2==1 then letter=({'A','B','C','L','F','P','S'})[(c-101)//2+1];kind='protection'
 elseif c>=162 and c<=174 and c%2==0 then letter=({'A','B','C','L','F','P','S'})[(c-162)//2+1];kind='bane'
 elseif c>=176 and c<=188 and c%2==0 then letter=({'B','S','P','L','C','F','A'})[(c-176)//2+1];kind='bane' end
 if not kind then return true end
 local profiles=kind=='protection' and {'CUSTOM','ALL','NONE','B','BPS','BPSA','ALFC','BPSAC'} or {'CUSTOM','ALL','NONE','B','BPS','BPSA','BPSAC','ALFC'}
 local selection=profiles[p[kind..'_profile'] or 2] or 'ALL'
 if selection=='CUSTOM' then selection=string.upper(p[kind..'_custom'] or 'ALL') end
 return selection=='ALL' or (selection~='NONE' and selection:contains(letter))
end
-- Zero means automatic; explicit tiers use the retail formula level, not
-- difficulty (special spells can have different difficulty at the same tier).
local function stronger_spell(v,other)
 if not other then return true end
 if (v.level or 0)~=(other.level or 0) then return (v.level or 0)>(other.level or 0) end
 return v.power>other.power
end
local function buff_tier(v,p)
 local key=v.school==4 and 'creature_buff_level' or v.school==2 and 'life_buff_level' or v.school==3 and 'item_buff_level'
 local level=key and (p[key] or 0) or 0
 return level==0 or v.level==level
end
local function buff_skipped(s,target,category)
 local key=tostring(target)..':'..category
 return s.time<(buff_skips[key] or 0) or (forced_buff and forced_buff.skipped and forced_buff.skipped[key])
end
-- A side-effect-free buff plan is shared by NeedBuff and the casting policy.
local function compute_buff(s,p)
 local trained={};for _,id in ipairs(s.trained_skills or {}) do trained[id]=true end
 for _,id in ipairs(s.usable_skills or {}) do trained[id]=true end
 -- Index selections once. Scanning long imported buff/exclusion lists for every
 -- spell made a fully learned spellbook exhaust the sandbox's tick budget.
 local selected,excluded={},{}
 for _,id in ipairs(p.buffs or {}) do selected[id]=true end
 for _,id in ipairs(p.excluded_buffs or {}) do excluded[id]=true end
 local excluded_ids={};for _,id in ipairs(p.excluded_buff_spells or {}) do excluded_ids[id]=true end
 local families={}
 if next(selected) or next(excluded_ids) then for _,v in ipairs(s.spells or {}) do
  if excluded_ids[v.id] then excluded[v.category]=true end
  if selected[v.id] then families[v.category]=true end
 end end
 local by_id=index_spells(s)
 local best,order,blocked,active,confirmed,item_best,item_blocked={},{},{},{},{},{},{};local refresh=math.max(10,p.refresh_seconds or 60)
 local eligibility={}
 local item_targets=p.item_buff_targets and #p.item_buff_targets>0
 local margin=p.buff_skill_margin or p.skill_margin or 30
 local tiers={[2]=p.life_buff_level or 0,[3]=p.item_buff_level or 0,[4]=p.creature_buff_level or 0}
 for _,v in ipairs(s.spells or {}) do
  if item_targets and v.known~=false and v.beneficial and v.school==3 and (v.duration or 0)>0 and (v.skill or 0)>=v.power+margin and buff_tier(v,p) and not excluded[v.category] then
   if not item_best[v.category] or item_best[v.category].power<v.power then
    if s.time<(unavailable_spells[v.id] or 0) or not catalog_supplied[v.id] then item_blocked[v.category]=true
    else item_best[v.category]=v end
   end
  end
  if v.known~=false and (v.self_buff or (v.beneficial and v.school==3 and (v.duration or 0)>0
   and (v.caster_target or band(v.target_type,0x8107)~=0))) then
  local eligible=eligibility[v.category]
  if eligible==nil then
   local skill=skill_categories[v.category];eligible=((p.auto_buffs~=false and (not skill or trained[skill]) and buff_element_enabled(p,v)) or families[v.category])==true
   eligibility[v.category]=eligible
  end
  -- Automatic maintenance uses sustained buffs. Burst spells such as Tusker
  -- Leap (10 seconds) are only included when explicitly selected.
  if eligible and ((v.duration or 300)>=300 or selected[v.id]) and not excluded[v.category] and (not best[v.category] or v.power>best[v.category].power) then
   local tier=tiers[v.school] or 0
   if (tier~=0 and v.level~=tier) or (v.skill or 0)<v.power+margin
    or s.time<(unavailable_spells[v.id] or 0) or not catalog_supplied[v.id] then blocked[v.category]=v
   else
    if not best[v.category] then order[#order+1]=v.category end
    best[v.category]=v
   end
  end
  end
 end
 -- Order only the chosen families, not the entire spellbook. Keep the
 -- remaining families stable and preserve exclusions/skill/component checks.
 local ordered={}
 for _,category in ipairs(casting_buff_order) do if best[category] then ordered[#ordered+1]=category end end
 for _,category in ipairs(order) do if not casting_buff_family[category] then ordered[#ordered+1]=category end end
 order=ordered
 -- A configured refresh window must never make a freshly cast short buff stale.
 local function refresh_window(duration) return duration and duration>0 and math.min(refresh,duration*.5) or refresh end
 for _,e in ipairs(s.enchantments or {}) do local v=by_id[e.id] or best[e.category]
  if e.remaining>refresh_window(v and v.duration) then active[e.category]=math.max(active[e.category] or 0,e.power) end
 end
 for _,e in ipairs(s.item_buffs or {}) do local v=best[e.category] or item_best[e.category]
  if e.expires-s.time>refresh_window(e.duration or (v and v.duration)) then local k=tostring(e.target)..':'..e.category;confirmed[k]=math.max(confirmed[k] or 0,e.power) end
 end
 -- Retail equipment-compatible item spells accept the player. Banes/impen
 -- spread to worn armor, and weapon auras apply to the character. Do not cast
 -- the same family on every carried weapon or armor piece. Other-player banes
 -- intentionally retain their separate equipment targeting below.
 local id=s.player or 0
 for _,category in ipairs(order) do local v=best[category]
  if not buff_skipped(s,id,category) then
  local refreshed=forced_buff and (forced_buff.done[tostring(id)..':'..v.category] or -1)>=v.power
  if forced_buff and not refreshed then return v,id end
  if not forced_buff and not ((active[v.category] or -1)>=v.power or (confirmed[tostring(id)..':'..v.category] or -1)>=v.power) then return v,id end
  end
 end
 local owned,usable_names,current_weapon={},{},nil
 for _,v in ipairs(s.inventory or {}) do owned[v.id]=v;if v.usable and v.name then usable_names[v.name]=v end;if v.equipped and band(v.type,0x8101)~=0 then current_weapon=v end end
 for _,binding in ipairs(p.item_buff_targets or {}) do
  local item=binding.item==0 and current_weapon or owned[binding.item];local exemplar=by_id[binding.spell]
  local v=exemplar and item_best[exemplar.category]
  if item and v then
   -- Keep UCM's player-targeted armor/weapon auras. Explicit non-redirectable
   -- item enchantments (for example a jewelry target) retain their own target.
   local target=(v.caster_target or band(v.target_type,0x8107)~=0) and id or item.id
   if (target==id or band(v.target_type,item.type or 0)~=0) and not buff_skipped(s,target,v.category) then
    local key=tostring(target)..':'..v.category
    if forced_buff and (forced_buff.done[key] or -1)<v.power then return v,target end
    if not forced_buff and (confirmed[key] or -1)<v.power and (target~=id or (active[v.category] or -1)<v.power) then return v,target end
   end
  elseif item and exemplar and item_blocked[exemplar.category] then blocked[exemplar.category]=exemplar
  end
 end
 local incomplete=false
 for category,v in pairs(blocked) do if not best[category] then
  if forced_buff and not forced_buff.done[tostring(id)..':'..category] or (not forced_buff and not (active[category] or confirmed[tostring(id)..':'..category])) then incomplete=true end
 end end
 for _,expires in pairs(buff_skips) do if s.time<expires then incomplete=true;break end end
 for _,b in ipairs(p.buff_items or {}) do
  local v=by_id[b.spell]
  if v and (band(v.flags,0x2000)==0 or s.in_fellowship) and (active[v.category] or -1)<v.power then
   if usable_names[b.name] and (item_attempts['buffitem'..b.name] or 0)<3 then return nil,nil,incomplete,true end
  end
 end
 return nil,nil,incomplete
end
-- NeedBuff may occur in many rules in the same meta tick. Reuse the plan only
-- within that snapshot and identical options; never cache server confirmations
-- or inventory across ticks.
local buff_plan
local function next_buff(s,p)
 local keys={'auto_buffs','buffs','excluded_buffs','excluded_buff_spells','item_buff_targets','buff_items','skill_margin','buff_skill_margin','refresh_seconds','creature_buff_level','life_buff_level','item_buff_level'}
 local same=buff_plan and buff_plan.snapshot==s
 if same then for _,k in ipairs(keys) do if buff_plan.options[k]~=p[k] then same=false;break end end end
 if not same then
  local v,id,blocked,consumable=compute_buff(s,p)
  local options={};for _,k in ipairs(keys) do options[k]=p[k] end
  buff_plan={snapshot=s,options=options,spell=v,target=id,blocked=blocked,consumable=consumable}
 end
 return buff_plan.spell,buff_plan.target,buff_plan.blocked,buff_plan.consumable
end
-- Legacy meta state is session-local; imported options never overwrite saved profiles.
local meta={name='Default',fired={},stack={},options={},route_revision=0,queue={}}
local function meta_state(name,s)
 meta.name=name;meta.entered=s.time;meta.persistent_entered=s.time;meta.fired={};meta.watchdog=nil
 meta.chat_start=s.chat_serial or 0;meta.portal_start=s.portal_serial or 0
end
local meta_expression
local function meta_command(c,s)
 if c.op=='stop' then return 'Stopped by UCM command'
 elseif c.op=='start' or c.op=='noop' then return -- Already executing within an explicitly started run.
 elseif c.op=='option' then meta.options[c.key]=c.value
 elseif c.op=='reverse_route' then route_direction=-route_direction;meta.notice='Nav backwards is: '..tostring(route_direction<0)
 elseif c.op=='reverse_query' then meta.notice='Nav backwards is: '..tostring(route_direction<0)
 elseif c.op=='state' then meta_state(c.state,s)
 elseif c.op=='expression' then meta_expression(c.expression,s)
 elseif c.op=='forcebuff' then
  meta.forcebuff=c.value
  if c.value then if not forced_buff then forced_buff={request=0,done={}};buff_plan=nil end
  elseif forced_buff and forced_buff.request==0 then forced_buff=nil;buff_plan=nil end
 elseif c.op=='echo' then meta.notice=c.text
 elseif c.op=='use' or c.op=='say' or c.op=='give' or c.op=='select' or c.op=='use_selected' or c.op=='combat_mode' or c.op=='logout' or c.op=='jump' or c.op=='load' or c.op=='recall' or c.op=='face' or c.op=='fellowship' or c.op=='confirm' or c.op=='attack_bar' then
  if #meta.queue>=64 then return 'Meta command queue exceeds 64 actions' end
  meta.queue[#meta.queue+1]=c
 else return 'Unsupported meta command' end
end
local meta_vars={}
local object_refs,object_snapshot={},nil
local meta_active_profile={}
local species_names={}
local function truth(v) return v~=nil and v~=false and v~=0 and v~='' end
-- Lists are reference objects, with zero-based public indexes. Keep their
-- identity and reject cycles before insertion, including indirect cycles.
local function list_value(items) return {vt_list=true,items=items or {}} end
local function check_list(v)
 if type(v)~='table' or not v.vt_list then error('Expected meta list') end
 return v.items
end
local function list_cycle(value,target,seen,depth)
 if value==target then return true end
 if type(value)~='table' or not value.vt_list then return false end
 if depth>32 then error('Meta list nesting exceeds 32') end
 if seen[value] then return false end;seen[value]=true
 for _,child in ipairs(value.items) do if list_cycle(child,target,seen,depth+1) then return true end end
 return false
end
meta_expression=function(e,s)
 local op=e.op
 if op=='literal' then return e.value end
 if op=='monster_var' then
  local t=s.monster or {};local n=e.name
  if n=='name' then return t.name or '' elseif n=='typeid' then return t.wcid or 0
  elseif n=='range' then return t.distance or 0 elseif n=='metastate' then return meta.name end
  if n=='hasshield' then return t.has_shield and 1 or 0 end
  local catalog=((s.profile or {}).monster_catalog or {})[t.name or ''] or {}
  if n=='maxhp' then return (t.max_health and t.max_health>0) and t.max_health or catalog.max_health or -1 end
  if n=='species' then
   local id=(t.creature_type and t.creature_type>0) and t.creature_type or catalog.species
   return id and species_names[tostring(math.floor(id))] or ''
  end
  error('Unknown monster variable: '..tostring(n))
 end
 if op=='call' then
  local args={};for i,a in ipairs(e.args) do args[i]=meta_expression(a,s) end
  local n=e.name;local key=vt_string(args[1] or '')
  if n=='isfalse' then return args[1]==0 or args[1]==false end
  if n=='istrue' then return args[1]==true or type(args[1])=='number' and args[1]~=0 end
  if n=='iif' then if args[1]==true or type(args[1])=='number' and args[1]~=0 then return args[2] end;return args[3] end
  if n=='strlen' then
   if type(args[1])~='string' then error('strlen requires a string') end
   local length=0;for _,code in utf8.codes(args[1]) do length=length+(code>0xffff and 2 or 1) end;return length
  end
  if n=='cnumber' then
   if type(args[1])~='string' then error('cnumber requires a string') end
   -- The legacy parser accepts decimal grouping and exponents, not Lua hex.
   local chars={};for i=1,#args[1] do local char=args[1]:sub(i,i)
    if char=='x' or char=='X' or char=='p' or char=='P' then return 0 end
    if char~=',' then chars[#chars+1]=char end
   end
   local text=table.concat(chars)
   local number=tonumber(text);return number and number==number and math.abs(number)<math.huge and number or 0
  end
  if n=='floor' or n=='ceiling' or n=='round' or n=='abs' or n=='randint' then
   local value=type(args[1])=='boolean' and (args[1] and 1 or 0) or args[1]
   if type(value)~='number' or value~=value or math.abs(value)==math.huge then error(n..' requires a finite number') end
   if n=='floor' then return math.floor(value) elseif n=='ceiling' then return math.ceil(value) elseif n=='abs' then return math.abs(value) end
   if n=='round' then
    local lower=math.floor(value);local fraction=value-lower
    if fraction==.5 then return lower%2==0 and lower or lower+1 end -- .NET midpoint-to-even.
    return fraction<.5 and lower or lower+1
   end
   local upper=type(args[2])=='boolean' and (args[2] and 1 or 0) or args[2]
   if type(upper)~='number' or upper~=upper or value< -2147483648 or value>2147483647 or upper< -2147483648 or upper>2147483647 then error('randint requires 32-bit bounds') end
   value=math.modf(value);upper=math.modf(upper)
   if value>upper then error('randint minimum exceeds maximum') end
   return value==upper and value or math.random(value,upper-1)
  end
  if n=='listcreate' then return list_value(args) end
  if n:sub(1,4)=='list' then
   local values=check_list(args[1]);local count=#values
   local function index(v,insert)
    if type(v)~='number' or v%1~=0 or v<0 or v>=count+(insert and 1 or 0) then error('Meta list index out of range') end
    return v+1
   end
   if n=='listcount' then return count end
   if n=='listgetitem' then return values[index(args[2])] end
   if n=='listclear' then args[1].items={};return args[1] end
   if n=='listcopy' or n=='listreverse' then local copy={};for i=1,count do copy[i]=values[n=='listreverse' and count-i+1 or i] end;return list_value(copy) end
   if n=='listadd' or n=='listinsert' then
    if count>=1024 then error('Meta list exceeds 1024 items') end
    if list_cycle(args[2],args[1],{},0) then error('Meta lists cannot contain themselves') end
    table.insert(values,n=='listinsert' and index(args[3],true) or count+1,args[2]);return args[1]
   end
   if n=='listremoveat' then table.remove(values,index(args[2]));return args[1] end
   local found=-1
   for i,v in ipairs(values) do if v==args[2] then found=i-1;if n~='listlastindexof' then break end end end
   if n=='listcontains' then return found>=0 and 1 or 0 end
   if n=='listindexof' or n=='listlastindexof' then return found end
   if n=='listremove' then if found>=0 then table.remove(values,found+1) end;return args[1] end
   error('Unsupported list operation: '..n)
  end
  if n=='getworldname' then return s.world_name or '' end
  if n:sub(1,9)=='getfellow' then
   local f=s.fellowship or {};local members=f.members or {};local active=f.valid==true
   local leader=active and f.leader==s.player
   if n=='getfellowshipstatus' then return active and 1 or 0 end
   if n=='getfellowshipname' then return active and f.name or '' end
   if n=='getfellowshipcount' then return active and #members or 0 end
   if n=='getfellowshipleaderid' then return active and f.leader or 0 end
   if n=='getfellowshiplocked' then return active and f.locked and 1 or 0 end
   if n=='getfellowshipisleader' then return leader and 1 or 0 end
   if n=='getfellowshipisopen' then return active and f.open and 1 or 0 end
   if n=='getfellowshipisfull' then return active and #members>=9 and 1 or 0 end
   if n=='getfellowshipcanrecruit' then return active and #members<9 and (leader or f.open) and 1 or 0 end
   if n=='getfellownames' or n=='getfellowids' then local values={};if active then for _,m in ipairs(members) do values[#values+1]=n=='getfellownames' and m.name or m.id end end;table.sort(values);return list_value(values) end
  end
  if n=='getvar' then if meta_vars[key]==nil then return 0 end;return meta_vars[key] end
  if n=='testvar' then return meta_vars[key]~=nil end
  if n=='clearallvars' then meta_vars={};return true end
  if n=='clearvar' then meta_vars[key]=nil;return true end
  if n=='setvar' or n=='touchvar' then
   if n=='touchvar' and meta_vars[key]~=nil then return 1 end
   if #key>256 then error('Meta variable name exceeds 256 bytes') end
   local count=0;for _ in pairs(meta_vars) do count=count+1 end
   if count>=256 and meta_vars[key]==nil then error('Meta exceeds 256 variables') end
   meta_vars[key]=n=='touchvar' and 0 or args[2];return meta_vars[key]
  end
  if n=='getobjectinternaltype' then local t=type(args[1]);return t=='table' and 7 or t=='string' and 3 or (t=='number' or t=='boolean') and 1 or 0 end
  if n=='cstr' then local v=args[1];if type(v)=='boolean' then v=v and 1 or 0 end;if type(v)~='number' then error('cstr requires a number') end;return vt_string(v) end
  if n=='getisspellknown' then
   if type(args[1])~='number' then error('getisspellknown requires a spell ID') end
   if s.known_spells then return contains(s.known_spells,args[1]) end
   for _,spell in ipairs(s.spells or {}) do if spell.id==args[1] then return spell.known~=false end end
   return false
  end
  if n=='cstrf' then
   local format=tostring(args[2]);local digits=tonumber(format:sub(2));if format:sub(1,1)~='F' or not digits or digits<0 or digits>9 then error('Only F0 through F9 numeric formatting is supported') end
   return string.format('%.'..digits..'f',args[1])
  end
  local function object(id)
   if type(id)=='table' then if not id.vt_object then return nil end;id=id.id end
   if id==s.player then return {id=id,name=(s.char_strings or {})['1'] or '',object_class=24,cell=s.position.cell,x=s.position.x,y=s.position.y,z=s.position.z} end
   for _,list in ipairs({s.inventory or {},s.route_objects or {},s.targets or {},s.corpses or {}}) do for _,o in ipairs(list) do if o.id==id then return o end end end
  end
  if n=='getcharintprop' or n=='getcharquadprop' or n=='getchardoubleprop' or n=='getcharboolprop' or n=='getcharstringprop' then
   local id=args[1];if type(id)~='number' or id~=id or id< -2147483648 or id>2147483647 then error('Character property requires a numeric 32-bit ID') end
   id=id<0 and math.ceil(id) or math.floor(id)
   local field=({getcharintprop='char_ints',getcharquadprop='char_quads',getchardoubleprop='char_doubles',getcharboolprop='char_bools',getcharstringprop='char_strings'})[n]
   local values=s[field]
   return (values or {})[string.format('%.0f',id)] or 0
  end
  if n:sub(1,13)=='getcharskill_' then
   if type(args[1])~='number' then error('Skill query requires a numeric skill ID') end
   local skill=(s.skills or {})[key] or {};return skill[n=='getcharskill_base' and 'base' or n=='getcharskill_buffed' and 'current' or 'training'] or 0
  end
  if n:sub(1,13)=='getcharvital_' then
   if type(args[1])~='number' then error('Vital query requires a numeric vital ID') end
   local vital=({'health','stamina','mana'})[math.floor(args[1])]
   return math.max(1,vital and s[(n=='getcharvital_base' and 'base_' or n=='getcharvital_buffedmax' and 'max_' or '')..vital] or 1)
  end
  local function object_ref(o)
   if object_snapshot~=s then
    object_snapshot=s;local present={[s.player or 0]=true}
    for _,list in ipairs({s.inventory or {},s.route_objects or {},s.targets or {},s.corpses or {}}) do for _,v in ipairs(list) do present[v.id]=true end end
    for id in pairs(object_refs) do if not present[id] then object_refs[id]=nil end end
   end
   if not o then return 0 end
   object_refs[o.id]=object_refs[o.id] or {vt_object=true,id=o.id};return object_refs[o.id]
  end
  if n=='getplayerlandcell' then return s.position.cell end
  if n=='wobjectgetselection' then return object_ref(object(s.selected)) end
  if n=='wobjectgetplayer' then return object_ref(object(s.player)) end
  if n=='wobjectgetobjectclass' or n=='wobjectgettemplatetype' then
   local o=object(args[1]);if not o then error('World object is unavailable') end
   return o[n=='wobjectgetobjectclass' and 'object_class' or 'wcid'] or 0
  end
  if n=='wobjectfindininventorybytemplatetype' then
   for _,o in ipairs(s.inventory or {}) do if o.wcid==args[1] then return object_ref(o) end end;return 0
  end
  if n=='actiontryselect' then
   if not object(args[1]) then return 0 end
   meta.queue[#meta.queue+1]={op='select',item_id=object(args[1]).id};return 1
  end
  if n=='actiontryequipanywand' or n=='actiontrycastbyid' or n=='actiontrycastbyidontarget' or n=='getcancastspell_hunt' or n=='getcancastspell_buff' then
   local p=meta_active_profile;local spell
   local function setting(k,fallback) local v=meta.options[k];if v==nil then v=p[k] end;if v==nil then return fallback end;return v end
   if n~='actiontryequipanywand' then
    if type(args[1])~='number' then error('Spell expression requires numeric spell ID') end
    for _,v in ipairs(s.spells or {}) do if v.id==args[1] then spell=v;break end end
    local eligible=spell and spell.known~=false and spell.skill>=spell.power+setting(n=='getcancastspell_buff' and 'buff_skill_margin' or 'skill_margin',n=='getcancastspell_buff' and 5 or 30)
    eligible=eligible and spell_supplied(s,spell)
    if n=='getcancastspell_hunt' or n=='getcancastspell_buff' then return eligible and 1 or 0 end
    local untargeted=spell and (spell.caster_target or spell.target_type==0)
    if not eligible or (n=='actiontrycastbyid' and not untargeted) or (n=='actiontrycastbyidontarget' and (untargeted or not object(args[2]))) then return 2 end
   end
   if s.busy or s.ready==false or meta.pending or #meta.queue>0 or s.jumping then return 0 end
   local wand,unknown
   for _,v in ipairs(s.inventory or {}) do if band(v.type,0x8000)~=0 then
    if v.equipped then wand=v;break end
    if allowed(p.weapons,v.wcid) and allowed(p.weapon_items,v.id) then
     if v.identified and v.can_wield then wand=wand or v elseif not v.identified then unknown=unknown or v end
    end
   end end
   if not wand then if unknown then meta.queue[#meta.queue+1]={op='identify',item_id=unknown.id} end;return 0 end
   if not wand.equipped then meta.queue[#meta.queue+1]={op='equip',item_id=wand.id};return 0 end
   if s.combat_mode~=8 then meta.queue[#meta.queue+1]={op='combat_mode',mode=8};return 0 end
   if n=='actiontryequipanywand' then return 1 end
   meta.queue[#meta.queue+1]={op='cast',spell=spell.id,target_id=n=='actiontrycastbyid' and s.player or object(args[2]).id};return 1
  end
  if n=='wobjectfindininventorybyname' then for _,o in ipairs(s.inventory or {}) do if string.lower(o.name)==string.lower(key) then return object_ref(o) end end;return 0 end
  if n=='wobjectfindininventorybynamerx' then
   if type(args[1])~='string' then error('Inventory name regex must be a string') end
   for _,o in ipairs(s.inventory or {}) do if literal_pattern(o.name or '',args[1]) then return object_ref(o) end end;return 0
  end
  if n=='wobjectfindnearestmonster' then
   local best;for _,o in ipairs(s.targets or {}) do
    if s.time>=(monster_blacklist[o.id] or 0) and (not best or o.distance<best.distance) then best=o end
   end;return object_ref(best)
  end
  if n=='wobjectfindnearestdoor' or n=='wobjectfindnearestbyobjectclass' or n=='wobjectfindnearestbynameandobjectclass' then
   local cls=n=='wobjectfindnearestdoor' and 26 or args[1];local best
   if type(cls)~='number' or (n=='wobjectfindnearestbynameandobjectclass' and type(args[2])~='string') then error('World lookup requires a numeric class and string pattern') end
   for _,list in ipairs({s.route_objects or {},s.targets or {},s.corpses or {}}) do for _,o in ipairs(list) do
    if o.id~=s.player and o.object_class==cls and (n~='wobjectfindnearestbynameandobjectclass' or literal_pattern(o.name or '',args[2])) and (not best or o.distance<best.distance) then best=o end
   end end
   return object_ref(best)
  end
  if n=='wobjectgetname' then local o=object(args[1]);return o and o.name or '' end
  if n=='wobjectgetisdooropen' then local o=object(args[1]);return o and o.door_open or false end
  if n=='getplayercoordinates' then return s.position end
  if n=='wobjectgetphysicscoordinates' then local o=object(args[1]);if not o or not o.cell then error('Object position is unavailable') end;return o end
  if n=='coordinateparse' then
   if type(args[1])~='string' then error('coordinateparse requires text') end
   local groups=regexmatch(args[1],'([0-9]+(?:[.][0-9]+)?)\\s*([nNsS])[\\s,]*([0-9]+(?:[.][0-9]+)?)\\s*([eEwW])')
   if not groups then return 0 end
   local ns,a,ew,b=groups['1'],groups['2'],groups['3'],groups['4']
   ns,ew=tonumber(ns),tonumber(ew);if ns>1000 or ew>1000 then return 0 end
   if a:lower()=='s' then ns=-ns end;if b:lower()=='w' then ew=-ew end
   -- A synthetic landblock zero coordinate preserves signed global locations.
   return {cell=0,x=24468+ew*240,y=24468+ns*240,z=0}
  end
  if n=='coordinategetns' or n=='coordinategetwe' or n=='coordinategetz' then
   local x,y,z=world(args[1]);return n=='coordinategetns' and (y-24468)/240 or n=='coordinategetwe' and (-x-24468)/240 or z/240
  end
  if n=='coordinatedistancewithz' then return distance(args[1],args[2]) end
  if n=='coordinatedistanceflat' then local x,y=world(args[1]);local X,Y=world(args[2]);return math.sqrt((x-X)^2+(y-Y)^2) end
  if n=='coordinatetostring' then local x,y,z=world(args[1]);return string.format('%.4f%s, %.4f%s, %.4fZ',math.abs((y-24468)/240),y>=24468 and 'N' or 'S',math.abs((-x-24468)/240),-x>=24468 and 'E' or 'W',z/240) end
  if n=='actiontryuseitem' or n=='actiontryapplyitem' or n=='actiontrygiveitem' then
   local item=object(args[1]);local target=object(args[2]);if not item or (n~='actiontryuseitem' and not target) or s.busy or s.ready==false or meta.pending or #meta.queue>0 then return false end
   if n=='actiontrygiveitem' then local owned=false;for _,i in ipairs(s.inventory or {}) do if i.id==item.id and not i.equipped then owned=true;break end end;if not owned then return false end end
   meta.queue[#meta.queue+1]={op=n=='actiontrygiveitem' and 'give' or 'use',item_id=item.id,target_id=target and target.id};return true
  end
  if n=='stopwatchcreate' then return {stopwatch=true,elapsed=0} end
  local w=args[1]
  if type(w)~='table' or not w.stopwatch then error('Expected stopwatch') end
  if n=='stopwatchstart' then w.started=w.started or s.time;return w end
  if n=='stopwatchstop' then if w.started then w.elapsed=w.elapsed+s.time-w.started;w.started=nil end;return w end
  if n=='stopwatchelapsedseconds' then return w.elapsed+(w.started and s.time-w.started or 0) end
  error('Unsupported expression function: '..n)
 end
 local a=meta_expression(e.left,s)
 if op=='not' then return not truth(a) end
 if op=='negative' then return -a end
 -- VT evaluates both operands, including function side effects.
 local b=meta_expression(e.right,s)
 if op==';' then return a end
 if type(a)=='boolean' then a=a and 1 or 0 end
 if type(b)=='boolean' then b=b and 1 or 0 end
 if type(a)~=type(b) then error('Meta operator requires operands of the same type') end
 if type(a)=='table' then error('Meta operators do not accept internal objects; use object queries') end
 if type(a)=='string' then
  if op=='&&' or op=='||' or op=='^' then error('Meta logical operators require numbers') end
  if op=='+' or op=='-' then local text=a..(op=='-' and '-' or '')..b;if #text>4096 then error('Meta string exceeds 4096 bytes') end;return text end
  if op=='#' then return literal_pattern(a,'(?i)'..b) and 1 or 0 end
  a=string.lower(a);b=string.lower(b)
 end
 if op=='==' then return a==b and 1 or 0 elseif op=='!=' then return a~=b and 1 or 0
 elseif op=='&&' then return truth(a) and truth(b) and 1 or 0
 elseif op=='||' then return (truth(a) or truth(b)) and 1 or 0
 elseif op=='<' then return a<b and 1 or 0 elseif op=='>' then return a>b and 1 or 0
 elseif op=='<=' then return a<=b and 1 or 0 elseif op=='>=' then return a>=b and 1 or 0
 elseif op=='+' then return a+b elseif op=='-' then return a-b elseif op=='*' then return a*b
 elseif op=='^' then return math.tointeger(math.modf(a)) ~ math.tointeger(math.modf(b))
 elseif op=='/' or op=='%' then
  if b==0 then error('Meta division by zero') end
  if op=='/' then return a/b end
  a=math.modf(a);b=math.modf(b);if b==0 then error('Meta division by zero') end;return math.fmod(a,b)
 end
 error('Unsupported expression operator: '..op)
end
local function monster_rule(t,p)
 local rule={}
 for _,r in ipairs(p.monsters or {}) do
  local matches
  if r.expression then
   local value=meta_expression(r.expression,{monster=t,profile=p})
   matches=type(value)=='string' and string.lower(t.name)==string.lower(value) or (type(value)~='string' and truth(value))
  end
  if r.default then rule=r
  elseif matches or (not r.expression and ((r.exact and string.lower(t.name)==string.lower(r.name or '')) or (not r.exact and string.lower(t.name):sub(1,#(r.name or ''))==string.lower(r.name or '')))) then return r end
 end
 return rule
end
local function meta_condition(c,s,p)
 local t,v,q=c.type,c.value,c.properties or {}
 if t==0 then return false elseif t==1 then return true end
 if t==26 then return truth(meta_expression(c.expression,s)) end
 if t==2 then for _,n in ipairs(c.children) do if not meta_condition(n,s,p) then return false end end;return true end
 if t==3 then for _,n in ipairs(c.children) do if meta_condition(n,s,p) then return true end end;return false end
 if t==21 then return not meta_condition(c.children[1],s,p) end
 if t==4 or t==28 then
  for _,e in ipairs(s.chat_events or {}) do if e.serial>meta.chat_start then
   local colors=';'..(q.c or '')..';';local colorok=t==4 or (q.c or '')=='' or colors:contains(';'..vt_string(e.color or 0)..';')
   local groups=colorok and regexmatch(e.text,t==4 and v or q.p)
   if groups then
    if t==28 then for key in pairs(meta_vars) do if key:sub(1,13)=='capturegroup_' then meta_vars[key]=nil end end;for k,text in pairs(groups) do meta_vars['capturegroup_'..k]=text end;meta_vars.capturecolor=e.color or 0 end
    return true
   end
  end end;return false
 end
 if t==5 then return (s.pack_slots or 0)<=v end
 if t==8 then return (s.health or 0)<=0 end
 if t==9 then return (s.vendor or 0)~=0 end
 if t==10 then return (s.vendor or 0)==0 end
 if t==15 then local v,_,blocked,consumable=next_buff(s,p);return v~=nil or consumable==true end
 if t==23 then for _,e in ipairs(s.enchantments or {}) do if e.id==q.sid and e.remaining>=q.sec then return true end end;return false end
 if t==24 then return (s.burden_percent or 0)>=v end
 if t==25 then
  if #(p.route or {})==0 then return false end
  for _,pt in ipairs(p.route) do local d=distance(s.position,pt);if d>0.00001 and d<=q.dist then return false end end;return true
 end
 if t==14 then local count=0;for _,m in ipairs(s.targets or {}) do
  local priority=monster_rule(m,p).priority or 0
  if m.distance<=q.r and priority==q.p then count=count+1 end
 end;return count>=q.c end
 if t==6 or t==22 then return s.time-(t==22 and meta.persistent_entered or meta.entered)>=v end
 if t==7 then return #(p.route or {})==0 or (not p.loop_route and not p.reverse_route and point>#p.route) end
 if t==11 or t==12 then
  local count=0;for _,i in ipairs(s.inventory or {}) do if string.lower(i.name)==string.lower(q.n) then count=count+(i.count or 1) end end
  return t==11 and count<=q.c or t==12 and count>=q.c
 end
 if t==13 or t==16 then
  local count=0;for _,m in ipairs(s.targets or {}) do if m.distance<=(q.r or 0) and (t==16 or literal_pattern(m.name,q.n)) then count=count+1 end end
  return t==16 and count==0 or t==13 and count>=q.c
 end
 if t==17 then return (math.floor(s.position.cell)&0xffff0000)==(math.floor(v)&0xffff0000) end
 if t==18 then return math.floor(s.position.cell)==(math.floor(v)&0xffffffff) end
 if t==19 or t==20 then for _,e in ipairs(s.portal_events or {}) do if e.serial>meta.portal_start and e.entered==(t==19) then return true end end;return false end
 error('Unsupported meta condition: '..t)
end
local function meta_action(a,s,p)
 local t,q=a.type,a.properties or {}
 if t==0 then return end
 if t==1 then meta_state(a.value,s)
 elseif t==2 then return meta_command(a.command,s)
 elseif t==3 then for _,c in ipairs(a.children) do local e=meta_action(c,s,p);if e then return e end end
 elseif t==4 then
  meta.route=a.route;point=1;route_direction=1;route_pending=nil;route_join_pending=true;route_join_scan=nil;meta.route_revision=meta.route_revision+1;meta.route_changed=true
 elseif t==5 then
  if #meta.stack>=128 then return 'Meta call stack exceeds 128 entries' end
  meta.stack[#meta.stack+1]=q.ret;meta_state(q.st,s)
 elseif t==6 then
  if #meta.stack==0 then return 'Meta return has no caller' end
  local name=meta.stack[#meta.stack];meta.stack[#meta.stack]=nil;meta_state(name,s)
 elseif t==7 then meta_expression(a.expression,s)
 elseif t==8 then return meta_command(ucmcommand(tostring(meta_expression(a.expression,s))),s)
 elseif t==9 then meta.watchdog={state=q.s,radius=q.r,seconds=q.t,next=s.time+q.t/10,samples={}}
 elseif t==10 then meta.watchdog=nil
 elseif t==11 then
  local defaults={combat='off',buffing=true,navigation=false,looting=false,autostack=false,meta_enabled=true,idle_peace=false,open_doors=false,stop_on_death=false,autocram=false,nav_priority=false,radius=30,approach_range=60,waypoint_radius=.8,refresh_seconds=60,height=2,mana_threshold=45,health_threshold=65,stamina_threshold=50,door_range=2,
   item_mana=true,item_mana_threshold=20,read_unknown_scrolls=false,buff_skill_margin=p.skill_margin or 30,skill_margin=30,
   loot_fellow_corpses=false,loot_all_corpses=false,auto_attack_power=false,use_recklessness=false,idle_buff_topoff=false,idle_buff_seconds=1200,
   idle_health_threshold=0,idle_stamina_threshold=0,idle_mana_threshold=0,loot_range=15,loot_priority=false,loot_only_rare=false,salvage_combine=false,follow_corners=false,target_lock=true,
   kit_min_success=0,kits_in_magic=true,kit_peace=false,helper_health_threshold=0,helper_stamina_threshold=0,helper_mana_threshold=0,
   helper_health_range=74.5,helper_stamina_range=74.5,helper_mana_range=40,ring_range=5,ring_min_targets=4,debuff_refresh=5,debuff_fallback=false,
   split_peas=true,component_critical=4,component_normal=20,component_idle=20,fast_cast_buffs=true,switch_debuff_wand=true,
   summon_pets=false,pet_range_mode=0,pet_range=5,pet_min_targets=1,refill_summons=false,summon_refill_charges=5,fill_mana_stones=false,
   protection_profile=2,bane_profile=2,protection_custom='ALL',bane_custom='ALL',target_select=1,target_angle_range=5,minimum_range=0,monster_attempts=4,monster_blacklist_seconds=120,corpse_attempts=30,corpse_blacklist_seconds=200,use_arcs=1,arc_range=5,debuff_each_first=1}
  local v=meta.options[a.key];if v==nil then v=p[a.key] end;if v==nil then v=defaults[a.key] end
  if a.key=='combat' then v=v~='off' end
  meta_vars[q.v]=a.boolean and (v and 1 or 0) or v/a.scale
 elseif t==12 then
  local value=meta_expression(a.expression,s)
  if a.boolean then value=truth(value) and 'true' or 'false' end
  return meta_command(ucmcommand('/ucm opt set '..q.o..' '..tostring(value)),s)
 else return 'Unsupported meta action '..t end
end
local function meta_profile(s,p)
 local effective={};for k,v in pairs(p) do effective[k]=v end
 for k,v in pairs(meta.options) do effective[k]=v end
 if meta.route then for k,v in pairs(meta.route) do if k~='navigation' then effective[k]=v end end end
 if meta.forcebuff then effective.buffing=true end
 if p.vt_meta then effective.states=nil;effective.initial_state='Default' end
 return effective
end
local function meta_tick(s,p)
 meta_active_profile=p
 if meta.profile_revision~=(p.vt_revision or 0) then
  local first=meta.profile_revision==nil;meta.profile_revision=p.vt_revision or 0
  if not first and p.vt_loaded_kind=='usd' then
   meta.options={}
   recovery_pending=nil;buff_item_pending=nil;mana_refill_pending=nil;helper_pending=nil;debuff_pending=nil;debuff_scan=nil;pea_pending=nil;pet_pending=nil;dispel_pending=nil;buff_plan=nil;last_cast=nil
   until_time={};retries={};item_attempts={}
  end
  if not first and p.vt_loaded_kind=='met' then meta={name='Default',fired={},stack={},options=meta.options,queue={},profile_revision=p.vt_revision,route_revision=0};meta_vars={};meta_state('Default',s) end
  if not first and (p.vt_loaded_kind=='nav' or p.vt_loaded_kind=='met') then
   point=1;route_direction=1;route_pending=nil;route_join_pending=true;route_join_scan=nil;meta.route=nil;meta.route_changed=false
  end
  meta.loading=false
 end
 if meta.loading then return end
 if not meta.entered then meta_state('Default',s) end
 if p.stop_on_death and (s.health or 0)<=0 then return 'Stopped on death' end
 if not p.vt_meta or p.meta_enabled==false then return end
 local w=meta.watchdog
 if w and s.time>=w.next then
  w.next=s.time+w.seconds/10;w.samples[#w.samples+1]=s.position
  if #w.samples>10 then table.remove(w.samples,1) end
  local trapped=#w.samples==10
  for _,pos in ipairs(w.samples) do if distance(pos,s.position)>w.radius then trapped=false end end
  if trapped then
   if #meta.stack>=128 then return 'Meta watchdog call stack overflow' end
   meta.stack[#meta.stack+1]=meta.name;meta_state(w.state,s);return
  end
 end
 local before=meta.name
 for i,r in ipairs(p.vt_meta) do
  if r.state==meta.name and not meta.fired[i] and meta_condition(r.condition,s,meta_profile(s,p)) then
   meta.fired[i]=true;local e=meta_action(r.action,s,p);if e then return e end
   -- State changes, including a transition to the same state, rearm on the next tick.
   if meta.name~=before or not meta.fired[i] then break end
  end
 end
end
local function meta_work(s,p)
 if not activity_ready('meta') then return end
 if meta.notice then local text=meta.notice;meta.notice=nil;return {action='notice',text=text,status=text} end
 if meta.turn then
  local delta=math.abs(((s.heading or 0)-meta.turn.heading+180)%360-180)
  if delta<2 then meta.turn=nil
  elseif s.time>meta.turn.deadline then return failed('Unable to face route heading')
  else return {action='face',heading=meta.turn.heading,status='Facing route heading'} end
 end
 if (s.health or 0)<=0 then meta.pending=nil;return result(nil,'Waiting for resurrection') end
 -- A NAV use/portal can wait for confirmation before it can complete.
 -- Respond to its immediately following confirmation command, then skip only
 -- that already executed command when the original server action completes.
 if route_pending and not route_pending.confirmed then
  local route=p.route or {};local current=route[point];local nextpoint=route[point+route_direction]
  local dialog=(s.confirmations or {})[1]
  if current and (current.kind=='use' or current.kind=='portal') and nextpoint and nextpoint.kind=='command' and nextpoint.command and nextpoint.command.op=='confirm' and dialog then
   if s.ready==false then return result(nil,'Waiting to respond to route confirmation') end
   route_pending.confirmed=true
   return {action='confirm',type=dialog.type,context=dialog.context,accept=nextpoint.command.accept,status='Confirming route interaction'}
  end
 end
 -- Confirmation is allowed while the original use is waiting for the server.
 local confirmation=meta.queue[1]
 if confirmation and confirmation.op=='confirm' then
  local dialog=(s.confirmations or {})[1]
  if s.ready==false then return result(nil,'Waiting to respond to confirmation') end
  if dialog then table.remove(meta.queue,1);return {action='confirm',type=dialog.type,context=dialog.context,accept=confirmation.accept,status='Responding to server confirmation'} end
  confirmation.deadline=confirmation.deadline or s.time+10
  if s.time>=confirmation.deadline then return failed('Expected server confirmation did not appear') end
  return result(nil,'Waiting for server confirmation')
 end
 if meta.pending then
  local q=meta.pending
  if not q.portal and (s.action_serial or 0)~=q.serial then
   if (s.action_error or 0)~=0 then return failed('Meta interaction failed; check target, range and item requirements') end
   meta.pending=nil
  elseif (s.teleport_sequence or 0)~=q.teleport and not s.portal_space then meta.pending=nil
  elseif s.time>q.deadline then return failed('Meta interaction timed out')
  else return result(nil,'Waiting for meta interaction') end
 end
 if meta.loading then return result(nil,'Waiting for profile load') end
 if #meta.queue==0 then return end
 if s.busy or s.ready==false or s.jumping then return result(nil,'Waiting to run meta command') end
 local c=meta.queue[1]
 local function immediate(intent) table.remove(meta.queue,1);return intent end
 if c.op=='load' then meta.loading=true;return immediate({action='profile_load',profile=c.profile,kind=c.kind,status='Loading '..c.profile}) end
 if c.op=='recall' then meta.pending={serial=s.action_serial or 0,teleport=s.teleport_sequence or 0,deadline=s.time+60,portal=true};return immediate({action='recall',destination=c.destination,status='Recalling to '..c.destination}) end
 if c.op=='say' then return immediate({action='say',text=c.text,channel=c.channel,status='Meta chat'}) end
 if c.op=='attack_bar' then meta.options.power_percent=c.power*100;return immediate({action='attack_bar',power=c.power,status='Attack power updated'}) end
 if c.op=='combat_mode' then return immediate({action='combat_mode',mode=c.mode,status='Changing combat mode'}) end
 if c.op=='equip' or c.op=='identify' then return immediate({action=c.op,item=c.item_id,status='Meta: '..c.op}) end
 if c.op=='cast' then
  meta.pending={serial=s.action_serial or 0,teleport=s.teleport_sequence or 0,deadline=s.time+30}
  return immediate({action='cast',spell=c.spell,target=c.target_id,status='Meta spell cast'})
 end
 if c.op=='logout' then return immediate({action='logout',status='Meta logout'}) end
 if c.op=='jump' then return immediate({action='jump',charge=c.charge,forward=c.forward or 0,strafe=c.strafe or 0,walk_jump=c.walk_jump,heading=c.heading,current_heading=c.current_heading~=false,status='Meta jump'}) end
 if c.op=='face' then meta.turn={heading=c.heading,deadline=s.time+10};return immediate({action='face',heading=c.heading,status='Facing route heading'}) end
 if c.op=='fellowship' then
  local id;if c.operation=='recruit' then for _,o in ipairs(s.route_objects or {}) do if o.object_class==24 and string.lower(o.name)==string.lower(c.name) then id=o.id;break end end;if not id then return failed('Fellowship recruit is not nearby: '..c.name) end end
  return immediate({action='fellowship',operation=c.operation,name=c.name,target=id,status='Fellowship: '..c.operation})
 end
 local function find(list,name,id,partial)
  name=string.lower(name or '')
  for _,o in ipairs(list or {}) do if (id and o.id==id) or (not id and string.lower(o.name or '')==name) then return o end end
  if partial and c.prefix and not id then for _,o in ipairs(list or {}) do if string.lower(o.name or ''):contains(name) then return o end end end
 end
 if c.op=='use_selected' then c.item_id=meta.selected or s.selected end
 if c.op=='select' and c.item_id==s.player then meta.selected=s.player;return immediate({action='select',target=s.player,status='Selected self'}) end
 local item,action
 for _,partial in ipairs({false,true}) do
  if c.scope~='world' then item=find(s.inventory,c.item,c.item_id,partial);if item then action='use_item';break end end
  if c.scope~='inventory' then item=find(s.route_objects,c.item,c.item_id,partial) or find(s.targets,c.item,c.item_id,partial) or find(s.corpses,c.item,c.item_id,partial);if item then action='use_world';break end end
 end
 if not item then return failed('Meta item or object is unavailable: '..(c.item or tostring(c.item_id))) end
 if c.op=='select' then meta.selected=item.id;return immediate({action='select',target=item.id,status='Selected '..item.name}) end
 local target
 if c.target or c.target_id then
  if action~='use_item' then return failed('Meta use-on/give requires an owned item') end
  if c.target_id==s.player then target={id=s.player} end
  for _,partial in ipairs({false,true}) do target=target or find(s.route_objects,c.target,c.target_id,partial) or find(s.inventory,c.target,c.target_id,partial) or find(s.targets,c.target,c.target_id,partial);if target then break end end
  if not target then return failed('Meta target is unavailable: '..(c.target or tostring(c.target_id))) end
  action=c.op=='give' and 'give' or 'apply_item'
 end
 table.remove(meta.queue,1);meta.pending={serial=s.action_serial or 0,teleport=s.teleport_sequence or 0,deadline=s.time+45}
 return {action=action,item=item.id,target=target and target.id,amount=1,status='Meta: '..c.op..' '..item.name}

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
local function corpse_owner_name(name)
 name=string.lower(name or '')
 while name:sub(1,1)=='+' do name=name:sub(2) end
 return name
end
local function condition_match(c,item,s,scan)
 if c.field=='legacy' then
  local t,a=c.key,c.args
  if t==6 then return false end -- Original Classic rule is retired and always fails.
  if t==7 then return item.object_class==a[1] end
  if t==17 then local palette=(item.palettes or {})[math.floor(a[1])+1];return palette~=nil and band(palette,0xffffff)==band(math.floor(a[2]),0xffffff) end
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
 if value==nil and not item.identified then return nil end
 if c.field=='string' then return literal_pattern(value or '',c.pattern or '') end
 if value==nil then if c.missing_zero then value=0 else return false end end
 local n=c.value or 0
 return (c.op=='ge' and value>=n) or (c.op=='le' and value<=n) or (c.op=='eq' and value==n) or (c.op=='ne' and value~=n) or (c.op=='bits' and (math.floor(value)&math.floor(n))~=0)
end
-- Check public fields before appraising; native rules require missing properties to exist.

local function rule_match(rule,item,s,scan)
 if rule.enabled==false then return false end
 local wanted=string.lower(rule.name or '')
 if wanted~='' then
  local name=string.lower(item.name or '');local mode=rule.name_mode or 'prefix'
  if mode=='exact' and name~=wanted then return false end
  if mode=='prefix' and name:sub(1,#wanted)~=wanted then return false end
  if mode=='contains' and not name:contains(wanted) then return false end
 end
 if (rule.material or 0)~=0 and item.material~=rule.material then return false end
 if (rule.type or 0)~=0 and band(item.type,rule.type)==0 then return false end
 local missing=scan.missing or false
 for _,field in ipairs({'value','workmanship','burden','rating'}) do
  local lo,hi=rule['min_'..field] or 0,rule['max_'..field] or 0
  if lo>0 or hi>0 then
   local value=item[field=='rating' and 'damage_rating' or field]
   if value==nil then missing=true
   elseif value<lo or (hi>0 and value>hi) then return false end
  end
 end
 local conditions=rule.conditions or {}
 -- Conditions are ANDed. Reject on inexpensive public/numeric properties before
 -- scanning spell names, regardless of the editor's display order.
 for phase=scan.phase or 1,2 do
 for index=scan.condition or 1,#conditions do
  local c=conditions[index];local expensive=c.field=='spells' or c.field=='string'
  if expensive==(phase==2) then
  if not workavailable() then scan.phase=phase;scan.condition=index;scan.missing=missing;return 'pending' end
  local match=condition_match(conditions[index],item,s,scan)
  if match=='pending' then scan.phase=phase;scan.condition=index;scan.missing=missing;return 'pending'
  elseif match==false then return false elseif match==nil then missing=true end
  end
 end
 scan.condition=nil
 end
 if missing then if item.identified then return false else return nil end end
 return true
end
local function equipment_mana(s,p,inventory,option)
 if not activity_ready('item_mana') then return end
 if mana_fill_pending then
  local q=mana_fill_pending;local stone
  for _,v in ipairs(inventory) do if v.id==q.stone then stone=v;break end end
  if stone and stone.identified and (stone.mana or 0)>0 then mana_fill_pending=nil
  elseif ((s.action_serial or 0)~=q.serial and (s.action_error or 0)~=0) or s.time>=q.deadline then
   mana_fill_pending=nil;until_time.mana_fill=s.time+30
  else return end -- Keep combat/recovery available while appraisal catches up.
 end
 -- Inventory mana is an item resource, not the player's mana vital.
 if mana_refill_pending then
  local improved=false
  for _,v in ipairs(inventory) do local before=mana_refill_pending.before[v.id];if before and (v.mana or 0)>before then improved=true;break end end
  if improved then mana_refill_pending=nil;item_attempts.mana_refill=0
  elseif ((s.action_serial or 0)~=mana_refill_pending.serial and (s.action_error or 0)~=0) or s.time>=mana_refill_pending.expires then
   mana_refill_pending=nil;until_time.mana_stone=s.time+30
  else return result(nil,'Waiting for equipment mana refresh') end
 end
 if option('item_mana',true) then
  local low=false;for _,v in ipairs(inventory) do if v.equipped and (v.max_mana or 0)>0 and pct(v.mana or 0,v.max_mana)<math.min(99,option('item_mana_threshold',20)) then low=true end end
  if low and s.time>=(until_time.mana_stone or 0) then
   for _,v in ipairs(inventory) do
    local named=p.assist_items==nil;for _,a in ipairs(p.assist_items or {}) do if (a.type==6 or a.type==8) and a.name==v.name then named=true end end
    if band(v.type,0x80000)~=0 and (v.mana or 0)>0 and (not v.workmanship or v.workmanship==0 or (v.mana or 0)>=option('mana_tank_minimum',0)) and allowed(p.consumables,v.wcid) and named then
    if (item_attempts.mana_refill or 0)>=3 then return failed('Equipment mana refill failed repeatedly; check charges and item requirements') end
    item_attempts.mana_refill=(item_attempts.mana_refill or 0)+1
    local before={};for _,gear in ipairs(inventory) do if gear.equipped then before[gear.id]=gear.mana or 0 end end
    mana_refill_pending={before=before,serial=s.action_serial or 0,expires=s.time+10}
    until_time.mana_stone=s.time+30;return {action='use_item',item=v.id,status='Recharging equipment: '..v.name} end end
  end
 end
 if option('fill_mana_stones',false) and s.time>=(until_time.mana_fill or 0) then
  local stone,source
  local sources={};for _,id in ipairs(p.mana_source_items or {}) do sources[id]=true end
  for _,v in ipairs(inventory) do
   if v.identified and v.usable and v.mana_empty and band(v.type,0x80000)~=0 and allowed(p.consumables,v.wcid) then stone=stone or v end
   -- Explicit individual items, never all items of the same weenie class.
   if sources[v.id] and v.identified and not v.equipped and not v.retained and v.resource_available~=false
    and (v.mana or 0)>0 and (v.max_mana or 0)>0 and band(v.type,0x80200)==0 then source=source or v end
  end
  if stone and source and stone.id~=source.id then
   mana_fill_pending={stone=stone.id,source=source.id,serial=s.action_serial or 0,deadline=s.time+15}
   return {action='apply_item',item=stone.id,target=source.id,status='Filling '..stone.name..' from '..source.name..' (consumes item)'}
  end
 end
end

local function refill_pet(s,p,inventory,option,eligible)
 if not activity_ready('pets') then return end
 if pet_refill_pending then
  local q=pet_refill_pending;local device
  for _,v in ipairs(inventory) do if v.id==q.id then device=v;break end end
  if device and (device.structure or 0)>q.before then pet_refill_pending=nil
  elseif ((s.action_serial or 0)~=q.serial and (s.action_error or 0)~=0) or s.time>=q.deadline then
   pet_refill_pending=nil;until_time.pet_refill=s.time+30
  else return end
 end
 if not option('refill_summons',false) or s.time<(until_time.pet_refill or 0) then return end
 local spirit,device
 for _,v in ipairs(inventory) do
  -- ACE's Encapsulated Spirit recipe explicitly uses WCID 49485.
  if v.wcid==49485 and v.usable and not v.equipped and v.resource_available~=false then spirit=spirit or v end
  local props=v.int_properties or {}
  if eligible(v) and v.identified and props['280']==213 and (v.max_structure or 0)>0
   and (v.structure or 0)<v.max_structure and (v.structure or 0)<=option('summon_refill_charges',5) then device=device or v end
 end
 if spirit and device then
  pet_refill_pending={id=device.id,before=device.structure or 0,serial=s.action_serial or 0,deadline=s.time+15}
  return {action='apply_item',item=spirit.id,target=device.id,status='Refilling summon: '..device.name}
 end
end

local function tick(s,p)
 if not state then state=p.initial_state or 'Default';entered=s.time end
 local hp,sp,mp=pct(s.health,s.max_health),pct(s.stamina or 0,s.max_stamina or 1),pct(s.mana,s.max_mana)
 local current;for _,v in ipairs(p.states or {}) do if v.name==state then current=v end end
 if p.usd_keys then state='Default';current={} end -- Imported character settings replace native state overrides.
 if not p.states and state=='Default' then current={} end
 if not current then
  local invalid=state;state='Default';current={};entered=s.time
  if activity_ready('meta') then return failed('Unknown activity state: '..invalid..'; using base settings') end
 end
 local function option(k,default) if k=='navigation' and navigation_paused then return false end;if current[k]~=nil then return current[k] end;if p[k]~=nil then return p[k] end;return default end
 -- Failure counts come from server combat feedback, not from elapsed attack time
 -- or evades. Match VT's strict count > limit and let the route run past failures.
 local present={};for _,t in ipairs(s.targets or {}) do present[t.id]=true end
 for id in pairs(monster_failures) do if not present[id] then monster_failures[id]=nil end end
 for id,expiry in pairs(monster_blacklist) do if not present[id] or s.time>=expiry then monster_blacklist[id]=nil end end
 for _,e in ipairs(s.combat_events or {}) do if e.serial>combat_serial then
  combat_serial=e.serial
  if present[e.target] then
   if e.unhittable then monster_blacklist[e.target]=s.time+option('monster_blacklist_seconds',120);monster_failures[e.target]=nil
   elseif e.hit then monster_failures[e.target]=nil;ghost_attempts[e.target]=nil;ghost_hp[e.target]={time=s.time}
   else
    local failures=(monster_failures[e.target] or 0)+1
    if failures>option('monster_attempts',4) then monster_blacklist[e.target]=s.time+option('monster_blacklist_seconds',120);monster_failures[e.target]=nil
    else monster_failures[e.target]=failures end
   end
  end
 end end
 -- Keep server-owned objects intact; suppress stale combat targets locally.
 for id in pairs(ghost_attempts) do if not present[id] then ghost_attempts[id]=nil end end
 for id in pairs(ghost_hp) do if not present[id] then ghost_hp[id]=nil end end
 for _,t in ipairs(s.targets or {}) do
  local h=ghost_hp[t.id]
  if not h or (t.health_fraction and t.health_fraction~=h.health) then h={time=s.time,health=t.health_fraction};ghost_hp[t.id]=h end
  if (option('delete_ghost_monsters',false) and (ghost_attempts[t.id] or 0)>option('ghost_spell_attempts',20))
   or (option('delete_ghost_hp',false) and t.id==locked_target and s.time-h.time>=option('ghost_hp_seconds',600)) then
   monster_blacklist[t.id]=s.time+option('monster_blacklist_seconds',120);ghost_attempts[t.id]=nil;h.time=s.time
  end
 end
 local function ring_enabled(rule) if rule and rule.ring~=nil then return rule.ring end;return option('ring',false) end
 local target,target_rule,priority=nil,nil,-1e9
 local function nearer(a,b)
  if not b then return true end
  if option('target_lock',true) then if a.id==locked_target then return true elseif b.id==locked_target then return false end end
  local method=option('target_select',1);local range=option('target_angle_range',5)
  local angles=method==2 or (method==3 and a.distance<range and b.distance<range)
  if angles and (a.angle or 180)~=(b.angle or 180) then return (a.angle or 180)<(b.angle or 180) end
  if a.distance~=b.distance then return a.distance<b.distance end
  return (a.angle or 180)<(b.angle or 180)
 end
 local ring_count=0;local target_candidates={}
 for _,t in ipairs(s.targets or {}) do if not monster_blacklist[t.id] and t.line_of_sight~=false and t.distance>=option('minimum_range',0) and t.distance<=math.max(option('radius',30),option('approach_range',60)) and not contains(p.ignore_monsters,t.name) then
  local rule=monster_rule(t,p)
  if not rule.ignore and ring_enabled(rule) and t.distance<option('ring_range',5) then ring_count=ring_count+1 end
  local rank=rule.priority or 0
  if not rule.ignore then target_candidates[#target_candidates+1]={target=t,rule=rule,rank=rank} end
  if not rule.ignore and (rank>priority or (rank==priority and nearer(t,target))) then target=t;target_rule=rule;priority=rank end
 end end
 if not target and s.nearest~=0 and s.targets==nil then target={id=s.nearest,distance=s.distance or 0,resists={}} end
 for _,r in ipairs(not forced_buff and current.transitions or {}) do
  local match=(r.when=='health_below' and hp<r.value) or (r.when=='mana_below' and mp<r.value) or (r.when=='stamina_below' and sp<r.value)
   or (r.when=='elapsed' and s.time-entered>=r.value) or (r.when=='no_targets' and not target)
   or (r.when=='target_available' and target~=nil) or (r.when=='route_complete' and point>#(p.route or {}))
  if match then state=r.next;entered=s.time;return result(nil,'State: '..state) end
 end
 -- The host exposes only UCM's cancellable corpse approach here, never an
 -- inventory transfer or a cast. Reuse normal target filtering (LOS, rules,
 -- range and blacklists) before interrupting it for combat.
 if s.loot_approaching and not s.jumping and target and option('combat','off')~='off'
  and not option('loot_priority',false) and activity_ready('combat') then
  if corpse_pending then
   local key='corpse'..corpse_pending.id;item_attempts[key]=math.max(0,(item_attempts[key] or 1)-1);corpse_pending=nil
  end
  return result('pause_loot_approach','Pausing corpse approach for combat')
 end
 if s.busy or s.ready==false or s.jumping then return result(nil,s.jumping and 'Following jump arc' or 'Waiting for game action') end
 if locked_target and (not target or option('combat','off')=='off') then locked_target=nil;return result('cancel_attack','No unobstructed combat target; continuing other activities') end
 if last_cast and (s.action_serial or 0)~=last_cast.serial and s.last_spell==last_cast.id then
  local confirmed=(s.action_error or 0)==0 and s.last_spell_confirmed~=false
  if confirmed and forced_buff and last_cast.force_cycle==forced_buff then
   forced_buff.done[tostring(last_cast.target)..':'..last_cast.category]=last_cast.power;buff_plan=nil
  end
  if not last_cast.request then
   -- The server finished this attempt, so the twelve-second missing-reply
   -- watchdog no longer applies. Fizzles retry promptly but retain the bounded
   -- attempt count; they must not mark a forced family complete.
   local key=tostring(last_cast.target)..':'..last_cast.category;until_time[key]=nil
   if confirmed then retries[key]=nil end
  end
  if s.action_error==0x400 or (last_cast.recovery and (s.action_error or 0)~=0) then
   if last_cast.request and other_job and other_job.id==last_cast.request then
    other_job.unavailable[last_cast.id]=s.time+120
   else
    unavailable_spells[last_cast.id]=s.time+120
    catalog_snapshot=nil;buff_plan=nil
    local key=tostring(last_cast.target)..':'..last_cast.category;retries[key]=nil;until_time[key]=nil
   end
  end
  last_cast=nil
 end
 if route_pending and route_pending.kind~='pause' and not forced_buff then
   activity='navigation'
   local route=p.route or {};local v=route[point]
   if not v then return failed('Route changed during an action') end
   local kind=v.kind or 'walk'
   if kind=='jump' then
    if s.time-route_pending.time>2 and not s.jumping then route_pending=nil;point=point+route_direction end
   elseif kind=='portal' or kind=='recall' then
    local nextpoint=route[point+route_direction]
    if s.teleport_sequence~=route_pending.teleport and not s.portal_space and (v.legacy or (nextpoint and distance(s.position,nextpoint)<30)) then local step=route_pending.confirmed and 2 or 1;route_pending=nil;point=point+route_direction*step
    elseif s.time-route_pending.time>45 then return pause_navigation('portal did not reach the recorded destination') end
   elseif s.action_serial~=route_pending.serial then
    if (s.action_error or 0)~=0 then return pause_navigation('route interaction failed') end
    local step=route_pending.confirmed and 2 or 1;route_pending=nil;point=point+route_direction*step
   elseif s.time-route_pending.time>30 then return pause_navigation('route action timed out') end
   return result(nil,'Waiting for route action')
  end
 local by_id,spells=index_spells(s,option('skill_margin',30));local trained={}
 for _,id in ipairs(s.trained_skills or {}) do trained[id]=true end
 for _,id in ipairs(s.usable_skills or {}) do trained[id]=true end
 local inventory=s.inventory or {}
 -- Restock exact saved stock identities at an open vendor; never guess by GUID
 -- across logins. Wait for server inventory replication before buying again.
 local stock_counts
 local function stock_count(wcid,name)
  if not stock_counts then
   stock_counts={};for _,v in ipairs(inventory) do if v.wcid and v.name then
    local names=stock_counts[v.wcid] or {};stock_counts[v.wcid]=names;names[v.name]=(names[v.name] or 0)+(v.count or 1)
   end end
  end
  return stock_counts[wcid] and stock_counts[wcid][name] or 0
 end
 if activity_ready('vendors') then
 if note_pending then
  local q=note_pending
  if not option('vendor_restock',false) or s.vendor~=q.vendor or not s.vendor_uses_pyreals then
   note_pending=nil;return result(nil,'Trade note redemption cancelled: vendor or restocking changed')
  end
  if s.time>=q.deadline or ((s.action_serial or 0)~=q.serial and (s.action_error or 0)~=0) then
   return failed('Trade note redemption not confirmed: check inventory space and vendor messages')
  end
  local source
  for _,v in ipairs(inventory) do if v.id==q.id then source=v;break end end
  if not source then for _,v in ipairs(s.vendor_trade_notes or {}) do if v.id==q.id then source=v;break end end end
  if q.phase=='split' then
   local candidate
   for _,v in ipairs(s.vendor_trade_notes or {}) do
    if not q.known[v.id] and v.wcid==q.wcid and v.name==q.name and v.count==q.count and v.unit_value==q.unit_value then
     if candidate then return failed('Trade note split is ambiguous; no notes sold') end
     candidate=v
    end
   end
   if source and source.count==q.before-q.count and candidate then
    q.phase='sell';q.id=candidate.id;q.deadline=s.time+15;q.serial=s.action_serial or 0;q.known=nil
    q.money=s.pyreals or 0
    return {action='sell_note',vendor=q.vendor,item=q.id,count=q.count,status='Redeeming '..q.count..' '..q.name..' for supplies'}
   end
   return result(nil,'Waiting for trade note split')
  end
  -- Require both sides of the transaction. Replication may deliver the money
  -- before the sold object disappears, or vice versa.
  if not source and (s.pyreals or 0)>=q.money+q.count*q.unit_value then note_pending=nil
  else return result(nil,'Waiting for trade note proceeds') end
 end
 if purchase_pending then
  local q=purchase_pending
  if stock_count(q.wcid,q.name)>=q.expected then purchase_pending=nil
  elseif s.time>=q.deadline or (s.vendor or 0)~=q.vendor or ((s.action_serial or 0)~=q.serial and (s.action_error or 0)~=0) then
   return failed('Purchase not confirmed: check currency, pack space and vendor stock')
  else return result(nil,'Waiting for purchased '..q.name) end
 end
 if option('vendor_restock',false) and (s.vendor or 0)~=0 then
  local stock={}
  for _,v in ipairs(s.vendor_stock or {}) do if v.limit>0 then local names=stock[v.wcid] or {};stock[v.wcid]=names;names[v.name]=v end end
  for _,r in ipairs(p.vendor_rules or {}) do
   if r.enabled~=false and r.server==s.world_name and r.vendor_wcid==s.vendor_wcid and r.vendor_name==s.vendor_name then
    local have=stock_count(r.item_wcid,r.item_name)
    if have<r.quantity then
     local v=stock[r.item_wcid] and stock[r.item_wcid][r.item_name]
     if v then
      local count=math.min(r.quantity-have,v.limit)
      local cost=v.unit_value and math.max(1,math.ceil(v.unit_value*(v.sell_rate or 1)*count-.1))
      if cost and s.vendor_uses_pyreals and (s.pyreals or 0)<cost then
       local deficit=cost-(s.pyreals or 0);local candidates,total={},0;local reserved={}
       for _,rule in ipairs(p.vendor_rules or {}) do
        if rule.enabled~=false and rule.server==s.world_name and rule.vendor_wcid==s.vendor_wcid and rule.vendor_name==s.vendor_name then reserved[rule.item_wcid]=true end
       end
       for _,note in ipairs(s.vendor_trade_notes or {}) do
        if not reserved[note.wcid] and note.unit_value>0 and note.count>0 then
         candidates[#candidates+1]=note;total=total+note.unit_value*note.count
        end
       end
       if total<deficit then return failed('Not enough pyreals or redeemable trade notes for '..r.item_name) end
       -- Prefer a denomination covering the shortfall with the least change.
       -- Otherwise redeem the largest available contribution and reassess.
       table.sort(candidates,function(a,b)
        local ac=math.min(a.count,math.ceil(deficit/a.unit_value))*a.unit_value
        local bc=math.min(b.count,math.ceil(deficit/b.unit_value))*b.unit_value
        if (ac>=deficit)~=(bc>=deficit) then return ac>=deficit end
        if ac~=bc then if ac>=deficit then return ac<bc else return ac>bc end end
        return a.id<b.id
       end)
       local note=candidates[1];local amount=math.min(note.count,math.ceil(deficit/note.unit_value))
       note_pending={phase=amount<note.count and 'split' or 'sell',id=note.id,wcid=note.wcid,name=note.name,
        unit_value=note.unit_value,count=amount,before=note.count,money=s.pyreals or 0,vendor=s.vendor,deadline=s.time+15,serial=s.action_serial or 0}
       if amount<note.count then
        note_pending.known={};for _,item in ipairs(inventory) do note_pending.known[item.id]=true end
        for _,item in ipairs(s.vendor_trade_notes or {}) do note_pending.known[item.id]=true end
        return {action='split_note',vendor=s.vendor,item=note.id,count=amount,status='Splitting '..amount..' '..note.name..' for supplies'}
       end
       return {action='sell_note',vendor=s.vendor,item=note.id,count=amount,status='Redeeming '..amount..' '..note.name..' for supplies'}
      end
      purchase_pending={wcid=r.item_wcid,name=r.item_name,expected=have+count,deadline=s.time+15,serial=s.action_serial or 0,vendor=s.vendor}
      return {action='buy',vendor=s.vendor,item=v.id,count=count,status='Restocking '..r.item_name..' ('..have..' / '..r.quantity..')'}
     end
     return failed('Saved restock item is not available: '..r.item_name)
    end
   end
  end
 end
 end -- vendor activity
 local usable_names={};for _,v in ipairs(inventory) do if v.usable and v.name then usable_names[v.name]=v end end
 local weapon_types,weapon_ids={},{ }
 for _,v in ipairs(p.weapons or {}) do weapon_types[v]=true end
 for _,v in ipairs(p.weapon_items or {}) do weapon_ids[v]=true end
 local function weapon_allowed(v) return (#(p.weapons or {})==0 or weapon_types[v.wcid]) and (#(p.weapon_items or {})==0 or weapon_ids[v.id]) end
 for _,v in ipairs(inventory) do
  if not v.equipped then item_attempts['equip'..v.id..':remove']=0
  elseif band(v.equipped_slot,0x200000)~=0 then item_attempts['equip'..v.id..':offhand']=0
  else item_attempts['equip'..v.id]=0 end
 end
 local function equip(v,offhand,unequip)
  local key='equip'..v.id..(offhand and ':offhand' or unequip and ':remove' or '')
  if s.time<(until_time[key] or 0) then return result(nil,'Waiting for equipment') end
  if (item_attempts[key] or 0)>=3 then return failed('Could not equip '..v.name) end
  until_time[key]=s.time+5;item_attempts[key]=(item_attempts[key] or 0)+1
  return {action='equip',item=v.id,offhand=offhand,unequip=unequip,status=(unequip and 'Removing ' or 'Equipping ')..v.name}
 end
 local function assess(v)
  local key='id'..v.id
  if (item_attempts[key] or 0)>=3 then return failed('Unable to assess '..v.name..'; check appraisal skills') end
  if s.time<(until_time[key] or 0) then return result(nil,'Waiting for appraisal: '..v.name) end
  item_attempts[key]=(item_attempts[key] or 0)+1;until_time[key]=s.time+30
  return {action='identify',item=v.id,status='Assessing '..v.name}
 end
 local function caster()
  if not s.inventory then return end -- API v1 fixtures / hosts.
  local best,unknown
  for _,v in ipairs(inventory) do if band(v.type,0x8000)~=0 then
   if v.equipped then return end
   if not v.identified then unknown=v
   elseif v.can_wield and (not best or (v.element_mod or 1)>(best.element_mod or 1)) then best=v end
  end end
  if best then return equip(best) end
  if unknown then return assess(unknown) end
  return failed('No usable casting equipment; add a wand, staff or orb')
 end
 local function idle_stance()
  if not target and option('idle_peace',false) and (s.combat_mode or 1)~=1 then
   return {action='combat_mode',mode=1,status='Resting in peace mode'}
  end
 end
 local function route_door(destination)
  if not option('open_doors',true) then return end
  for id,attempt in pairs(door_attempts) do
   if s.time-attempt.time>30 then door_attempts[id]=nil end
  end
  local px,py=world(s.position);local tx,ty=world(destination)
  local dx,dy=tx-px,ty-py;local length=dx*dx+dy*dy
  if length==0 then return end
  for _,door in ipairs(s.route_objects or {}) do
   if door.object_class==26 then
    if door.door_open then door_attempts[door.id]=nil
    elseif door.distance<=option('door_range',2) and s.time>=(until_time['door'..door.id] or 0) then
     local x,y=world(door);local along=((x-px)*dx+(y-py)*dy)/length
     if along>=0 and along<=1 and (x-px-along*dx)^2+(y-py-along*dy)^2<2.25 then
      local attempt=door_attempts[door.id]
      -- A used door can close again. Only bound retries at the same obstruction;
      -- opening it, moving away, or returning later starts a fresh attempt.
      if not attempt or distance(s.position,attempt.position)>2 or s.time-attempt.time>30 then
       attempt={count=0,position=s.position};door_attempts[door.id]=attempt
      end
      if attempt.count>=3 then return pause_navigation('door remains closed after three attempts') end
      attempt.count=attempt.count+1;attempt.time=s.time
      until_time['door'..door.id]=s.time+5
      return {action='use_world',item=door.id,status='Opening route door'}
     end
    end
   end
  end
 end
 local function navigate()
 if not activity_ready('navigation') then return end
 if option('navigation',false) then
  -- VT's timed pause stops route movement, not the higher-priority combat and
  -- recharge rules. It has no server acknowledgement to monopolize the loop.
  if route_pending and route_pending.kind=='pause' then
   if s.time<route_pending.deadline then return end
   route_pending=nil;point=point+route_direction
   return result(nil,'Route pause complete')
  end
  local idle=idle_stance();if idle then return idle end
  if (p.follow_name or '')~='' or (p.follow_id or 0)~=0 then
   local follow
   for _,o in ipairs(s.route_objects or {}) do if o.id==p.follow_id or o.name==p.follow_name then follow=o;break end end
   if not follow then return result(nil,'Waiting for follow target to enter range') end
   if follow.distance<=option('follow_distance',3) then return result(nil,'Following '..follow.name) end
   local aim=option('follow_corners',false) and follow_path[1] or follow
   aim=aim or follow
   local door=route_door(aim);if door then return door end
   return {action='move',cell=aim.cell,x=aim.x,y=aim.y,z=aim.z,status='Following '..follow.name}
  end
  if meta.route_changed then return result(nil,'Loading route geometry') end
  local route=p.route or {};if #route==0 then return result(nil,'Record a route to begin navigation') end
  -- Return along positions actually traversed during combat/looting. Picking
  -- the nearest waypoint after a chase can cut across an adjacent room wall.
  if route_walking then route_trail={} end
  while #route_trail>0 do
   local back=route_trail[#route_trail]
   if distance(s.position,back)<=.45 then table.remove(route_trail)
   else route_returning=true;local door=route_door(back);if door then return door end
    return {action='move',cell=back.cell,x=back.x,y=back.y,z=back.z,arrival_radius=.4,status='Returning to route'} end
  end
  route_returning=false;route_walking=true
  if route_join_pending then
   -- Join once, then retain the ordered cursor through combat, looting and
   -- portal/jump acknowledgements. Camera heading must not affect the choice.
   -- Imported NAV action coordinates can be placeholders; only walk-first
   -- entries are spatial anchors. Keep action-only routes in their original order.
   local scan=route_join_scan or {index=1,position=s.position,visible=s.route_visible};route_join_scan=scan
   local here=scan.position;local cell=math.floor(here.cell)
   while scan.index<=#route do
    if not workavailable() then return result(nil,'Finding nearest route waypoint') end
    local i=scan.index;local candidate=route[i];scan.index=i+1
    local spatial=not candidate.legacy or candidate.walk_first
    if spatial then
     scan.first=scan.first or i
     local target=math.floor(candidate.cell)
     local connected=candidate.legacy or (cell>>16)==(target>>16) or ((cell&0xffff)<0x100 and (target&0xffff)<0x100)
     if connected and (not scan.visible or scan.visible[tostring(i)]~=false) then
      local d=distance(here,candidate)
      if not scan.distance or d<scan.distance then scan.point=i;scan.distance=d end
     end
    end
   end
   -- Native routes in another dungeon cannot be joined by walking to their
   -- unrelated coordinates. Do not silently charge towards waypoint one.
   if not scan.point and scan.first then route_join_scan=nil;return result(nil,'No reachable route waypoint in this area') end
   -- Retain a NAV's leading setup/use/confirmation actions when joining its
   -- first spatial anchor. They are not independent destinations to skip.
   point=(scan.point==scan.first) and 1 or (scan.point or 1);route_join_pending=false;route_join_scan=nil
  end
  if point>#route or point<1 then
   if p.reverse_route and #route>1 then route_direction=-route_direction;point=point>#route and #route-1 or 2
   elseif p.loop_route then point=point<1 and #route or 1
   elseif p.reverse_route then point=1
   else return result(nil,'Route complete') end
  end
  local v=route[point];local kind=v.kind or 'walk'
  if distance(s.position,v)<=option('waypoint_radius',.8) then route_blocks=0 end
  if (not v.legacy or v.walk_first) and distance(s.position,v)>option('waypoint_radius',.8) then
   local door=route_door(v);if door then return door end
   return {action='move',cell=v.cell,x=v.x,y=v.y,z=v.z,arrival_radius=option('waypoint_radius',.8),coordinate_route=v.legacy,status='Waypoint '..point..' / '..#route}
  end
  if kind=='checkpoint' and (not s.server_position or distance(s.server_position,v)>option('waypoint_radius',.8)) then return result(nil,'Checkpoint: waiting for server position') end
  if kind=='pause' then
   route_pending={kind='pause',time=s.time,deadline=s.time+(v.seconds or 5)}
   route_trail={{cell=s.position.cell,x=s.position.x,y=s.position.y,z=s.position.z}}
   return result(nil,'Pausing on route')
  end
  if kind=='recall' then
   if not by_id[v.spell] or by_id[v.spell].known==false then return failed('Route recall spell is not known') end
   local equipment=caster();if equipment then return equipment end
   if not v.legacy and not route[point+route_direction] then return failed('Record a destination point after the recall') end
   route_pending={time=s.time,serial=s.action_serial,teleport=s.teleport_sequence};return cast(s,by_id[v.spell],s.player,'Recalling on route')
  end
  if kind=='command' then local e=meta_command(v.command,s);if e then if e=='Stopped by UCM command' then return result('stop',e) end;return failed(e) end;point=point+route_direction;return result(nil,'Route command completed') end
  if kind=='jump' then route_pending={time=s.time};return {action='jump',heading=v.heading or 0,current_heading=v.current_heading,charge=v.charge or .5,forward=v.forward or 1,strafe=v.strafe or 0,walk_jump=v.walk_jump,status='Charging route jump'} end
  if kind=='portal' or kind=='use' then
   if kind=='portal' and not v.legacy and not route[point+route_direction] then return failed('Record a destination point after the portal') end
   local object
   for _,o in ipairs(s.route_objects or {}) do
    local match=(v.object_id and o.id==v.object_id) or (not v.object_id and ((v.wcid and o.wcid==v.wcid) or o.name==v.object_name))
    if v.object_class and o.object_class~=v.object_class then match=false end
    if match and v.object_coordinates then
     local x,y,z=world(o);local c=v.object_coordinates
     match=math.sqrt((-x-(c.ew*240+24468))^2+(y-(c.ns*240+24468))^2+(z-c.z*240)^2)<=2.5
    end
    if match and (not object or o.distance<object.distance) then object=o end
   end
   if not object then return failed('Route object is not visible; select it and record Use again') end
   route_pending={time=s.time,serial=s.action_serial,teleport=s.teleport_sequence};return {action='use_world',item=object.id,status='Using '..object.name}
  end
  point=point+route_direction;return result(nil,'Waypoint reached')
 end
 end
 -- Confirm the vital actually improved. A failed kit or fizzle must not loop forever.
 local function split_peas(minimum)
  if not activity_ready('components') then return end
  if s.components_required==false then pea_pending=nil;return end
  if not pea_pending and (not option('split_peas',true) or #(p.pea_recipes or {})==0) then return end
  local counts,items={},{};for _,v in ipairs(inventory) do if not v.equipped and v.name then counts[v.name]=(counts[v.name] or 0)+(v.count or 1);items[v.name]=v end end
  if pea_pending then local q=pea_pending
   if (counts[q.output] or 0)>q.before then pea_pending=nil;item_attempts['pea'..q.output]=0
   elseif s.time>=q.deadline or ((s.action_serial or 0)~=q.serial and (s.action_error or 0)~=0) then return failed('Pea splitting was not confirmed; check supplies and pack space')
   else return result(nil,'Waiting for '..q.output..' from splitting') end
  end
  if not option('split_peas',true) then return end
  local selected,all={},false
  for _,v in ipairs(p.assist_items or {}) do if v.type==9 then selected[v.name]=true elseif v.type==11 and v.name=='[All Peas]' then all=true end end
  for _,recipe in ipairs(p.pea_recipes or {}) do
   if (all or selected[recipe.output]) and (counts[recipe.output] or 0)<minimum then
    local tool,input=items[recipe.tool],items[recipe.input]
    if tool and input and (input.count or 1)>0 then
     if (s.combat_mode or 1)~=1 then return {action='combat_mode',mode=1,status='Preparing to split '..recipe.input} end
     if (item_attempts['pea'..recipe.output] or 0)>=3 then return failed('Pea splitting exceeded retry limit') end
     item_attempts['pea'..recipe.output]=(item_attempts['pea'..recipe.output] or 0)+1
     pea_pending={output=recipe.output,before=counts[recipe.output] or 0,serial=s.action_serial or 0,deadline=s.time+15}
     return {action='apply_item',item=tool.id,target=input.id,status='Splitting '..recipe.input}
    end
   end
  end
 end
 local critical_components=split_peas(option('component_critical',4));if critical_components then return critical_components end
 if recovery_pending then
  local v=recovery_pending.vital;local value=v==2 and s.health or v==4 and s.stamina or s.mana
  local failed=s.action_serial~=recovery_pending.serial and (s.action_error or 0)~=0
  -- Vital replication and UseDone may arrive in either order. The authoritative
  -- vital increase is enough; do not accumulate failures across successful uses.
  if not failed and value>recovery_pending.before then item_attempts['recover'..v]=0;recovery_pending=nil
  elseif not failed and s.time<recovery_pending.expires then return result(nil,'Waiting for recovery result')
  else
   -- A rejected or unconfirmed preferred method yields to the other kind.
   -- Otherwise a bad kit could consume all retries without ever trying a heal.
   if recovery_pending.spell then
    unavailable_spells[recovery_pending.spell]=math.max(unavailable_spells[recovery_pending.spell] or 0,s.time+30)
    until_time['recover_spell'..v]=s.time+15
    catalog_snapshot=nil;by_id,spells=index_spells(s,option('skill_margin',30))
   elseif recovery_pending.item then until_time['recover_supply'..v]=s.time+15 end
   if recovery_pending.handler then until_time[recovery_pending.handler]=s.time+15 end
   recovery_pending=nil
  end
 end
 local recovery_missing={}
 local function recovery(vital,percent,threshold)
  if not activity_ready('recovery'..vital) then return end
  if percent>=threshold then item_attempts['recover'..vital]=0;return end
  if (item_attempts['recover'..vital] or 0)>=3 then return failed('Recovery failed repeatedly; check supplies, skill and components') end
  local function pending(handler)
   item_attempts['recover'..vital]=(item_attempts['recover'..vital] or 0)+1
   recovery_pending={vital=vital,before=vital==2 and s.health or vital==4 and s.stamina or s.mana,expires=s.time+8,serial=s.action_serial,handler=handler}
  end
  local function can_prepare_spell()
   if s.time<(until_time['recover_spell'..vital] or 0) then return false end
   if not s.inventory then return true end
   for _,item in ipairs(inventory) do
    if band(item.type,0x8000)~=0 and (item.equipped or (item.identified and item.can_wield)) then return true end
   end
   return false
  end
  local function recover_cast(spell,handler)
   local equipment=caster();if equipment then return equipment end
   pending(handler);recovery_pending.spell=spell.id
   local action=cast(s,spell,s.player,'Recovering with '..(spell.name or 'spell'));last_cast.recovery=true
   return action
  end
  local function named_supply(item,kind)
   if not p.assist_items then return true end
   for _,a in ipairs(p.assist_items) do if a.type==kind and a.name==item.name then return true end end
   return false
  end
  local function assess_supply(handlers)
   for _,v in ipairs(inventory) do
    local candidate=not v.identified and v.usable and not v.equipped and allowed(p.consumables,v.wcid)
     and ((v.healing_kit and trained[21] and (v.structure or 0)>0) or band(v.type,32)~=0 or contains(p.consumables,v.wcid))
    if candidate and handlers then
     candidate=false
     for _,h in ipairs(handlers) do
      local kit=h.handler=='Kit Recharge'
      if h.vital==vital and h.stance==(s.combat_mode==8 and 1 or 2) and (kit or h.handler=='Recharge With Food')
       and (v.healing_kit==true)==kit and named_supply(v,vital-2+(kit and 0 or 1)) then candidate=true;break end
     end
    end
    if candidate and (item_attempts['id'..v.id] or 0)<3 then return assess(v) end
   end
  end
  local function source_ready(source)
   if source==0 then return true end
   local percent=source==2 and hp or source==4 and sp or mp
   local reserve=source==2 and option('health_threshold',65) or source==4 and option('stamina_threshold',50) or option('mana_threshold',45)
   return percent>math.max(source==2 and 50 or 25,reserve)
  end
  local function kit_ready(v)
   if not trained[21] or (v.structure or 0)<=0 or (vital~=4 and (s.stamina or 0)<15) then return false end
   if s.combat_mode==8 and not option('kits_in_magic',true) then return false end
   local minimum=option('kit_min_success',0)
   if minimum<=0 then return true end
   local healing=(s.skills or {})['21'];if not healing then return false end
   local missing=vital==2 and s.max_health-s.health or vital==4 and s.max_stamina-s.stamina or s.max_mana-s.mana
   -- VT's kit estimate uses current Healing + kit bonus against twice the
   -- missing vital, with a 10% combat difficulty penalty. This is a prediction;
   -- only the server's subsequent vital update confirms a successful heal.
   local difficulty=math.ceil(math.max(0,missing)*(s.combat_mode==1 and 2 or 2.2))
   local chance=100/(1+math.exp(math.max(-700,math.min(700,-.03*((healing.current or 0)+(v.boost or 0)-difficulty)))))
   return chance>=minimum
  end
  local function better_kit(v,best)
   return not best or (v.heal_mod or 1)>(best.heal_mod or 1)
    or ((v.heal_mod or 1)==(best.heal_mod or 1) and v.structure<best.structure)
  end
  local function use_supply(best,handler)
   if best.healing_kit and option('kit_peace',false) and s.combat_mode~=1 then return {action='combat_mode',mode=1,status='Entering peace to use healing kit'} end
   pending(handler);recovery_pending.item=best.id;return {action='use_item',item=best.id,status='Recovering with '..best.name}
  end
  if p.recharge_handlers and option('use_imported_recovery_order',true) then
   local stance=s.combat_mode==8 and 1 or 2;local last
   for i,h in ipairs(p.recharge_handlers) do if h.vital==vital and h.stance==stance then last=i end end
   for i,h in ipairs(p.recharge_handlers) do
    local key='handler'..i
    if h.vital==vital and h.stance==stance and (i==last or (percent>=h.min and percent<=h.max)) and s.time>=(until_time[key] or 0) then
     local best,best_rank;local kind=h.handler
     if kind=='Recharge With Food' or kind=='Kit Recharge' then
      local kit=kind=='Kit Recharge'
      for _,v in ipairs(inventory) do
       if s.time>=(until_time['recover_supply'..vital] or 0) and (v.count or 1)>0 and v.identified and v.usable and v.boost_vital==vital and (v.healing_kit==true)==kit and allowed(p.consumables,v.wcid)
        and named_supply(v,vital-2+(kit and 0 or 1)) and (not kit or kit_ready(v))
        and ((kit and better_kit(v,best)) or (not kit and (not best or (v.boost or 0)>(best.boost or 0)))) then best=v end
      end
      if best then return use_supply(best,key) end
     else
      local conversion=kind~='Regular Spell'
      local source=kind:sub(1,6)=='Health' and hp or kind:sub(1,7)=='Stamina' and sp or mp
      local reserve=kind:sub(1,6)=='Health' and option('health_threshold',65) or kind:sub(1,7)=='Stamina' and option('stamina_threshold',50) or option('mana_threshold',45)
      if not conversion or source>(vital==2 and 10 or reserve) then
       for _,v in ipairs(spells) do
        local destination,from=recovery_spell(v)
        local regular=destination==vital and (from==0 or (vital==6 and from==4 and source_ready(from)))
        local handler_source=kind:sub(1,6)=='Health' and 2 or kind:sub(1,7)=='Stamina' and 4 or 6
        local matches=conversion and destination==vital and from==handler_source or (not conversion and regular)
        local rank=not conversion and vital==6 and from==4 and 2 or 1
        if matches and v.caster_target and (v.duration or 0)==0 and (not best or rank>best_rank or (rank==best_rank and stronger_spell(v,best))) then best=v;best_rank=rank end
       end
      end
      if best and conversion and vital==2 then
       local healing,heal_power=0,-1
       local amounts={['Heal Self I']=17,['Heal Self II']=25,['Heal Self III']=32,['Heal Self IV']=45,['Heal Self V']=67,['Heal Self VI']=87,["Adja's Intervention"]=115,['Incantation of Heal Self']=135}
       for _,v in ipairs(spells) do local destination,from=recovery_spell(v);if destination==2 and from==0 and v.power>heal_power then heal_power=v.power;healing=amounts[v.name] or 10 end end
       local multiplier=kind=='Stamina to Health' and option('stamina_health_multiplier',1) or option('mana_health_multiplier',1)
       local source_value=kind=='Stamina to Health' and s.stamina or math.max(0,s.mana-30)
       local ratio,cap=1.75,math.huge
       local levels={{' I',.9,50},{' II',1,100},{' III',1.1,150},{' IV',1.2,200},{' V',1.35,math.huge},{' VI',1.5,math.huge}}
       for _,v in ipairs(levels) do if best.name:sub(-#v[1])==v[1] then ratio=v[2];cap=v[3];break end end
       local missing=s.max_health-s.health;local gain=math.min(missing,cap,math.floor(source_value*ratio))
       if healing>missing or healing*multiplier>=missing or healing*multiplier>=gain then best=nil end
      end
      if best and can_prepare_spell() then return recover_cast(best,key) end
     end
    end
   end
   local appraisal=assess_supply(p.recharge_handlers);if appraisal then return appraisal end
   recovery_missing[#recovery_missing+1]=vital
   return -- An explicit handler list must not fall back to unrelated supplies.
  end
  local best
  for _,v in ipairs(inventory) do
   if s.time>=(until_time['recover_supply'..vital] or 0) and (v.count or 1)>0 and v.identified and v.usable and v.boost_vital==vital and allowed(p.consumables,v.wcid)
    and (not v.healing_kit or kit_ready(v))
    and (not best or (v.boost or 0)>(best.boost or 0)) then best=v end
  end
  local supply=best;best=nil
  if supply and option('recovery_supplies_first',false) then return use_supply(supply) end
  local best_rank=0;local casting_available=can_prepare_spell()
  for _,v in ipairs(spells) do
   local destination,from=recovery_spell(v)
   if destination==vital and source_ready(from) and casting_available then
    -- VT Regular Spell uses Stamina to Mana; direct Mana Boost is fallback.
    local rank=vital==6 and from==4 and 3 or from==0 and 2 or 1
    if not best or rank>best_rank or (rank==best_rank and stronger_spell(v,best)) then best=v;best_rank=rank end
   end
  end
  if best then return recover_cast(best) end
  if supply then return use_supply(supply) end
  local appraisal=assess_supply();if appraisal then return appraisal end
  recovery_missing[#recovery_missing+1]=vital
 end
 if option('recovery',true) then
  local action=recovery(2,hp,not target and math.max(option('idle_health_threshold',0),option('health_threshold',65)) or option('health_threshold',65))
   or recovery(4,sp,not target and math.max(option('idle_stamina_threshold',0),option('stamina_threshold',50)) or option('stamina_threshold',50))
   or recovery(6,mp,not target and math.max(option('idle_mana_threshold',0),option('mana_threshold',45)) or option('mana_threshold',45))
  if action then return action end
 end
 -- Even without a ready heal, keep fighting; stopping here abandons the player.
 local health_critical=hp<option('stop_health',15)
 -- VT c4/c8/cx only dispel temporary elemental vulnerabilities, not every
 -- negative enchantment. A UseDone acknowledgement alone is not success.
 activity='dispel'
 local vulnerabilities={};local lowest_vulnerability
 for _,e in ipairs(s.harmful_enchantments or {}) do
  if e.category>=102 and e.category<=114 and e.category%2==0 and (e.remaining or 0)>0 then
   vulnerabilities[e.id]=true;lowest_vulnerability=math.min(lowest_vulnerability or math.huge,e.power or math.huge)
  end
 end
 if dispel_pending then
  local q=dispel_pending;local removed=false;for id in pairs(q.before) do if not vulnerabilities[id] then removed=true;break end end
  if removed then dispel_pending=nil
  elseif s.time>=q.expires or ((s.action_serial or 0)~=q.serial and (s.action_error or 0)~=0) then
   dispel_pending=nil;until_time.dispel=s.time+15
  else return result(nil,'Waiting for dispel enchantment update') end
 end
 if activity_ready('dispel') and lowest_vulnerability and s.time>=(until_time.dispel or 0) and (option('dispel_self',false) or option('dispel_items',false)) then
  local function pending_dispel()
   dispel_pending={before=vulnerabilities,expires=s.time+8,serial=s.action_serial or 0}
  end
  if option('dispel_self',false) then
   local chorizite=false;for _,v in ipairs(inventory) do if v.name=='Chorizite' and (v.count or 1)>0 then chorizite=true;break end end
   local best
   if chorizite then for _,v in ipairs(spells) do
    if v.category==250 and v.school==2 and v.caster_target and v.power>=lowest_vulnerability and (not best or v.power>best.power) then best=v end
   end end
   if best then local equipment=caster();if equipment then return equipment end;pending_dispel();return cast(s,best,s.player,'Dispelling elemental vulnerabilities') end
  end
  if option('dispel_items',false) then
   for _,choice in ipairs({{'Rune of Dispel',400},{'Society Gem of Dispelling',400},{'Black Market Gem of Dispelling',400},{'Chocolate Gromnie',350},{'Condensed Dispel Potion',350},{'Gem of Stillness',350}}) do
    if lowest_vulnerability<=choice[2] then for _,v in ipairs(inventory) do
     local cooldown=string.format('%.0f',(v.int_properties or {})['280'] or 0)
     if v.name==choice[1] and v.usable and not v.equipped and (v.count or 1)>0 and ((s.cooldowns or {})[cooldown] or 0)<=0 then
      pending_dispel();return {action='use_item',item=v.id,status='Dispelling with '..v.name}
     end
    end end
   end
  end
 end
 local fellows=s.fellowship or {}
 if helper_pending then
  local q=helper_pending;local member
  for _,m in ipairs(fellows.members or {}) do if m.id==q.target then member=m;break end end
  if member and (member.vitals_age or 1e9)<10 and (member[q.vital] or 0)>q.before then
   helper_pending=nil;item_attempts[q.key]=0
  elseif not fellows.valid or not member or s.time>=q.expires or ((s.action_serial or 0)~=q.serial and (s.action_error or 0)~=0) then
   helper_pending=nil;until_time[q.key]=s.time+15
  else return result(nil,'Waiting for fellowship recovery result') end
 end
 if activity_ready('helper') and not forced_buff and option('recovery',true) and fellows.valid then
  for index,vital in ipairs({'health','stamina','mana'}) do
   local threshold=option('helper_'..vital..'_threshold',0)
   local recipient,lowest=nil,threshold
   for _,m in ipairs(fellows.members or {}) do
    local key='helper'..tostring(m.id)..vital
    if m.id~=s.player and (m.health or 0)>0 and (m['max_'..vital] or 0)>0 and (m.vitals_age or 1e9)<10
     and m.line_of_sight==true and (m.distance or 1e9)<=option('helper_'..vital..'_range',index==3 and 40 or 74.5)
     and s.time>=(until_time[key] or 0) and (item_attempts[key] or 0)<3 then
     local percent=math.floor(pct(m[vital] or 0,m['max_'..vital]));if percent<lowest then recipient=m;lowest=percent end
    end
   end
   if recipient then
    local best
    for _,spell in ipairs(spells) do
     if spell.category==77+index*2 and not spell.caster_target and spell.beneficial and (spell.duration or 0)==0 and band(spell.flags,0x2000)==0
      and (not best or spell.power>best.power) then best=spell end
    end
    if best then
     local equipment=caster();if equipment then return equipment end
     local key='helper'..tostring(recipient.id)..vital;item_attempts[key]=(item_attempts[key] or 0)+1
     helper_pending={target=recipient.id,vital=vital,before=recipient[vital],serial=s.action_serial or 0,expires=s.time+10,key=key}
     return cast(s,best,recipient.id,'Restoring '..recipient.name..'\'s '..vital)
    end
   end
  end
 end
 local mana_action=equipment_mana(s,p,inventory,option);if mana_action then return mana_action end
 local function buffs(refresh)
 if health_critical or not activity_ready('buffs') then return end
 if forced_buff or option('buffing',true) then
  -- Consumable buffs require an observed enchantment, not merely UseDone.
  -- Failed uses rotate to the next item instead of consuming a whole stack.
  if buff_item_pending then
   local confirmed=false
   for _,e in ipairs(s.enchantments or {}) do if e.category==buff_item_pending.category and e.power>=buff_item_pending.power and e.remaining>buff_item_pending.before then confirmed=true;break end end
   if confirmed then
    if forced_buff then forced_buff.done['item'..buff_item_pending.name]=true end
    item_attempts['buffitem'..buff_item_pending.name]=0;buff_item_pending=nil
   elseif s.time<buff_item_pending.expires then return result(nil,'Waiting for consumable buff confirmation')
   else until_time['buffitem'..buff_item_pending.name]=s.time+30;buff_item_pending=nil end
  end
  local settings={};for k,val in pairs(p) do settings[k]=val end;for k,val in pairs(current) do settings[k]=val end
  if refresh then settings.refresh_seconds=refresh end
  local v,id,blocked=next_buff(s,settings)
  -- Missing components or rejected families must not stop the rest of a run.
  if v then
   local key=tostring(id)..':'..v.category
   if s.time<(until_time[key] or 0) then return result(nil,'Waiting for buff confirmation') end
   if mp<option('stop_mana',10) then return failed('Mana too low to buff; configure recovery') end
   local equipment=caster()
   if (retries[key] or 0)>=3 or (equipment and equipment.action=='activity_failed') then
    buff_skips[key]=s.time+120;retries[key]=nil;until_time[key]=nil;buff_plan=nil
    if forced_buff then forced_buff.skipped=forced_buff.skipped or {};forced_buff.skipped[key]=true end
    return result(nil,'Skipping failed buff: '..(v.name or tostring(v.id))..'; continuing cycle')
   end
   if equipment then return equipment end
   retries[key]=(retries[key] or 0)+1;until_time[key]=s.time+12
   local action=cast(s,v,id,(forced_buff and 'Force Buff: ' or 'Buffing: ')..(v.name or tostring(v.id)))
   if forced_buff then last_cast.force_cycle=forced_buff;last_cast.power=v.power end
   return action
  end
  for _,b in ipairs(p.buff_items or {}) do
   local spell=by_id[b.spell];local item=usable_names[b.name]
   if spell and item and (band(spell.flags,0x2000)==0 or s.in_fellowship) then
    local active,before=false,0;for _,e in ipairs(s.enchantments or {}) do if e.category==spell.category and e.power>=spell.power then before=math.max(before,e.remaining);if e.remaining>(refresh or option('refresh_seconds',60)) then active=true end end end
    local needed=forced_buff and not forced_buff.done['item'..b.name] or (not forced_buff and not active)
    if needed and (item_attempts['buffitem'..b.name] or 0)<3 then
     local key='buffitem'..b.name
     if s.time<(until_time[key] or 0) then return result(nil,'Waiting before retrying consumable buff: '..b.name) end
     item_attempts[key]=(item_attempts[key] or 0)+1
     buff_item_pending={name=b.name,category=spell.category,power=spell.power,before=before,expires=s.time+12}
     return {action='use_item',item=item.id,status='Buffing with '..b.name}
    end
   end
  end
  meta.forcebuff=false
  if forced_buff then
   local skipped=blocked or (forced_buff.skipped and next(forced_buff.skipped))
   for _,b in ipairs(p.buff_items or {}) do if (item_attempts['buffitem'..b.name] or 0)>=3 then skipped=true end end
   local status=skipped and 'Force Buff finished; unavailable or failed buffs skipped' or 'Force Buff complete'
   local request=forced_buff.request;forced_buff=nil;buff_plan=nil
   if request~=0 then return {action='force_buff_done',request=request,status=status} end
   return result(nil,status)
  end
 end
 end
 local buff_action=buffs();if buff_action then return buff_action end
 local normal_components=split_peas(option('component_normal',20));if normal_components then return normal_components end
 -- Tell requests use Other spells, never redirect Self spells to the requester.
 local request=s.buff_request
 if request and activity_ready('buff_others') then
  local function finish(message)
   other_job=nil;return {action='buff_request_done',request=request.id,status=message}
  end
  if not option('buffing',true) or not option('buff_others',false) then return finish('Buff request cancelled: buffing is disabled') end
  if not other_job or other_job.id~=request.id then other_job={id=request.id,done={},failed={},attempts={},unavailable={},count=0} end
  local job=other_job
  if s.time>(request.expires or math.huge) then return finish('Buff request expired: '..request.name) end
  if not request.present or request.distance>option('buff_other_range',20) then
   return finish('Buff request cancelled: '..request.name..' is not nearby; continuing to the next request')
  end
  if request.started==false then return {action='buff_request_start',request=request.id,status='Starting buffs for '..request.name} end
  if job.pending then
   local pending=job.pending
   local confirmed=(s.action_serial or 0)~=pending.serial and s.last_spell==pending.spell
   if not confirmed and s.time<pending.deadline then return result(nil,'Waiting for requested buff confirmation') end
   if confirmed and (s.action_error or 0)==0 and s.last_spell_confirmed~=false then job.done[pending.key]=true;job.count=job.count+1
   elseif (job.attempts[pending.key] or 0)>=3 then job.failed[pending.key]=pending.name end
   job.pending=nil
  end
  local common={1,3,5,7,9,11,37,39,41,51,53,67,69,77,93,95,97,101,103,105,107,109,111,113,115}
  local roles={mage={43,45,47,49,645},heavy={31,593},light={17},unarmed={17},finesse={23},twohanded={593},melee={17,23,31,593},missile={19,677}}
  local wanted={};for _,c in ipairs(common) do wanted[c]=true end
  for _,c in ipairs(roles[request.role] or {}) do wanted[c]=true end
  -- Modern retail uses Light Weapons for unarmed attacks. Every melee role
  -- includes Dual Wield, Shield, Dirty Fighting, Recklessness and Sneak Attack.
  if request.role~='mage' and request.role~='missile' then for _,c in ipairs({668,674,665,671,677}) do wanted[c]=true end end
  -- These are retail item buff families; DAT target masks determine which
  -- of the requester's server-known equipped items can receive each spell.
  for _,c in ipairs({152,154,156,158,160,162,164,166,168,170,172,174,195}) do wanted[c]=true end
  local best={}
  for _,v in ipairs(spells) do
   if wanted[v.category] and buff_tier(v,p) and v.beneficial and not v.caster_target and not v.self_buff and (v.duration or 0)>0 and s.time>=(job.unavailable[v.id] or 0)
    and not contains(p.excluded_buffs,v.category) and (not best[v.category] or v.power>best[v.category].power) then best[v.category]=v end
  end
  local categories={};for c in pairs(wanted) do categories[#categories+1]=c end;table.sort(categories)
  for _,category in ipairs(categories) do
   local v=best[category]
   if not v then job.failed['family'..category]='unavailable family '..category
   else
    local targets={request.player}
    if v.school==3 then
     targets={}
     for _,item in ipairs(request.equipment or {}) do
      if band(item.type,v.target_type or 0)~=0 and (item.count or 1)==1 then targets[#targets+1]=item.id end
     end
     if #targets==0 then job.failed['family'..category]='equipment unavailable for '..(v.name or category) end
    end
    for _,id in ipairs(targets) do
     local key=tostring(id)..':'..category
     if not job.done[key] and not job.failed[key] then
      if mp<option('stop_mana',10) then return finish('Buff request incomplete: mana too low for '..request.name) end
      local equipment=caster();if equipment then return equipment end
      job.attempts[key]=(job.attempts[key] or 0)+1
      job.pending={key=key,spell=v.id,serial=s.action_serial or 0,name=v.name or tostring(v.id),deadline=s.time+15}
      local intent=cast(s,v,id,'Buffing '..request.name..': '..(v.name or tostring(v.id)))
      last_cast.request=request.id;return intent
     end
    end
   end
  end
  local missing=0;for _ in pairs(job.failed) do missing=missing+1 end
  return finish(string.format('%s: %d buffs confirmed%s',request.name,job.count,missing>0 and (', '..missing..' unavailable or failed (skills, components or visible equipment)') or '; requested set complete'))
 else other_job=nil end
 local combat=activity_ready('combat') and option('combat','off') or 'off'
 if combat~='off' and not option('manual_combat',false) then combat='auto' end
 local pet_cooldown=(s.cooldowns or {})['213'] or 0
 activity='pets'
 local refill=refill_pet(s,p,inventory,option,weapon_allowed);if refill then return refill end
 if pet_pending then
  if s.owned_pet or pet_cooldown>0 then pet_pending=nil
  elseif ((s.action_serial or 0)~=pet_pending.serial and (s.action_error or 0)~=0) or s.time>pet_pending.deadline then
   return failed('Pet summon was not confirmed; check charges and requirements')
  else return result(nil,'Waiting for summoned pet or server cooldown') end
 end
 if activity_ready('pets') and not pet_refill_pending and combat~='off' and option('summon_pets',false) and not s.owned_pet and pet_cooldown<=0 then
  local skill=(s.skills or {})['54'] or {}
  if (skill.training or 0)>=2 then
   local pet_target,pet_rule,count=nil,nil,0
   local range=option('pet_range_mode',0)==1 and option('pet_range',5) or option('radius',30)
   for _,t in ipairs(s.targets or {}) do
    local rule=monster_rule(t,p)
    if t.distance<range and t.line_of_sight~=false and not rule.ignore and rule.pet_damage_type~=98 and not contains(p.ignore_monsters,t.name) then
     count=count+1
     if not pet_target or (rule.priority or 0)>(pet_rule.priority or 0) or ((rule.priority or 0)==(pet_rule.priority or 0) and t.distance<pet_target.distance) then pet_target=t;pet_rule=rule end
    end
   end
   if pet_target and count>=option('pet_min_targets',1) then
    local best,best_rank,best_level=nil,-1,-1
    local preferred=({[0]=2,4,1,32,64,16,8})[pet_rule.pet_damage_type]
    local attack_element=pet_rule.damage_type or option('damage_type',0)
    if attack_element==0 then
     local score=-1
     for _,v in ipairs(spells) do local e=element(v);local rank=(v.power or 0)*resistance(pet_target,e)
      if e~=0 and not v.beneficial and rank>score then attack_element=e;score=rank end
     end
     if combat~='magic' then for _,v in ipairs(inventory) do if v.equipped and (band(v.type,1)~=0 or band(v.slots,0x800000)~=0) then
      local rank=-1;for bit=0,7 do local e=1<<bit;if band(v.damage_type,e)~=0 and resistance(pet_target,e)>rank then attack_element=e;rank=resistance(pet_target,e) end end
     end end end
    end
    for _,v in ipairs(inventory) do local props=v.int_properties or {}
     -- VT's equipment list explicitly opts devices in. A native profile can
     -- instead select device classes through its equipment picker.
     local selected=weapon_ids[v.id] or weapon_types[v.wcid]
     local required_skill=(s.skills or {})[tostring(props['366'] or 54)] or skill
     if selected and v.identified and v.usable and props['280']==213 and (v.structure or 0)>0
      and (props['369'] or 0)<=(s.level or 0) and (props['367'] or 0)<=(required_skill.current or 0)
      and ((props['368'] or 0)==0 or (((s.skills or {})[tostring(props['368'])] or {}).training or 0)>=3)
      and ((props['362'] or 0)==0 or props['362']==(s.char_ints or {})['362']) then
      local damage=({[1]=4,[32]=8,[64]=64,[128]=16,[256]=32})[v.icon_effects] or 4
      local rank=resistance(pet_target,damage)
      if damage==preferred then rank=rank+1000000
      elseif (preferred or (pet_rule.pet_damage_type or 101)==101) and damage==attack_element then rank=rank+10000 end
      local level=props['369'] or 0
      if rank>best_rank or (rank==best_rank and level>best_level) then best=v;best_rank=rank;best_level=level end
     end
    end
    if best then
     -- Summoning devices can be used while armed. Keep the current stance;
     -- dropping to peace here unnecessarily removes combat defenses.
     pet_pending={serial=s.action_serial or 0,deadline=s.time+15}
     return {action='use_item',item=best.id,status='Summoning with '..best.name}
    end
   end
  end
 end
 if combat=='off' then debuff_pending=nil;debuff_scan=nil;if locked_target then locked_target=nil;return result('cancel_attack','Combat disabled') end end
 local function loot()
 if not activity_ready('loot') then return end
 if corpse_pending then
  if s.container==corpse_pending.id then item_attempts['corpse'..corpse_pending.id]=0;corpse_pending=nil
  elseif s.time<corpse_pending.deadline and not ((s.action_serial or 0)~=corpse_pending.serial and (s.action_error or 0)~=0) then return result(nil,'Approaching / waiting for corpse to open')
  else
   local key='corpse'..corpse_pending.id;local blacklisted=(item_attempts[key] or 0)>=option('corpse_attempts',30)
   corpses[corpse_pending.id]=s.time+(blacklisted and option('corpse_blacklist_seconds',200) or 10)
   if blacklisted then item_attempts[key]=0 end
   corpse_pending=nil
  end
 end
 -- A corpse can close/disappear with its final transfer. Confirm ownership
 -- independently of the UI container, before moving on or applying loot jobs.
   if loot_pending then
    local present=false;for _,v in ipairs(s.contents or {}) do if s.container==loot_pending.container and v.id==loot_pending.id and v.count>=loot_pending.count then present=true end end
    local owned,received_exact=0,false
    for _,v in ipairs(inventory) do
     if v.wcid==loot_pending.wcid then owned=owned+(v.count or 1) end
     if v.id==loot_pending.id and (v.count or 1)>=loot_pending.amount then received_exact=true end
    end
    if not received_exact and (present or owned<loot_pending.expected) then
     if s.time<loot_pending.expires then return result(nil,'Waiting for loot transfer') end
     -- A timeout does not prove full packs or lost ownership. Release this
     -- corpse so combat/recovery continue; retry it later without flooding Use.
     return failed('Loot transfer unconfirmed; leaving items untouched')
    end
    if loot_pending.after and loot_pending.after~='keep' then
     if #loot_jobs>=128 then return failed('Too many pending loot actions; visit a vendor') end
     local received
     for _,i in ipairs(inventory) do if i.wcid==loot_pending.wcid and not loot_pending.before[i.id] and (i.count or 1)==loot_pending.amount then
      if received then return failed('Loot transfer has multiple matching new items; review inventory before continuing') end
      received=i
     end end
     if not received then return failed('Loot was merged or changed; review inventory before applying a destructive rule') end
     loot_jobs[#loot_jobs+1]={id=received.id,count=loot_pending.amount,action=loot_pending.after,wcid=loot_pending.wcid}
    end
    if s.container~=loot_pending.container then corpses[loot_pending.container]=s.time+30 end
    loot_pending=nil
   end
 if option('looting',false) and (#(p.loot_rules or {})>0 or option('read_unknown_scrolls',false)) then
  if s.container and s.container~=0 then
   if s.container_is_corpse==false then return result(nil,'Waiting for the open container to close') end
   if s.contents_ready==false then return result(nil,'Waiting for corpse item descriptions') end
   -- Charge actual work, not all the conditions/spells a rule might examine.
   -- Stable ordering prevents snapshot enumeration changes restarting the scan.
   local rules=p.loot_rules or {};local contents=s.contents or {}
   table.sort(contents,function(a,b)return a.id<b.id end)
   local signature=tostring(s.container)..':'..#contents..':'..#rules..':'..(p.vt_revision or 0)..':'..(p.loot_revision or 0)
   for _,v in ipairs(contents) do signature=signature..':'..v.id..':'..v.count..':'..tostring(v.identified) end
   if loot_scan.signature~=signature then loot_scan={signature=signature,item=1,rule=1} end
   local owned,owned_names={},{};for _,v in ipairs(inventory) do owned[v.wcid]=(owned[v.wcid] or 0)+(v.count or 1);local name=v.name or '';owned_names[name]=(owned_names[name] or 0)+(v.count or 1) end
   local learned,carried_scrolls={},{}
   if option('read_unknown_scrolls',false) then
    for _,spell in ipairs(s.spells or {}) do if spell.known~=false then learned[spell.id]=true end end
    for _,id in ipairs(s.known_spells or {}) do learned[id]=true end
    for _,v in ipairs(inventory) do if v.object_class==42 and (v.spell or 0)>0 then carried_scrolls[v.spell]=true end end
   end
   local pending_appraisals=0
   for _,item in ipairs(contents) do if not item.identified and s.time<(until_time['id'..item.id] or 0) then pending_appraisals=pending_appraisals+1 end end
   local function progress()
    local r=result(nil,'Checking loot rules: item '..loot_scan.item..'/'..#contents..', rule '..loot_scan.rule..'/'..#rules)
    r.continue_work=true;return r
   end
   while loot_scan.item<=#contents do
    local item=contents[loot_scan.item]
    local item_rules=rules
    -- VT considers learnable unknown scrolls before the user's loot rules.
    -- Carried copies suppress duplicates while a new scroll waits to be read.
    if option('read_unknown_scrolls',false) and item.object_class==42 and item.scroll_can_learn and (item.spell or 0)>0 and not learned[item.spell] and not carried_scrolls[item.spell] then
     item_rules={{action='read',label='Learn unknown spell'}}
    end
    if band(item.type,0x80000)~=0 and (p.mana_stone_loot_count or 0)>0 then
     for _,a in ipairs(p.assist_items or {}) do if a.type==8 and a.name==item.name then
      item_rules={{action='keep',name=item.name,name_mode='exact',keep_up_to=p.mana_stone_loot_count,count_by_name=true,label='Mana stone supplies'}};break
     end end
    end
    while loot_scan.rule<=#item_rules do
     local rule=item_rules[loot_scan.rule]
     if not workavailable() then return progress() end
     loot_scan.evaluation=loot_scan.evaluation or {}
     local match=rule_match(rule,item,s,loot_scan.evaluation)
     if match=='pending' then return progress() end
     loot_scan.evaluation=nil
     if match==nil then
      local key='id'..item.id
      if (item_attempts[key] or 0)>=3 and s.time>=(until_time[key] or 0) then return failed('Unable to appraise loot; check appraisal skills') end
      -- VT's ID queue advances while earlier replies are pending. Preserve
      -- first-match rule order for this item, but keep examining other items.
      loot_scan.waiting=true
      if pending_appraisals<4 and s.time>=(until_time[key] or 0) then
       item_attempts[key]=(item_attempts[key] or 0)+1;until_time[key]=s.time+10
       loot_scan.item=loot_scan.item+1;loot_scan.rule=1
       return {action='identify',item=item.id,status='Inspecting '..item.name..' for loot rule'}
      end
      break
     elseif match then
      -- First match wins, including Skip and a satisfied quantity limit.
      local have=rule.count_by_name and (owned_names[item.name] or 0) or (owned[item.wcid] or 0)
      local amount=rule.keep_up_to and rule.keep_up_to>0 and math.min(item.count,math.max(0,rule.keep_up_to-have)) or item.count
      if rule.action~='skip' and amount>0 then
       local before={};for _,v in ipairs(inventory) do before[v.id]=true end
       loot_scan={};loot_pending={container=s.container,before=before,amount=amount,id=item.id,count=item.count,wcid=item.wcid,expected=(owned[item.wcid] or 0)+amount,expires=s.time+10,after=rule.action}
       return {action='loot',item=item.id,amount=amount,status='Looting '..item.name..' ('..(rule.label or 'matching rule')..')'}
      end
      break
     end
     loot_scan.rule=loot_scan.rule+1
    end
    loot_scan.item=loot_scan.item+1;loot_scan.rule=1
   end
   local waiting=loot_scan.waiting;loot_scan={}
   if waiting then return result(nil,'Waiting for loot appraisal') end
   corpses[s.container]=s.time+300;return {action='close_corpse',status='Loot complete'}
  end
 end
 -- Existing possessions require the explicit inventory salvage option below.
 -- Importing a loot profile alone must never queue them for destruction.
 if loot_job_pending then
  local job=loot_job_pending;local exists=false;for _,i in ipairs(inventory) do if i.id==job.id then exists=true end end
  if not exists then table.remove(loot_jobs,job.index);loot_job_pending=nil
  elseif (s.action_serial or 0)~=job.serial and (s.action_error or 0)~=0 then return failed('Loot '..job.action..' was rejected by the server')
  elseif s.time>=job.deadline then return failed('Loot '..job.action..' did not complete; item remains in inventory')
  else return result(nil,'Waiting for loot '..job.action) end
 end
 if not s.container or s.container==0 then
  for index,job in ipairs(loot_jobs) do if job.action~='sell' or (s.vendor or 0)~=0 then
   local item;for _,i in ipairs(inventory) do if i.id==job.id and i.wcid==job.wcid then item=i;break end end
   if not item then table.remove(loot_jobs,index);return result(nil,'Queued loot item is no longer present') end
   if item.equipped or (item.count or 1)~=job.count then return failed('Queued loot item was equipped or its stack changed; review loot rules') end
   local tool
   if job.action=='salvage' then
    if not salvage_candidate(item) or salvage_protected(item) then table.remove(loot_jobs,index);return result(nil,'Keeping protected or unsalvageable item: '..item.name) end
    if not item.identified then return assess(item) end
    for _,i in ipairs(inventory) do if i.object_class==40 and i.resource_available~=false then tool=i.id;break end end
    if not tool then return failed('Salvage rule requires an Ust') end
   end
   loot_job_pending={index=index,id=item.id,action=job.action,serial=s.action_serial or 0,deadline=s.time+15}
   return {action=job.action,item=item.id,tool=tool,status=job.action..': '..item.name}
  end end
 end
 if option('salvage_inventory',false) and (not s.container or s.container==0) then
  local scan=inventory_salvage_scan;local rules=p.loot_rules or {}
  local revision=tostring(s.inventory_revision)..':'..tostring(s.appraisal_revision)..':'..(p.loot_revision or 0)..':'..(p.vt_revision or 0)
  if scan.revision~=revision then scan={revision=revision,item=1,rule=1};inventory_salvage_scan=scan end
  if not scan.done then
  local has_salvage=false;for _,rule in ipairs(rules) do if rule.enabled~=false and rule.action=='salvage' then has_salvage=true;break end end
  if not has_salvage then scan.done=true else
  local ordered,present={},{};for _,v in ipairs(inventory) do ordered[#ordered+1]=v;present[v.id]=true end;table.sort(ordered,function(a,b)return a.id<b.id end)
  for id in pairs(inventory_salvage_blocked) do if not present[id] then inventory_salvage_blocked[id]=nil end end
  local function progress() local r=result(nil,'Checking inventory salvage rules: item '..scan.item..'/'..#ordered);r.continue_work=true;return r end
  while scan.item<=#ordered do
   local item=ordered[scan.item]
   if salvage_candidate(item) and not inventory_salvage_blocked[item.id] then
    if not item.identified then
     if (item_attempts['id'..item.id] or 0)>=3 then inventory_salvage_blocked[item.id]=true
     else return assess(item) end
    elseif not salvage_protected(item) then
     while scan.rule<=#rules do
      if not workavailable() then return progress() end
      scan.evaluation=scan.evaluation or {};local match=rule_match(rules[scan.rule],item,s,scan.evaluation)
      if match=='pending' then return progress() end
      scan.evaluation=nil
      if match==nil then inventory_salvage_blocked[item.id]=true;break end
      if match then
       if rules[scan.rule].action=='salvage' then
        -- Freeze identity and count; execute through the same validated queue
        -- as newly looted salvage, never Sell/Read a pre-existing possession.
        inventory_salvage_blocked[item.id]=true
        loot_jobs[#loot_jobs+1]={id=item.id,count=item.count or 1,action='salvage',wcid=item.wcid}
        scan.item=scan.item+1;scan.rule=1
        return result(nil,'Queued inventory salvage: '..item.name)
       end
       break -- First match wins, including Keep, Skip and disabled quantities.
      end
      scan.rule=scan.rule+1
     end
    end
   end
   scan.item=scan.item+1;scan.rule=1
  end
  scan.done=true
  end
  end
 else inventory_salvage_scan={} end
 if option('looting',false) and (#(p.loot_rules or {})>0 or option('read_unknown_scrolls',false)) then
  for _,c in ipairs(s.corpses or {}) do if c.line_of_sight~=false and c.distance<=option('loot_range',15) and s.time>=(corpses[c.id] or 0) then
   if (c.ownership_available or option('loot_only_rare',false)) and not c.identified then return assess(c) end
   local permitted=true
   if c.ownership_available then
    local name=corpse_owner_name((s.char_strings or {})['1']);local killer=corpse_owner_name(c.killer)
    permitted=killer==name and name~=''
    if not permitted and not c.rare then
     local fellow
     for _,m in ipairs((s.fellowship or {}).members or {}) do if killer~='' and corpse_owner_name(m.name)==killer then fellow=m;break end end
     if fellow then permitted=option('loot_fellow_corpses',false) and (fellow.share_loot or (c.observed_age or 0)>=100)
     else permitted=option('loot_all_corpses',false) and (c.observed_age or 0)>=100 end
    end
   end
   if permitted and (not option('loot_only_rare',false) or c.rare) then
   item_attempts['corpse'..c.id]=(item_attempts['corpse'..c.id] or 0)+1
   corpse_pending={id=c.id,deadline=s.time+20,serial=s.action_serial or 0};return {action='open_corpse',item=c.id,status='Opening '..c.name} end end end
 end
 end
 -- VT cLogic puts corpse approach and loot actions before attack only with
 -- LootPriorityBoost. An open corpse or pending receipt must not enable it.
 -- Keep loot_pending intact across combat so the server transfer is reconciled
 -- once, rather than requesting the item again or losing its follow-up rule.
 if option('loot_priority',false) then local action=loot();if action then return action end end
 if option('nav_priority',false) then
  -- Keep clear-area looting from being starved by a continuous route, without
  -- implicitly enabling LootPriorityBoost while a combat target is present.
  if not target or combat=='off' then local action=loot();if action then return action end end
  local nav=navigate();if nav then return nav end
 end
 activity='combat'
 local attack_spells,combat_inventory,best_debuffs,attack_indices={},{},{},{}
 if target and combat~='off' then
 for _,v in ipairs(spells) do
  if element(v)~=0 and not v.beneficial and v.category~=636 and v.category~=637 and v.category~=638 then
   local key=tostring(v.category)..':'..tostring(band(v.flags,0x1000))
   -- An explicit spell choice must survive family compaction.
   if not p.attack_spell or p.attack_spell==0 or p.attack_spell==v.id then
    local index=attack_indices[key]
    if not index then attack_spells[#attack_spells+1]=v;attack_indices[key]=#attack_spells
    elseif v.power>attack_spells[index].power then attack_spells[index]=v end
   end
  elseif not v.beneficial and not v.caster_target and (v.duration or 0)>0 and (not best_debuffs[v.category] or v.power>best_debuffs[v.category].power) then best_debuffs[v.category]=v end
 end
 for _,v in ipairs(inventory) do if band(v.type,0x8101)~=0 or band(v.slots,0x800000)~=0 then combat_inventory[#combat_inventory+1]=v end end
 end
 local effect_index={}
 for _,e in ipairs(s.debuffs or {}) do
 local refresh=(e.category==636 or e.category==637 or e.category==638) and 0 or option('debuff_refresh',5)
 if e.expires-s.time>refresh then
  local effects=effect_index[e.target] or {};effect_index[e.target]=effects
  effects[e.category]=math.max(effects[e.category] or 0,e.power)
 end end
 local vulns={[32]=102,[4]=104,[16]=106,[64]=108,[8]=110,[2]=112,[1]=114}
 local function wanted(rule,key) if rule and rule[key]~=nil then return rule[key] end;return option(key,false) end
 local function debuff_categories(rule,element)
  local categories={}
  if wanted(rule,'debuff_yield') then categories[#categories+1]=42 end
  if wanted(rule,'debuff_weakening') then categories[#categories+1]=642 end
  if wanted(rule,'debuff_festering') then categories[#categories+1]=643 end
  if wanted(rule,'debuff_corruption') then categories[#categories+1]=636 end
  if wanted(rule,'debuff_destructive') then categories[#categories+1]=637 end
  if wanted(rule,'debuff_corrosion') then categories[#categories+1]=638 end
  if wanted(rule,'debuff_imperil') then categories[#categories+1]=116 end
  if wanted(rule,'debuff_vuln') and vulns[element] then categories[#categories+1]=vulns[element] end
  local secondary=rule and vulns[rule.secondary_vuln]
  if secondary and (not wanted(rule,'debuff_vuln') or secondary~=vulns[element]) then categories[#categories+1]=secondary end
  if wanted(rule,'debuff_gravity') then categories[#categories+1]=78 end
  if wanted(rule,'debuff_broadside') then categories[#categories+1]=40 end
  if wanted(rule,'debuff_fester') then categories[#categories+1]=96 end
  return categories
 end
 local function missing_debuff(target,categories)
  local confirmed=effect_index[target.id] or {}
  for _,category in ipairs(categories) do
   local spell=best_debuffs[category]
   if (not spell and not option('debuff_fallback',false)) or (spell and (confirmed[category] or 0)<spell.power and (not option('debuff_fallback',false) or (item_attempts['debuff'..target.id..':'..category] or 0)<3)) then return true end
  end
  return false
 end
 local function combat_plan(target,target_rule)
  local target_combat=target_rule and target_rule.combat or combat
  local requested=target_rule and target_rule.weapon
  local explicit
  if requested and requested>0 then for _,v in ipairs(combat_inventory) do if v.id==requested and not v.auto_wield_left then explicit=v;break end end end
  -- VT falls back to automatic selection when an explicit object is no longer
  -- owned. Zero means automatic casting equipment, not the first melee item.
  if requested==0 then target_combat='magic' end
  local damage_override=target_rule and target_rule.damage_type or p.damage_type
  local attack,best_score=nil,-1
  local ring,ring_score,streak_spell,streak_score=nil,-1,nil,-1
  local arc,arc_score=nil,-1
  local use_ring=ring_enabled(target_rule)
  local magic_skill=-1
  if target_combat=='auto' then
   for _,v in ipairs(attack_spells) do
    local e=element(v)
    if e~=0 and not v.beneficial and (not damage_override or damage_override==0 or damage_override==e)
     and (not p.attack_spell or p.attack_spell==0 or p.attack_spell==v.id) then magic_skill=math.max(magic_skill,trained_skill(s,combat_school_skill[v.school])) end
   end
  end
  local bolt_enabled=not target_rule or target_rule.bolt~=false
  local streak_enabled=not target_rule or target_rule.streak~=false
  for _,v in ipairs(attack_spells) do local e=element(v)
   local streak=(v.category>=243 and v.category<=249) or v.category==639
   local is_ring=(v.category>=222 and v.category<=228) or v.category==641
   if e~=0 and not v.beneficial and (not p.attack_spell or p.attack_spell==0 or p.attack_spell==v.id) and trained_skill(s,combat_school_skill[v.school])>=magic_skill then
    local score=v.power*resistance(target,e);if damage_override and damage_override~=0 and e~=damage_override then score=-1 end
    if is_ring then if use_ring and score>ring_score then ring=v;ring_score=score end
    elseif streak then if streak_enabled and score>streak_score then streak_spell=v;streak_score=score end
    elseif band(v.flags,0x1000)~=0 then if score>arc_score then arc=v;arc_score=score end
    elseif score>best_score then attack=v;best_score=score end
   end
  end
  -- VT chooses the higher-quality eligible family first. The arc preference
  -- breaks ties; it does not discard the only usable family.
  local arc_mode=option('use_arcs',1)
  if arc and (arc_score>best_score or (arc_score==best_score and (arc_mode==3 or (arc_mode==2 and target.distance>=option('arc_range',5))))) then attack=arc;best_score=arc_score end
  -- VT keeps ordinary spells as fallback if a requested streak/ring is not
  -- usable. Ring-only rules cast with one nearby eligible target, otherwise
  -- the configured minimum applies (counted only from ring-enabled rules).
  if streak_spell and (not bolt_enabled or streak_score>best_score) then attack=streak_spell;best_score=streak_score end
  if ring and (ring_count>=option('ring_min_targets',4) or (not bolt_enabled and not streak_enabled and ring_count>0)) then attack=ring;best_score=ring_score end
  local weapon,score,best_skill=nil,-1,-1
  local unassessed,unassessed_ammo
  local ammo_scores,ammo_elements,unknown_ammo={},{},{}
  for _,a in ipairs(combat_inventory) do if band(a.slots,0x800000)~=0 then
   local kind=a.ammo_type or 0
   if not a.identified then unknown_ammo[kind]=a end
   if a.identified and a.can_wield and (not damage_override or damage_override==0 or band(a.damage_type,damage_override)~=0) then
    local rank=(a.damage or 0)*resistance(target,a.damage_type or 2)
    if a.equipped then rank=rank*1.01 end
    if rank>(ammo_scores[kind] or -1) then ammo_scores[kind]=rank;ammo_elements[kind]=a.damage_type or 2 end
   end
  end end
  for _,v in ipairs(combat_inventory) do
   local melee=band(v.type,1)~=0;local missile=band(v.type,0x100)~=0 and band(v.slots,0x800000)==0;local caster=band(v.type,0x8000)~=0
   local allowed=explicit and v.id==explicit.id or not explicit and weapon_allowed(v)
   local main_hand=option('melee_weapon',-1)
   if melee then
    if not explicit and main_hand>0 then allowed=v.id==main_hand end
    if v.auto_wield_left then allowed=false end
   end
   if not v.identified and allowed and (melee or missile or caster) then unassessed=v end
   if v.identified and v.can_wield and allowed and (explicit or (target_combat=='auto' and (melee or missile or (caster and attack))) or (target_combat=='melee' and melee) or (target_combat=='missile' and missile) or (target_combat=='magic' and caster)) then
    local damage=v.damage_type or 0;local resist=damage==0 and 1 or 0
    for bit=0,7 do local e=1<<bit;if band(damage,e)~=0 then resist=math.max(resist,resistance(target,e)) end end
    local value=caster and best_score or ((v.damage or 0)*(1-(v.variance or 0)*.5)*math.max(1,v.damage_mod or 1)*resist/(1+(v.weapon_time or 0)/100))
    if caster and attack and band(damage,element(attack))~=0 then value=value*math.max(1,v.element_mod or 1) end
    local launcher=missile and (v.ammo_type or 0)~=0
    if launcher then
     local ammo_score=ammo_scores[v.ammo_type] or -1
     unassessed_ammo=unknown_ammo[v.ammo_type] or unassessed_ammo
     value=ammo_score<0 and -1 or ammo_score*math.max(1,v.damage_mod or 1)/(1+(v.weapon_time or 0)/100)
    end
    if (v.slayer or 0)~=0 and v.slayer==target.creature_type then value=value*math.max(1,v.slayer_mod or 1) end
    if v.equipped then value=value*1.01 end
    -- Bows/launchers inherit the ammunition's element. Their own piercing
    -- profile must not reject a requested fire/acid/etc. arrow loadout.
    if not explicit and damage_override and damage_override~=0 and not caster and not launcher and damage~=0 and band(damage,damage_override)==0 then value=-1 end
    -- Compare usable skill first, then damage within that skill. A wand's
    -- spell damage is not comparable to a sword's per-swing damage.
    local skill_id=caster and attack and combat_school_skill[attack.school] or v.weapon_skill
    local rank=trained_skill(s,skill_id)
    if target_combat~='auto' then rank=0 end
    if value>=0 and (rank>best_skill or (rank==best_skill and value>score)) then weapon=v;score=value;best_skill=rank end
   end
  end
  local mode=weapon and (band(weapon.type,0x8000)~=0 and 'magic' or band(weapon.type,0x100)~=0 and 'missile' or 'melee') or target_combat
  local debuff_element=damage_override
  if not debuff_element or debuff_element==0 then
   if mode=='magic' and attack then debuff_element=element(attack)
   elseif mode=='missile' and weapon and (weapon.ammo_type or 0)~=0 then
    debuff_element=ammo_elements[weapon.ammo_type]
   elseif weapon then local best=-1;for bit=0,7 do local e=1<<bit;if band(weapon.damage_type,e)~=0 and resistance(target,e)>best then debuff_element=e;best=resistance(target,e) end end end
  end
  local categories=debuff_categories(target_rule,debuff_element)
  local confirmed=effect_index[target.id] or {}
  local needs=missing_debuff(target,categories)
  return {attack=attack,weapon=weapon,unassessed=unassessed,unassessed_ammo=unassessed_ammo,mode=mode,categories=categories,confirmed=confirmed,needs=needs,damage=damage_override}
 end
 -- Keep an in-flight debuff on its recipient until the server answers. A new
 -- higher-priority spawn must not make UseDone look like successful debuffing.
 if debuff_pending then local q=debuff_pending
  local recipient_present=s.targets==nil
  for _,candidate in ipairs(s.targets or {}) do if candidate.id==q.target then recipient_present=true;break end end
  local reported=(s.debuff_revision or 0)~=q.revision and s.debuff_target==q.target and s.debuff_spell==q.spell
  local failed=(s.action_serial or 0)~=q.serial and (s.action_error or 0)~=0
  local confirmed=false;for _,e in ipairs(s.debuffs or {}) do if e.target==q.target and e.category==q.category and e.power>=q.power and e.expires>s.time then confirmed=true;break end end
  if not recipient_present or reported or failed or confirmed or s.time>=q.deadline then debuff_pending=nil
  else return result(nil,'Waiting for debuff confirmation') end
 end
 local debuff_mode=option('debuff_each_first',1)
 if target and combat~='off' and debuff_mode>1 then
  -- Large crowds are evaluated in bounded slices. Spells are compacted by
  -- family above, and every slice uses current positions and server effects.
  local candidates={}
  for _,candidate in ipairs(target_candidates) do if debuff_mode==3 or candidate.rank==priority then
   candidates[#candidates+1]=candidate
  end end
  table.sort(candidates,function(a,b)
   if a.target.id==b.target.id then return false end
   if a.rank~=b.rank then return a.rank>b.rank end
   if nearer(a.target,b.target) then return true elseif nearer(b.target,a.target) then return false end
   return a.target.id<b.target.id
  end)
  local signature=tostring(debuff_mode)..':'..priority
  for _,candidate in ipairs(candidates) do signature=signature..':'..candidate.target.id end
  if not debuff_scan or debuff_scan.signature~=signature then debuff_scan={signature=signature,index=1} end
  local work=0
  while debuff_scan.index<=#candidates do
   local candidate=candidates[debuff_scan.index]
   local element=candidate.rule and candidate.rule.damage_type or p.damage_type
   local dynamic=wanted(candidate.rule,'debuff_vuln') and (not element or element==0)
   -- Fixed debuff families do not depend on choosing a weapon. Index confirmed
   -- effects once, and only calculate a loadout for an automatic vulnerability.
   local needs=dynamic and combat_plan(candidate.target,candidate.rule).needs or missing_debuff(candidate.target,debuff_categories(candidate.rule,element))
   if needs then target=candidate.target;target_rule=candidate.rule;break end
   debuff_scan.index=debuff_scan.index+1;work=work+(dynamic and math.max(16,#attack_spells+#combat_inventory*16) or 16)
   if work>=2048 and debuff_scan.index<=#candidates then return result(nil,'Evaluating group debuffs') end
  end
  debuff_scan=nil
 else debuff_scan=nil end
 if target and combat~='off' then
  locked_target=target.id
  if not target.identified and s.targets and s.time>=(until_time['id'..target.id] or 0) then
   until_time['id'..target.id]=s.time+30;return {action='identify',item=target.id,status='Assessing '..target.name} end
  local plan=combat_plan(target,target_rule)
  local attack,weapon,mode,damage_override=plan.attack,plan.weapon,plan.mode,plan.damage
  if not weapon and plan.unassessed then return assess(plan.unassessed) end
  if not weapon and plan.unassessed_ammo then return assess(plan.unassessed_ammo) end
  if not weapon and s.inventory then return failed('No eligible combat loadout; check weapon requirements, damage type and ammunition') end
  local categories,confirmed=plan.categories,plan.confirmed
  for _,category in ipairs(categories) do
   local spell=best_debuffs[category]
   if not spell then if not option('debuff_fallback',false) then return failed('Required debuff unavailable; check learned spells, skill margin and scarabs') end
   elseif (confirmed[category] or 0)<spell.power then
    local key='debuff'..target.id..':'..category
    if (item_attempts[key] or 0)>=3 then if not option('debuff_fallback',false) then return failed('Debuff resisted or unconfirmed after three attempts') end
    elseif target.distance<=option('radius',30) then
     if option('switch_debuff_wand',true) and mode=='magic' and weapon and not weapon.equipped then return equip(weapon) end
     local equipment=caster();if equipment then return equipment end
     item_attempts[key]=(item_attempts[key] or 0)+1
     debuff_pending={target=target.id,spell=spell.id,category=category,power=spell.power,deadline=s.time+10,serial=s.action_serial or 0,revision=s.debuff_revision or 0}
     return cast(s,spell,target.id,'Debuffing: '..(spell.name or 'spell'))
    end
   else item_attempts['debuff'..target.id..':'..category]=0 end
  end
  if weapon and (not weapon.equipped or band(weapon.equipped_slot,0x200000)~=0) then return equip(weapon) end
  local secondary_request=target_rule and target_rule.secondary_equip
  if secondary_request==nil then secondary_request=option('melee_secondary_equip',nil) end
  -- With no explicit offhand rule, Left-hand Tether is the user's assignment.
  -- Do not rank this weapon as a main hand or repeatedly swap the two hands.
  local tether_only=secondary_request==nil
  if tether_only then for _,v in ipairs(inventory) do if v.auto_wield_left and weapon_allowed(v) then secondary_request=2;break end end end
  if weapon and mode=='melee' and secondary_request~=nil and band(weapon.slots,0x2000000)==0 then
   local requested=secondary_request
   local current,shield,secondary,unknown
   local rank=-1
   for _,v in ipairs(inventory) do
    if v.equipped and band(v.equipped_slot,0x200000)~=0 then current=v end
    local melee=band(v.type,1)~=0 and band(v.slots,0x2000000)==0
    local armor=band(v.type,2)~=0 and band(v.slots,0x200000)~=0
    if v.id~=weapon.id and (melee or armor) and (not tether_only or v.auto_wield_left)
     and (requested>3 and requested==v.id or requested<=2 and weapon_allowed(v)) then
     if not v.identified then unknown=v
     elseif v.can_wield then
      if requested>3 then secondary=v
      elseif armor and not shield then shield=v
      elseif melee then
       local resistance_score=0;for bit=0,7 do local e=1<<bit;if band(v.damage_type,e)~=0 then resistance_score=math.max(resistance_score,resistance(target,e)) end end
       local value=(v.damage or 0)*(1-(v.variance or 0)*.5)*resistance_score/(1+(v.weapon_time or 0)/100)
       if not secondary or (v.auto_wield_left and not secondary.auto_wield_left) or ((v.auto_wield_left==true)==(secondary.auto_wield_left==true) and value>rank) then secondary=v;rank=value end
      end
     end
    end
   end
   if requested==1 or requested==0 and not (secondary and secondary.auto_wield_left) and (trained[48] or not trained[49]) and shield then secondary=shield
   elseif requested==3 then secondary=nil end
   if not secondary and unknown and requested~=3 then return assess(unknown) end
   if secondary then
    if not current or current.id~=secondary.id then return equip(secondary,true) end
    item_attempts['equip'..secondary.id..':offhand']=0
   elseif current then return equip(current,false,true) end
  end
  if mode=='missile' and weapon and (weapon.ammo_type or 0)~=0 then
   local ammo,rank,unknown_ammo=nil,-1,nil
   for _,v in ipairs(inventory) do if band(v.slots,0x800000)~=0 and v.ammo_type==weapon.ammo_type and not v.identified then unknown_ammo=v end end
   for _,v in ipairs(inventory) do if band(v.slots,0x800000)~=0 and v.ammo_type==weapon.ammo_type and v.can_wield then
    local value=(v.damage or 1)*resistance(target,v.damage_type or 2)
    if damage_override and damage_override~=0 and band(v.damage_type,damage_override)==0 then value=-1 end
    if v.equipped then value=value*1.01 end
    if value>rank then ammo=v;rank=value end
   end end
   if not ammo and unknown_ammo then return assess(unknown_ammo) end
   if not ammo then return failed('No usable ammunition for this weapon') end
   if not ammo.equipped then return equip(ammo) end
  end
  local range=mode=='melee' and option('melee_range',2) or option('radius',30)
  if target_rule and (target_rule.range or 0)>0 then range=target_rule.range end
  if target.distance>range then
   if target.distance<=option('approach_range',60) and option('approach',true) and target.cell then
    return {action='move',cell=target.cell,x=target.x,y=target.y,z=target.z,status='Approaching '..(target.name or 'target')}
   end
  elseif mode=='magic' and attack then
   local equipment=caster();if equipment then return equipment end
   ghost_attempts[target.id]=(ghost_attempts[target.id] or 0)+1
   return cast(s,attack,target.id,'Casting '..(attack.name or 'attack'))
  elseif mode=='melee' or mode=='missile' then
   local power=option('power_percent',nil)
   power=power and power/100 or option('power',.5)
   if option('auto_attack_power',false) and weapon then
    power=1
    if mode=='melee' then
     local damage=damage_override
     if not damage or damage==0 then local best=-1;for bit=0,7 do local e=1<<bit;if band(weapon.damage_type,e)~=0 and resistance(target,e)>best then damage=e;best=resistance(target,e) end end end
     local secondary='none';for _,v in ipairs(inventory) do if v.equipped and band(v.equipped_slot,0x200000)~=0 then secondary=band(v.type,1)~=0 and 'melee' or 'shield';break end end
     local dual=weapon.damage_type==3;local multi=band(weapon.attack_type,0x40)~=0
     if weapon.weapon_type==1 and secondary~='melee' then power=damage==1 and dual and .5 or 0
     elseif damage==2 and dual then power=not multi and .2 or secondary=='melee' and .49 or secondary=='shield' and 1 or .2 end
    end
    if option('use_recklessness',false) and trained[50] then power=math.max(.11,math.min(.9,power)) end
   end
   return {action='attack',target=target.id,mode=mode=='melee' and 2 or 4,power=power,height=option('manual_attack_height',false) and option('height',2) or (target.attack_height or 2),status='Combat: '..mode}
  elseif s.inventory then return failed('No eligible combat equipment or attack spell; review Combat setup') end
 end
 local loot_action=loot();if loot_action then return loot_action end
 if not target then local components=split_peas(option('component_idle',20));if components then return components end end
 if not target and option('idle_buff_topoff',false) then local action=buffs(option('idle_buff_seconds',1200));if action then return action end end
 if activity_ready('inventory') and option('autocram',false) and (s.main_pack_slots or 0)<2 then
  for _,bag in ipairs(s.packs or {}) do if bag.free>0 then for _,item in ipairs(inventory) do
   if item.container==s.player and item.object_class~=10 and item.object_class~=38 and not item.equipped then
    return {action='store_item',item=item.id,target=bag.id,status='Making room in the main pack'}
   end
  end end end
 end
 if activity_ready('combine') and option('salvage_combine',false) and (not s.container or s.container==0) then
  if combine_pending then
   local remains=false;for _,item in ipairs(inventory) do if combine_pending.ids[item.id] then remains=true end end
   if not remains then combine_pending=nil
   elseif ((s.action_serial or 0)~=combine_pending.serial and (s.action_error or 0)~=0) or s.time>combine_pending.deadline then return failed('Salvage combine did not complete; review bags and Ust')
   else return result(nil,'Waiting for salvage combination') end
  end
  local policy=p.salvage_policy or {default={{min=1,max=6},{min=7,max=8},{min=9,max=9},{min=10,max=10}}}
  local groups,tool={},nil
  for _,item in ipairs(inventory) do
   if item.object_class==40 and item.resource_available~=false then tool=item.id end
   if item.object_class==39 and not item.equipped and not item.retained and item.resource_available~=false and (item.material or 0)>0 and (item.structure or 0)>0 and item.structure<100 and item.workmanship then
    local ranges=(policy.materials or {})[vt_string(item.material)] or policy.default or {};local index=#ranges+1
    for n,range in ipairs(ranges) do if range.min>item.workmanship then index=n-1;break elseif item.workmanship<=range.max then index=n;break end end
    local key=tostring(item.material)..':'..index;groups[key]=groups[key] or {};groups[key][#groups[key]+1]=item
   end
  end
  local keys={};for key in pairs(groups) do keys[#keys+1]=key end;table.sort(keys)
  for _,key in ipairs(keys) do local bags=groups[key];if #bags>=2 then
   -- VT sorts by material/workmanship before selecting bags. Stable GUID ties
   -- keep server enumeration order from changing the selected combination.
   table.sort(bags,function(a,b)return a.workmanship==b.workmanship and a.id<b.id or a.workmanship<b.workmanship end)
   local selected,units,value={},0,0;local threshold=(policy.values or {})[vt_string(bags[1].material)]
   if threshold and threshold<=0 then threshold=nil end
   for _,bag in ipairs(bags) do selected[#selected+1]=bag;units=units+bag.structure;value=value+(bag.value or 0);if #selected==64 or (not threshold and units>=100) then break end end
   if threshold and value<threshold then
    table.sort(bags,function(a,b) return a.structure<b.structure end)
    selected=bags[1].structure+bags[2].structure<100 and {bags[1],bags[2]} or {}
   end
   if #selected>=2 then
    if not tool then return failed('Salvage combining requires an Ust') end
    local ids,text={},{};for _,bag in ipairs(selected) do ids[bag.id]=true;text[#text+1]=string.format('%d',bag.id) end
    combine_pending={ids=ids,deadline=s.time+15,serial=s.action_serial or 0}
    return {action='combine_salvage',tool=tool,items=table.concat(text,','),status='Combining salvage workmanship group'}
   end
  end end
 end
 if activity_ready('inventory') and option('autostack',false) then
  local stacks={};for _,v in ipairs(inventory) do if v.max_stack>1 then
   local old=stacks[v.wcid];if old and old.count<old.max_stack then return {action='merge',item=v.id,target=old.id,status='Stacking '..v.name} end
   stacks[v.wcid]=v
  end end
 end
 local nav=navigate();if nav then return nav end
 local idle=idle_stance();if idle then return idle end
 if #recovery_missing>0 then
  local names={};for _,v in ipairs(recovery_missing) do names[#names+1]=v==2 and 'health' or v==4 and 'stamina' or 'mana' end
  return result(nil,'Recovery unavailable for '..table.concat(names,', ')..'; check skill buffer, components, supplies and imported handlers')
 end
 return result(nil,navigation_paused or (route_pending and route_pending.kind=='pause' and 'Pausing on route') or (health_critical and 'Health critical; recovery unavailable, UCM remains active') or 'Ready - waiting for enabled activities')
end

local function pause_activity(s,name,reason)
 if name=='navigation' then return pause_navigation(reason) end
 -- Invalid meta execution requires explicit restart; do not skip ahead through
 -- a script whose later commands may depend on the rejected operation.
 local delay=name=='meta' and math.huge or (name=='combat' or name:sub(1,8)=='recovery') and 5 or 30
 activity_pauses[name]=s.time+delay
 if name=='vendors' then note_pending=nil;purchase_pending=nil
 elseif name=='loot' then
  if loot_pending then corpses[loot_pending.container]=s.time+30 end
  if corpse_pending then corpses[corpse_pending.id]=s.time+30 end
  if s.container and s.container~=0 then corpses[s.container]=s.time+30 end
  -- Never retry ambiguous destructive jobs or reclassify existing inventory.
  loot_pending=nil;corpse_pending=nil;loot_scan={};loot_jobs={};loot_job_pending=nil
 elseif name=='components' then pea_pending=nil
 elseif name=='item_mana' then mana_refill_pending=nil;mana_fill_pending=nil;item_attempts.mana_refill=0
 elseif name=='pets' then pet_pending=nil;pet_refill_pending=nil
 elseif name=='combine' then combine_pending=nil
 elseif name=='helper' then helper_pending=nil
 elseif name=='dispel' then dispel_pending=nil
 elseif name=='buffs' then buff_item_pending=nil
 elseif name=='buff_others' then other_job=nil
 elseif name=='combat' then locked_target=nil;debuff_pending=nil;debuff_scan=nil
 elseif name:sub(1,8)=='recovery' then recovery_pending=nil;item_attempts['recover'..name:sub(9)]=0
 elseif name=='meta' then
  meta.pending=nil;meta.queue={};meta.turn=nil;meta.options={}
  meta.forcebuff=false;if forced_buff and forced_buff.request==0 then forced_buff=nil end
 end
 -- Shared preparation can fail inside several activities. Release exhausted
 -- appraisal/equipment attempts, while the activity cooldown prevents a spin.
 for key in pairs(item_attempts) do
  local text=tostring(key)
  if text:sub(1,5)=='equip' or text:sub(1,2)=='id' or (name=='components' and text:sub(1,3)=='pea') then item_attempts[key]=nil end
 end
 local labels={loot='Looting',vendors='Vendor restocking',components='Component preparation',item_mana='Equipment mana',pets='Pet summoning',combine='Salvage combining',helper='Fellowship recovery',dispel='Dispelling',buffs='Buffing',buff_others='Buff requests',combat='Combat',recovery2='Health recovery',recovery4='Stamina recovery',recovery6='Mana recovery',meta='Meta',inventory='Inventory management'}
 return {action='activity_failed',activity=name,status=(labels[name] or name)..' paused: '..reason..
  (delay==math.huge and '. Restart UCM to retry; other activities remain active' or '. Retrying in '..delay..' seconds; other activities remain active')}
end

local function run(s,p)
 if p.ucm_activity_failure then
  local failure=p.ucm_activity_failure
  return pause_activity(s,failure.activity,failure.status)
 end
 -- The host switches navigation off when it receives pause_navigation. Only
 -- re-enabling that shared switch can resume; state/meta overrides cannot.
 if navigation_paused and p.navigation then
   navigation_paused=nil;route_blocks=0;door_attempts={}
   route_join_pending=true;route_join_scan=nil;meta.options.navigation=nil
 end
 if p.ucm_mana_only then
  if s.busy or s.ready==false or s.jumping then return result(nil,'Waiting for game action') end
  return equipment_mana(s,p,s.inventory or {},function(k,d) if p[k]==nil then return d end;return p[k] end) or result(nil,'Equipment mana ready')
 end
 species_names=s.species_names or {}
 meta_active_profile=p
 if not meta.entered then meta_state('Default',s) end
 -- Chat and imported routes use the same bounded command executor.
 -- Host drains this array once; it is never saved in a user profile.
 for _,command in ipairs(p.ucm_commands or {}) do
  local command_error=meta_command(command,s)
  if command_error then
   if command_error=='Stopped by UCM command' then return result('stop',command_error) end
   return failed(command_error)
  end
 end
 if p.ucm_command_only then
  local command_intent=meta_work(s,p)
  if command_intent then return command_intent end
  if s.jumping or s.busy or s.ready==false then return result(nil,'Waiting for UCM command completion') end
  return result('stop','UCM command completed')
 end
 local request=s.force_buff_request or 0
 if request~=0 and (not forced_buff or forced_buff.request~=request) then
  forced_buff={request=request,done={}};buff_plan=nil;retries={};buff_skips={}
  -- Force Buff is an explicit new attempt, including families skipped by a
  -- previous pass. Preserve component/skill eligibility and fresh failures.
  unavailable_spells={};catalog_snapshot=nil;activity_pauses.buffs=nil
  for key in pairs(item_attempts) do if tostring(key):sub(1,8)=='buffitem' then item_attempts[key]=nil end end
  for key in pairs(until_time) do
   local text=tostring(key)
   if text:sub(1,8)=='buffitem' then until_time[key]=nil end
   for i=1,#text do if text:sub(i,i)==':' then until_time[key]=nil;break end end
  end
 elseif request==0 and forced_buff and forced_buff.request~=0 then forced_buff=nil;buff_plan=nil end
 local effective=meta_profile(s,p)
 activity='meta'
 local error=activity_ready('meta') and not navigation_paused and (not forced_buff or forced_buff.request==0) and meta_tick(s,effective)
 if error then
  if error=='Stopped by UCM command' then local out=result('stop',error);out.preserve_meta=true;return out end
  return failed(error)
 end
 effective=meta_profile(s,p)
 if navigation_paused then effective.navigation=false end
 -- Retain the followed player's observed path even while combat/actions pause walking.
 if effective.follow_corners and effective.navigation then
  local target
  for _,o in ipairs(s.route_objects or {}) do if o.id==effective.follow_id or o.name==effective.follow_name then target=o;break end end
  if not target or target.id~=follow_target or follow_teleport~=s.teleport_sequence then follow_path={} end
  follow_target=target and target.id;follow_teleport=s.teleport_sequence
  if target then
   local last=follow_path[#follow_path]
   if last and distance(last,target)>50 then follow_path={} end
   if #follow_path==0 or distance(follow_path[#follow_path],target)>=.096 then
    if #follow_path>=512 then return pause_navigation('follow path exceeded 512 points') end
    follow_path[#follow_path+1]=target
   end
   local px,py,pz=world(s.position)
   for i=#follow_path,2,-1 do
    local ax,ay,az=world(follow_path[i-1]);local bx,by,bz=world(follow_path[i]);local dx,dy,dz=bx-ax,by-ay,bz-az
    local length=dx*dx+dy*dy+dz*dz;local t=length>0 and math.max(0,math.min(1,((px-ax)*dx+(py-ay)*dy+(pz-az)*dz)/length)) or 0
    if (px-ax-t*dx)^2+(py-ay-t*dy)^2+(pz-az-t*dz)^2<2.4^2 then for j=1,i-1 do table.remove(follow_path,1) end;break end
   end
  end
 else follow_path={};follow_target=nil end
 local route=effective.route or {}
 if not effective.navigation or route_teleport~=s.teleport_sequence or route_revision~=(effective.vt_revision or 0) or route_join_pending then
  route_trail={};route_returning=false;route_walking=false
 end
 if route_teleport~=s.teleport_sequence or route_revision~=(effective.vt_revision or 0) then route_blocks=0 end
 route_teleport=s.teleport_sequence;route_revision=effective.vt_revision or 0
 if effective.navigation and #route>0 and s.position and (not route_pending or route_pending.kind=='pause') then
  if route_walking then
   route_trail={{cell=s.position.cell,x=s.position.x,y=s.position.y,z=s.position.z}}
  elseif not route_returning and #route_trail>0 and distance(s.position,route_trail[#route_trail])>=.6 then
   if distance(s.position,route_trail[#route_trail])>15 then route_trail={};route_join_pending=true
   elseif #route_trail>=256 then return pause_navigation('combat detour is too long')
   else route_trail[#route_trail+1]={cell=s.position.cell,x=s.position.x,y=s.position.y,z=s.position.z} end
  end
 end
 local blocked=s.movement_blocked_serial or 0
 if blocked_serial==nil then blocked_serial=blocked end
 if blocked~=blocked_serial then
  blocked_serial=blocked
  if effective.navigation then
   route_blocks=route_blocks+1
   if route_blocks>2 then return pause_navigation('route obstructed after two recovery attempts') end
   if route_returning then return pause_navigation('return path is blocked') end
  end
  if locked_target then monster_blacklist[locked_target]=s.time+30;locked_target=nil end
  if #route_trail==0 then route_join_pending=true;route_join_scan=nil end
 end
 local intent=(not forced_buff and not navigation_paused and meta_work(s,effective)) or tick(s,effective)
 if intent then
  route_walking=intent.status and (intent.status:sub(1,9)=='Waypoint ' or intent.status=='Route complete') or false
  if intent.status~='Returning to route' then route_returning=false end
  if intent.action=='cast' then intent.fast_cast=effective.fast_cast_buffs~=false end
  intent.helper_updates=effective.recovery~=false and ((effective.helper_health_threshold or 0)>0 or (effective.helper_stamina_threshold or 0)>0 or (effective.helper_mana_threshold or 0)>0)
  intent.route_point=point
  intent.route_join_pending=route_join_pending
  if p.vt_meta then intent.meta_state=meta.name end
  if meta.route_changed then
   intent.runtime_route=meta.route.route;intent.runtime_loop_route=effective.loop_route==true;intent.runtime_reverse_route=effective.reverse_route==true
   meta.route_changed=false
  end
 end
 return intent
end

return function(s,p)
 activity_time=s.time;activity='meta'
 if p.ucm_resume then
  -- VT resets the ordinary state timer on Start, retaining the persistent timer,
  -- variables, fired rules, call stack and selected route. In-flight requests
  -- are abandoned: inventory/targets may have changed while stopped.
  if meta.entered then meta.entered=s.time end
  entered=s.time;locked_target=nil;route_blocks=0;blocked_serial=nil
  route_join_pending=true;route_join_scan=nil
  meta.pending=nil;meta.queue={};meta.forcebuff=false
  if meta.route then meta.route_changed=true end
  route_pending=nil;loot_pending=nil;recovery_pending=nil;buff_item_pending=nil
  corpse_pending=nil;purchase_pending=nil;note_pending=nil;mana_refill_pending=nil;mana_fill_pending=nil;pet_refill_pending=nil;helper_pending=nil;debuff_pending=nil;debuff_scan=nil;pea_pending=nil;pet_pending=nil;dispel_pending=nil
  last_cast=nil;unavailable_spells={};forced_buff=nil;buff_skips={};other_job=nil
  loot_scan={};loot_jobs={};loot_job_pending=nil;combine_pending=nil;inventory_salvage_scan={};inventory_salvage_blocked={}
  retries={};until_time={};item_attempts={};corpses={};monster_failures={};monster_blacklist={};ghost_attempts={};ghost_hp={}
  follow_path={};follow_target=nil;follow_teleport=nil
  activity_pauses={}
 end
 buff_plan=nil
 local intent=run(s,p)
 if intent then
  intent.activity=intent.activity or activity
  -- Host rejections arrive already formatted; ordinary policy failures are
  -- isolated here once, after the deciding function has unwound.
  if intent.action=='activity_failed' and not p.ucm_activity_failure then
   intent=pause_activity(s,intent.activity,intent.status)
  end
 end
 buff_plan=nil -- do not retain the previous full snapshot across callbacks
 catalog_snapshot=nil;catalog_by_id=nil;catalog_supplied=nil;catalog_usable=nil
 object_snapshot=nil -- references keep IDs; the full world snapshot can be collected
 -- Expression helpers only need these during a callback. Retaining the profile
 -- kept an entire LootSnob rule tree alive while the host marshalled the next
 -- copy, exhausting the VM heap on the second looting decision.
 meta_active_profile=nil;species_names=nil
 return intent
end
