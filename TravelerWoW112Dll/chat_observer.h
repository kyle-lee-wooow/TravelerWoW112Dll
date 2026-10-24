#pragma once

// Lua 5.0 / Vanilla event globals. No native callbacks retained by the frame.
static const char* kChatObserverLua = R"lua(
if not Traveler112ChatFrame then
 local f=CreateFrame('Frame')
 Traveler112ChatFrame=f
 f.queue={} f.seq=0 f.session=''
 local events={'CHAT_MSG_SAY','CHAT_MSG_YELL','CHAT_MSG_WHISPER','CHAT_MSG_WHISPER_INFORM',
 'CHAT_MSG_PARTY','CHAT_MSG_RAID','CHAT_MSG_RAID_LEADER','CHAT_MSG_RAID_WARNING',
 'CHAT_MSG_GUILD','CHAT_MSG_OFFICER','CHAT_MSG_CHANNEL','CHAT_MSG_SYSTEM',
 'CHAT_MSG_EMOTE','CHAT_MSG_TEXT_EMOTE'}
 for _,e in ipairs(events) do f:RegisterEvent(e) end
 f:RegisterEvent('PLAYER_ENTERING_WORLD')
 local function quote(s)
  s=string.gsub(s or '', '[%z\1-\31\\"]', function(c)
   if c=='"' then return '\\"' end
   if c=='\\' then return '\\\\' end
   return string.format('\\u%04x',string.byte(c))
  end)
  return '"'..s..'"'
 end
 -- Limit bytes without splitting a UTF-8 character.
 local function bounded(s,n)
  s=tostring(s or '')
  if string.len(s)<=n then return s end
  local k=n+1
  while k>1 and string.byte(s,k)>=128 and string.byte(s,k)<192 do k=k-1 end
  return string.sub(s,1,k-1)
 end
 f:SetScript('OnEvent',function()
  if event=='PLAYER_ENTERING_WORLD' then f.queue={} return end
  if not arg1 then return end
  f.seq=f.seq+1
  local item={seq=f.seq,type=string.sub(event,10),text=bounded(arg1,2048),
   sender=bounded(arg2,256),channel=bounded(arg9 or arg4,256),channel_id=tonumber(arg8) or 0,
   time=GetTime()}
  table.insert(f.queue,item)
  if table.getn(f.queue)>200 then table.remove(f.queue,1) end
 end)
 function Traveler112ChatPoll(after)
  local reset=after>f.seq
  if reset then after=0 end
  local parts={} local bytes=0 local cursor=after
  local oldest=f.seq+1
  if table.getn(f.queue)>0 then oldest=f.queue[1].seq end
  for _,m in ipairs(f.queue) do
   if m.seq>after then
    local s='{"seq":'..m.seq..',"type":'..quote(m.type)..',"sender":'..quote(m.sender)..
     ',"text":'..quote(m.text)..',"channel":'..quote(m.channel)..',"channel_id":'..m.channel_id..
     ',"time":'..string.format('%.3f',m.time)..'}'
    if table.getn(parts)>=20 or bytes+string.len(s)>48000 then break end
    table.insert(parts,s) bytes=bytes+string.len(s)+1 cursor=m.seq
   end
  end
  local lost=math.max(0,oldest-after-1)
  Traveler112ChatResult='{"messages":['..table.concat(parts,',')..'],"next_seq":'..cursor..
   ',"latest_seq":'..f.seq..',"oldest_seq":'..oldest..',"dropped":'..lost..
   ',"reset":'..tostring(reset)..',"session":'..quote(f.session)..'}'
 end
end
Traveler112ChatReady=1
)lua";
