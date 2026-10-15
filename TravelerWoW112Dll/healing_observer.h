#pragma once

// Vanilla 1.12 events are player-only. No callbacks into DLL code survive unload.
// The observer writes Lua globals; C++ reads them through the native Lua runtime.
static const char* kHealingObserverLua = R"lua(
if not Traveler112CastObserver then
 local f=CreateFrame('Frame')
 Traveler112CastObserver=f
 f.seq=0
 Traveler112CastEnd=0
 Traveler112CastName=''
 Traveler112CastResult=''
 local function compact(s)
  return string.gsub(s or '', '%((.-)%)',function(rank) return '('..string.gsub(rank,'%s+','')..')' end)
 end
 local function base(s) return string.gsub(s or '', '%(.*%)$', '') end
 local function publish()
  Traveler112CastResult=tostring(f.lastSeq)..'\031'..f.lastSpell..'\031'..(f.lastSuccess and '1' or '0')..'\031'..f.lastReason..'\031'..(f.lastVerified and '1' or '0')
 end
 local function finish(success, reason, verified)
  if not f.pending then return end
  f.lastSeq=f.seq f.lastSpell=f.pending f.lastSuccess=success f.lastReason=reason f.lastVerified=verified~=false f.lastAt=GetTime()
  publish()
  f.pending=nil f.started=nil f.stopAt=nil f.failedAt=nil f.cancelRequested=nil
 end
 local function reason(message)
  if type(message)~='string' then return 'unknown' end
  if message==SPELL_FAILED_OUT_OF_RANGE or message==ERR_OUT_OF_RANGE then return 'out_of_range' end
  if message==SPELL_FAILED_NO_POWER or message==ERR_OUT_OF_MANA then return 'not_enough_mana' end
  if message==SPELL_FAILED_INTERRUPTED or message==SPELL_FAILED_INTERRUPTED_COMBAT then return 'interrupted' end
  if message==SPELL_FAILED_BAD_TARGETS or message==SPELL_FAILED_TARGETS_DEAD or message==ERR_INVALID_ATTACK_TARGET or message==ERR_NO_TARGET then return 'invalid_target' end
  if message==SPELL_FAILED_NOT_STANDING then return 'must_stand' end
  return 'unknown'
 end
 function Traveler112BeginCast(spell)
  if f.pending then finish(false,'unknown',false) end
  Traveler112RequestedSpell=spell
  f.seq=f.seq+1 f.pending=spell f.started=false f.stopAt=nil f.failedAt=nil f.beginAt=GetTime() f.wasActive=f.active
 end
 function Traveler112RejectCast(code) finish(false,code) end
 function Traveler112CancelCast()
  if f.pending and f.started then f.cancelRequested=true end
 end
 f:SetScript('OnEvent',function()
  if event=='PLAYER_ENTERING_WORLD' then
   f.pending=nil f.started=nil f.stopAt=nil f.failedAt=nil f.active=false f.cancelRequested=nil
   Traveler112CastEnd=0 Traveler112CastName='' Traveler112CastResult=''
  elseif event=='SPELLCAST_START' then
   f.active=true
   Traveler112CastName=arg1 or ''
   Traveler112CastEnd=GetTime()+(tonumber(arg2) or 0)/1000
   if f.pending and base(f.pending)==base(compact(arg1)) then f.started=true end
   if f.started then f.stopAt=nil end
  elseif event=='SPELLCAST_DELAYED' then
   if f.active then Traveler112CastEnd=Traveler112CastEnd+(tonumber(arg1) or 0)/1000 end
  elseif event=='SPELLCAST_CHANNEL_START' then
   f.active=true
   Traveler112CastName=arg2 or ''
   Traveler112CastEnd=GetTime()+(tonumber(arg1) or 0)/1000
   if f.pending and base(f.pending)==base(compact(arg2)) then f.started=true end
   if f.started then f.stopAt=nil end
  elseif event=='SPELLCAST_CHANNEL_UPDATE' then
   Traveler112CastEnd=GetTime()+(tonumber(arg1) or 0)/1000
  elseif event=='SPELLCAST_FAILED' or event=='SPELLCAST_INTERRUPTED' then
   f.active=false Traveler112CastEnd=0 Traveler112CastName=''
   if event=='SPELLCAST_INTERRUPTED' then finish(false,'interrupted')
   elseif f.pending then f.failedAt=GetTime() f.stopAt=nil end
  elseif event=='SPELLCAST_STOP' or event=='SPELLCAST_CHANNEL_STOP' then
   local wasActive=f.active
   local earlyChannel=event=='SPELLCAST_CHANNEL_STOP' and Traveler112CastEnd-GetTime()>0.1
   f.active=false Traveler112CastEnd=0 Traveler112CastName=''
   if f.pending and (f.started or (not wasActive and not f.wasActive)) then
    if f.cancelRequested or earlyChannel then finish(false,'interrupted') else f.stopAt=GetTime() end
   end
  elseif event=='UI_ERROR_MESSAGE' then
   if f.pending and (f.started or GetTime()-f.beginAt<1) then finish(false,reason(arg1)) end
  end
 end)
 f:SetScript('OnUpdate',function()
  if f.failedAt and GetTime()-f.failedAt>=0.1 then finish(false,'unknown') end
  if f.stopAt and GetTime()-f.stopAt>=0.1 then finish(true,'') end
  -- Missing events never imply successful casting.
  if f.pending and not f.started and GetTime()-f.beginAt>2 then finish(false,'unknown',false) end
 end)
 for _,e in ipairs({'SPELLCAST_START','SPELLCAST_DELAYED','SPELLCAST_STOP','SPELLCAST_FAILED','SPELLCAST_INTERRUPTED','SPELLCAST_CHANNEL_START','SPELLCAST_CHANNEL_UPDATE','SPELLCAST_CHANNEL_STOP','UI_ERROR_MESSAGE','PLAYER_ENTERING_WORLD'}) do f:RegisterEvent(e) end
 Traveler112CastReady=1
end
)lua";
