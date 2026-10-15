#pragma once

// No native callback is retained by Lua. Requests are polled on the game thread.
static const char* kNearbyLootLua = R"lua(
if not Traveler112LootFrame then
 local f=CreateFrame('Frame')
 Traveler112LootFrame=f
 Traveler112LootRequest=0
 Traveler112LootState=0
 Traveler112LootActive=0
 Traveler112LootItems=0
 function TravelerLoot(mode)
  if mode=='stop' then Traveler112LootRequest=3
  elseif mode=='all' then Traveler112LootRequest=2
  elseif mode==nil or mode=='one' then Traveler112LootRequest=1
  else error('Usage: TravelerLoot("all" / "one" / "stop")') end
 end
 f:SetScript('OnEvent',function()
  if event=='PLAYER_ENTERING_WORLD' then
   Traveler112LootRequest=0 Traveler112LootActive=0 Traveler112LootState=0
  elseif event=='LOOT_OPENED' and Traveler112LootActive==1 then
   Traveler112LootState=2
   Traveler112LootItems=GetNumLootItems() or 0
   -- One pass per window. Locked/full/confirmation slots are never spammed.
   for i=Traveler112LootItems,1,-1 do LootSlot(i) end
   if (GetNumLootItems() or 0)==0 then CloseLoot() end
  elseif event=='LOOT_SLOT_CLEARED' and Traveler112LootActive==1 then
   Traveler112LootItems=GetNumLootItems() or 0
   if Traveler112LootItems==0 then CloseLoot() end
  elseif event=='LOOT_CLOSED' and Traveler112LootActive==1 then
   Traveler112LootState=3 Traveler112LootItems=0
  end
 end)
 f:RegisterEvent('LOOT_OPENED')
 f:RegisterEvent('LOOT_SLOT_CLEARED')
 f:RegisterEvent('LOOT_CLOSED')
 f:RegisterEvent('PLAYER_ENTERING_WORLD')
 Traveler112LootReady=1
end
)lua";
