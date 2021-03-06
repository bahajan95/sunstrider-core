#include "Chat.h"
#include "Language.h"
#include "CharacterCache.h"
#include "LogsDatabaseAccessor.h"
#include "ChaseMovementGenerator.h"
#include "FollowMovementGenerator.h"
#include "BattleGroundMgr.h"
#include "ArenaTeamMgr.h"
#include "Weather.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "AccountMgr.h"
#include "ScriptMgr.h"
#include "SpellHistory.h"
#include "MovementDefines.h"

#ifdef PLAYERBOT
#include "playerbot.h"
#include "GuildTaskMgr.h"
#endif
#ifdef TESTS
#include "TestMgr.h"
#endif

//kick player
bool ChatHandler::HandleKickPlayerCommand(const char *args)
{
    const char* kickName = strtok((char*)args, " ");
    char* kickReason = strtok(nullptr, "\n");
    std::string reason = "No Reason";
    std::string kicker = "Console";
    if(kickReason)
        reason = kickReason;
    if(m_session)
        kicker = m_session->GetPlayer()->GetName();

    if(!kickName)
     {
        Player* player = GetSelectedPlayerOrSelf();
        if(!player)
        {
            SendSysMessage(LANG_NO_CHAR_SELECTED);
            SetSentErrorMessage(true);
            return false;
        }

        if(player == m_session->GetPlayer())
        {
            SendSysMessage(LANG_COMMAND_KICKSELF);
            SetSentErrorMessage(true);
            return false;
        }

        // check online security
        if (HasLowerSecurity(player, ObjectGuid::Empty))
            return false;

        if(sWorld->getConfig(CONFIG_SHOW_KICK_IN_WORLD) == 1)
        {

            sWorld->SendWorldText(LANG_COMMAND_KICKMESSAGE, player->GetName().c_str(), kicker.c_str(), reason.c_str());
        }
        else
        {

            PSendSysMessage(LANG_COMMAND_KICKMESSAGE, player->GetName().c_str(), kicker.c_str(), reason.c_str());
        }

        player->GetSession()->KickPlayer();
    }
    else
    {
        std::string name = kickName;
        if(!normalizePlayerName(name))
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        if(m_session && name==m_session->GetPlayer()->GetName())
        {
            SendSysMessage(LANG_COMMAND_KICKSELF);
            SetSentErrorMessage(true);
            return false;
        }

        Player* player = ObjectAccessor::FindConnectedPlayerByName(kickName);
        if(!player)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        if(m_session && player->GetSession()->GetSecurity() > m_session->GetSecurity())
        {
            SendSysMessage(LANG_YOURS_SECURITY_IS_LOW); //maybe replacement string for this later on
            SetSentErrorMessage(true);
            return false;
        }

        if(sWorld->KickPlayer(name.c_str()))
        {
            if(sWorld->getConfig(CONFIG_SHOW_KICK_IN_WORLD) == 1)
            {

                sWorld->SendWorldText(LANG_COMMAND_KICKMESSAGE, name.c_str(), kicker.c_str(), reason.c_str());
            }
            else
            {
                PSendSysMessage(LANG_COMMAND_KICKMESSAGE, name.c_str(), kicker.c_str(), reason.c_str());
            }
        }
        else
        {
            PSendSysMessage(LANG_COMMAND_KICKNOTFOUNDPLAYER, name.c_str());
            return false;
        }
    }
    return true;
}

//morph creature or player
bool ChatHandler::HandleMorphCommand(const char* args)
{
    ARGS_CHECK

    uint16 display_id = 0;

    if (strcmp("random", args) == 0)
    {
        display_id = urand(4,25958);
        PSendSysMessage("displayid: %u",display_id);
    } else
       display_id = (uint16)atoi((char*)args);

    if(!display_id)
        return false;

    Unit *target = GetSelectedUnit();
    if(!target)
        target = m_session->GetPlayer();

    target->SetDisplayId(display_id);

    return true;
}

//demorph player or unit
bool ChatHandler::HandleDeMorphCommand(const char* /*args*/)
{
    Unit *target = GetSelectedUnit();
    if(!target)
        target = m_session->GetPlayer();

    target->DeMorph();

    return true;
}

//move item to other slot
bool ChatHandler::HandleItemMoveCommand(const char* args)
{
    ARGS_CHECK

    uint8 srcslot, dstslot;

    char* pParam1 = strtok((char*)args, " ");
    if (!pParam1)
        return false;

    char* pParam2 = strtok(nullptr, " ");
    if (!pParam2)
        return false;

    srcslot = (uint8)atoi(pParam1);
    dstslot = (uint8)atoi(pParam2);

    if(srcslot==dstslot)
        return true;

    if(!m_session->GetPlayer()->IsValidPos(INVENTORY_SLOT_BAG_0,srcslot))
        return false;

    if(!m_session->GetPlayer()->IsValidPos(INVENTORY_SLOT_BAG_0,dstslot))
        return false;

    uint16 src = ((INVENTORY_SLOT_BAG_0 << 8) | srcslot);
    uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | dstslot);

    m_session->GetPlayer()->SwapItem( src, dst );

    return true;
}

bool ChatHandler::HandleGUIDCommand(const char* /*args*/)
{
    ObjectGuid guid = m_session->GetPlayer()->GetTarget();

    if (guid == 0)
    {
        SendSysMessage(LANG_NO_SELECTION);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_OBJECT_GUID, guid.GetCounter(), guid.GetHigh());
    return true;
}

//unmute player
bool ChatHandler::HandleUnmuteCommand(const char* args)
{
    ARGS_CHECK

    char *charname = strtok((char*)args, " ");
    if (!charname)
        return false;

    std::string cname = charname;

    if(!normalizePlayerName(cname))
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(cname.c_str());
    if(!guid)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = ObjectAccessor::FindPlayer(guid);

    // check security
    uint32 account_id = 0;
    uint32 security = 0;

    if (chr)
    {
        account_id = chr->GetSession()->GetAccountId();
        security = chr->GetSession()->GetSecurity();
    }
    else
    {
        account_id = sCharacterCache->GetCharacterAccountIdByGuid(guid);
        security = sAccountMgr->GetSecurity(account_id);
    }

    // must have strong lesser security level
    if (HasLowerSecurity(chr, guid, true))
        return false;


    if (chr)
    {
        if(chr->CanSpeak())
        {
            SendSysMessage(LANG_CHAT_ALREADY_ENABLED);
            SetSentErrorMessage(true);
            return false;
        }

        chr->GetSession()->m_muteTime = 0;
    }

    LoginDatabase.PExecute("UPDATE account SET mutetime = '0' WHERE id = '%u'", account_id );
    LogsDatabaseAccessor::RemoveSanction(m_session, account_id, 0, "", SANCTION_MUTE_ACCOUNT);

    if(chr)
        ChatHandler(chr).PSendSysMessage(LANG_YOUR_CHAT_ENABLED);

    PSendSysMessage(LANG_YOU_ENABLE_CHAT, cname.c_str());
    return true;
}

//mute player for some times
bool ChatHandler::HandleMuteCommand(const char* args)
{
    ARGS_CHECK

    char* charname = strtok((char*)args, " ");
    if (!charname)
        return false;

    std::string cname = charname;

    char* timetonotspeak = strtok(nullptr, " ");
    if(!timetonotspeak)
        return false;

    char* mutereason = strtok(nullptr, "");
    std::string mutereasonstr;
    if (!mutereason)
        return false;
    
    mutereasonstr = mutereason;
        
    uint32 notspeaktime = (uint32) atoi(timetonotspeak);

    if(!normalizePlayerName(cname))
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(cname.c_str());
    if(!guid)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = ObjectAccessor::FindPlayer(guid);

    // check security
    uint32 account_id = 0;
    uint32 security = 0;

    if (chr)
    {
        account_id = chr->GetSession()->GetAccountId();
        security = chr->GetSession()->GetSecurity();
    }
    else
    {
        account_id = sCharacterCache->GetCharacterAccountIdByGuid(guid);
        security = sAccountMgr->GetSecurity(account_id);
    }

    if(m_session && security >= m_session->GetSecurity())
    {
        SendSysMessage(LANG_YOURS_SECURITY_IS_LOW);
        SetSentErrorMessage(true);
        return false;
    }

    // must have strong lesser security level
    if (HasLowerSecurity(chr, guid, true))
        return false;

    uint32 duration = notspeaktime*MINUTE;
    time_t mutetime = time(nullptr) + duration;

    if (chr)
        chr->GetSession()->m_muteTime = mutetime;
        
    // Prevent SQL injection
    LogsDatabase.EscapeString(mutereasonstr);
    LoginDatabase.PExecute("UPDATE account SET mutetime = " UI64FMTD " WHERE id = '%u'", uint64(mutetime), account_id );

    LogsDatabaseAccessor::Sanction(m_session, account_id, 0, SANCTION_MUTE_ACCOUNT, duration, mutereasonstr);

    if(chr)
        ChatHandler(chr).PSendSysMessage(LANG_YOUR_CHAT_DISABLED, notspeaktime, mutereasonstr.c_str());

    PSendSysMessage(LANG_YOU_DISABLE_CHAT, cname.c_str(), notspeaktime, mutereasonstr.c_str());

    return true;
}

bool ChatHandler::HandleMaxSkillCommand(const char* /*args*/)
{
    Player* SelectedPlayer = GetSelectedPlayerOrSelf();

    // each skills that have max skill value dependent from level seted to current level max skill value
    SelectedPlayer->UpdateSkillsToMaxSkillsForLevel();
    SendSysMessage("Max skills set to target");
    return true;
}

bool ChatHandler::HandleSetSkillCommand(const char* args)
{
    // number or [name] Shift-click form |color|Hskill:skill_id|h[name]|h|r
    char* skill_p = extractKeyFromLink((char*)args,"Hskill");
    if(!skill_p)
        return false;

    char *level_p = strtok (nullptr, " ");

    if( !level_p)
        return false;

    char *max_p   = strtok (nullptr, " ");

    int32 skill = atoi(skill_p);

    if (skill <= 0)
    {
        PSendSysMessage(LANG_INVALID_SKILL_ID, skill);
        SetSentErrorMessage(true);
        return false;
    }

    int32 level = atol(level_p);
    if (level == 0)
        level = 1;

    Player * target = GetSelectedPlayerOrSelf();
    if(!target)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    SkillLineEntry const* sl = sSkillLineStore.LookupEntry(skill);
    if(!sl)
    {
        PSendSysMessage(LANG_INVALID_SKILL_ID, skill);
        SetSentErrorMessage(true);
        return false;
    }

    if(!target->GetSkillValue(skill))
    {
        PSendSysMessage(LANG_SET_SKILL_ERROR, target->GetName().c_str(), skill, sl->name[0]);
        SetSentErrorMessage(true);
        return false;
    }

    int32 max   = max_p ? atol (max_p) : target->GetPureMaxSkillValue(skill);

    if( level > max || max <= 0 )
        return false;

    uint16 step = (level - 1) / 75;
    target->SetSkill(skill, step, level, max); //remove skill if level == 0
    PSendSysMessage(LANG_SET_SKILL, skill, sl->name[0], target->GetName().c_str(), level, max);

    return true;
}

bool ChatHandler::HandleCooldownCommand(const char* args)
{
    Player* target = GetSelectedPlayerOrSelf();

    if (!*args)
    {
        target->GetSpellHistory()->ResetAllCooldowns();
        PSendSysMessage(LANG_REMOVEALL_COOLDOWN, target->GetName().c_str());
    }
    else
    {
        // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form
        uint32 spell_id = extractSpellIdFromLink((char*)args);
        if(!spell_id)
            return false;

        if(!sSpellMgr->GetSpellInfo(spell_id))
        {
            PSendSysMessage(LANG_UNKNOWN_SPELL, target==m_session->GetPlayer() ? GetTrinityString(LANG_YOU) : target->GetName().c_str());
            SetSentErrorMessage(true);
            return false;
        }

        target->GetSpellHistory()->ResetCooldown(spell_id, true);
        PSendSysMessage(LANG_REMOVE_COOLDOWN, spell_id, target==m_session->GetPlayer() ? GetTrinityString(LANG_YOU) : target->GetName().c_str());
    }
    return true;
}

bool ChatHandler::HandleAddItemCommand(const char* args)
{
    ARGS_CHECK

    uint32 itemId = 0;

    if(args[0]=='[')                                        // [name] manual form
    {
        char* citemName = citemName = strtok((char*)args, "]");

        if(citemName && citemName[0])
        {
            std::string itemName = citemName+1;
            WorldDatabase.EscapeString(itemName);
            QueryResult result = WorldDatabase.PQuery("SELECT entry FROM item_template WHERE name = '%s'", itemName.c_str());
            if (!result)
            {
                PSendSysMessage(LANG_COMMAND_COULDNOTFIND, citemName+1);
                SetSentErrorMessage(true);
                return false;
            }
            itemId = result->Fetch()->GetUInt16();
        }
        else
            return false;
    }
    else                                                    // item_id or [name] Shift-click form |color|Hitem:item_id:0:0:0|h[name]|h|r
    {
        char* cId = extractKeyFromLink((char*)args,"Hitem");
        if(!cId)
            return false;
        itemId = atol(cId);
    }

    char* ccount = strtok(nullptr, " ");

    int32 count = 1;

    if (ccount)
        count = strtol(ccount, nullptr, 10);

    if (count == 0)
        count = 1;

    Player* pl = m_session->GetPlayer();
    Player* plTarget = GetSelectedPlayerOrSelf();

    TC_LOG_DEBUG("command",GetTrinityString(LANG_ADDITEM), itemId, count);

    ItemTemplate const *pProto = sObjectMgr->GetItemTemplate(itemId);
    if(!pProto)
    {
        PSendSysMessage(LANG_COMMAND_ITEMIDINVALID, itemId);
        SetSentErrorMessage(true);
        return false;
    }

    //Subtract
    if (count < 0)
    {
        plTarget->DestroyItemCount(itemId, -count, true, false);
        PSendSysMessage(LANG_REMOVEITEM, itemId, -count, plTarget->GetName().c_str());
        return true;
    }

    //Adding items
    uint32 noSpaceForCount = 0;

    // check space and find places
    ItemPosCountVec dest;
    uint8 msg = plTarget->CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, itemId, count, &noSpaceForCount );
    if( msg != EQUIP_ERR_OK )                               // convert to possible store amount
        count -= noSpaceForCount;

    if( count == 0 || dest.empty())                         // can't add any
    {
        PSendSysMessage(LANG_ITEM_CANNOT_CREATE, itemId, noSpaceForCount );
        SetSentErrorMessage(true);
        return false;
    }

    Item* item = plTarget->StoreNewItem( dest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));

    // remove binding (let GM give it to another player later)
    if(pl==plTarget)
        for(ItemPosCountVec::const_iterator itr = dest.begin(); itr != dest.end(); ++itr)
            if(Item* item1 = pl->GetItemByPos(itr->pos))
                item1->SetBinding( false );

    if(count > 0 && item)
    {
        pl->SendNewItem(item,count,false,true);
        if(pl!=plTarget)
            plTarget->SendNewItem(item,count,true,false);
    }

    if(noSpaceForCount > 0)
        PSendSysMessage(LANG_ITEM_CANNOT_CREATE, itemId, noSpaceForCount);

    return true;
}

bool ChatHandler::HandleAddItemSetCommand(const char* args)
{
    ARGS_CHECK

    char* cId = extractKeyFromLink((char*)args,"Hitemset"); // number or [name] Shift-click form |color|Hitemset:itemset_id|h[name]|h|r
    if (!cId)
        return false;

    uint32 itemsetId = atol(cId);

    // prevent generation all items with itemset field value '0'
    if (itemsetId == 0)
    {
        PSendSysMessage(LANG_NO_ITEMS_FROM_ITEMSET_FOUND,itemsetId);
        SetSentErrorMessage(true);
        return false;
    }

    Player* pl = m_session->GetPlayer();
    Player* plTarget = GetSelectedPlayerOrSelf();

    TC_LOG_DEBUG("command",GetTrinityString(LANG_ADDITEMSET), itemsetId);

    bool found = false;

    ItemTemplateContainer const& its = sObjectMgr->GetItemTemplateStore();
    for (const auto & it : its)
    {
        ItemTemplate const *pProto = &(it.second);
        if (!pProto)
            continue;

        if (pProto->ItemSet == itemsetId)
        {
            found = true;
            ItemPosCountVec dest;
            uint8 msg = plTarget->CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, pProto->ItemId, 1 );
            if (msg == EQUIP_ERR_OK)
            {
                Item* item = plTarget->StoreNewItem( dest, pProto->ItemId, true);

                // remove binding (let GM give it to another player later)
                if (pl==plTarget)
                    item->SetBinding( false );

                pl->SendNewItem(item,1,false,true);
                if (pl!=plTarget)
                    plTarget->SendNewItem(item,1,true,false);
            }
            else
            {
                pl->SendEquipError( msg, nullptr, nullptr );
                PSendSysMessage(LANG_ITEM_CANNOT_CREATE, pProto->ItemId, 1);
            }
        }
    }

    if (!found)
    {
        PSendSysMessage(LANG_NO_ITEMS_FROM_ITEMSET_FOUND,itemsetId);

        SetSentErrorMessage(true);
        return false;
    }

    return true;
}

bool ChatHandler::HandleDieCommand(const char* /*args*/)
{
    Unit* target = GetSelectedUnit();

    if(!target || !m_session->GetPlayer()->GetTarget())
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if (Player* player = target->ToPlayer())
        if (HasLowerSecurity(player, ObjectGuid::Empty, false))
            return false;

    if(target->IsAlive())
        Unit::Kill(m_session->GetPlayer(), target);

    return true;
}

bool ChatHandler::HandleGetDistanceCommand(const char* /*args*/)
{
    Unit* pUnit = GetSelectedUnit();

    if (!pUnit)
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_DISTANCE, m_session->GetPlayer()->GetDistance(pUnit), m_session->GetPlayer()->GetDistance2d(pUnit));
    PSendSysMessage("Exact distance 2d: %f", m_session->GetPlayer()->GetExactDistance2d(pUnit->GetPositionX(), pUnit->GetPositionY()));

    return true;
}

bool ChatHandler::HandleDamageCommand(const char * args)
{
    ARGS_CHECK

    Unit* target = GetSelectedUnit();

    if(!target || !m_session->GetPlayer()->GetTarget())
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if (Player* player = target->ToPlayer())
        if (HasLowerSecurity(player, ObjectGuid::Empty, false))
            return false;

    if( !target->IsAlive() )
        return true;

    char* damageStr = strtok((char*)args, " ");
    if(!damageStr)
        return false;

    int32 damage = atoi((char*)damageStr);
    if(damage <=0)
        return true;

    char* schoolStr = strtok((char*)nullptr, " ");

    // flat melee damage without resistence/etc reduction
    if(!schoolStr)
    {
        Unit::DealDamage(m_session->GetPlayer(), target, damage, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
        m_session->GetPlayer()->SendAttackStateUpdate(HITINFO_NORMALSWING2, target, 1, SPELL_SCHOOL_MASK_NORMAL, damage, 0, 0, VICTIMSTATE_NORMAL, 0);
        return true;
    }

    uint32 school = schoolStr ? atoi((char*)schoolStr) : SPELL_SCHOOL_NORMAL;
    if(school >= MAX_SPELL_SCHOOL)
        return false;

    SpellSchoolMask schoolmask = SpellSchoolMask(1 << school);

    if (Unit::IsDamageReducedByArmor(schoolmask))
        damage = Unit::CalcArmorReducedDamage(m_session->GetPlayer(), target, damage, nullptr, BASE_ATTACK);

    char* spellStr = strtok((char*)nullptr, " ");

    // melee damage by specific school
    if(!spellStr)
    {
        DamageInfo damageInfo(m_session->GetPlayer(), target, damage, nullptr, schoolmask, SPELL_DIRECT_DAMAGE, BASE_ATTACK);
        Unit::CalcAbsorbResist(damageInfo);

        Unit::DealDamage(m_session->GetPlayer(), target, damageInfo.GetDamage(), nullptr, DIRECT_DAMAGE, schoolmask, nullptr, false);
        m_session->GetPlayer()->SendAttackStateUpdate(HITINFO_NORMALSWING2, target, 1, schoolmask, damageInfo.GetDamage(), damageInfo.GetAbsorb(), damageInfo.GetResist(), VICTIMSTATE_NORMAL, 0);
        return true;
    }

    // non-melee damage

    // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form
    uint32 spellid = extractSpellIdFromLink((char*)args);
    if(!spellid)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellid);
    if (!spellInfo)
        return false;

    Player* attacker = GetSession()->GetPlayer();
    SpellNonMeleeDamage dmgInfo(attacker, target, spellid, spellInfo->GetSchoolMask());
    Unit::DealDamageMods(dmgInfo.target, dmgInfo.damage, &dmgInfo.absorb);
    attacker->SendSpellNonMeleeDamageLog(&dmgInfo);
    attacker->DealSpellDamage(&dmgInfo, true);
    return true;
}

bool ChatHandler::HandleReviveCommand(const char* args)
{
    Player* SelectedPlayer = nullptr;

    if (*args)
    {
        std::string name = args;
        if(!normalizePlayerName(name))
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        SelectedPlayer = ObjectAccessor::FindConnectedPlayerByName(name.c_str());
    }
    else
        SelectedPlayer = GetSelectedPlayerOrSelf();

    if(!SelectedPlayer)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    SelectedPlayer->ResurrectPlayer(0.5f);
    SelectedPlayer->SpawnCorpseBones();
    SelectedPlayer->SaveToDB();
    return true;
}

bool ChatHandler::HandleAuraCommand(const char* args)
{
    char* px = strtok((char*)args, " ");
    if (!px)
        return false;

    Unit *target = GetSelectedUnit();
    if(!target)
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form
    uint32 spellID = extractSpellIdFromLink((char*)args);

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
    if (spellInfo)
    {
         AuraCreateInfo createInfo(spellInfo, MAX_EFFECT_MASK, target);
        createInfo.SetCaster(target);

        Aura::TryRefreshStackOrCreate(createInfo);
    }

    return true;
}

bool ChatHandler::HandleUnAuraCommand(const char* args)
{
    char* px = strtok((char*)args, " ");
    if (!px)
        return false;

    Unit *target = GetSelectedUnit();
    if(!target)
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    std::string argstr = args;
    if (argstr == "all")
    {
        target->RemoveAllAuras();
        return true;
    }

    uint32 spellID = (uint32)atoi(px);
    target->RemoveAurasDueToSpell(spellID);

    return true;
}

bool ChatHandler::HandleLinkGraveCommand(const char* args)
{
    ARGS_CHECK

    char* px = strtok((char*)args, " ");
    if (!px)
        return false;

    uint32 g_id = (uint32)atoi(px);

    uint32 g_team;

    char* px2 = strtok(nullptr, " ");

    if (!px2)
        g_team = 0;
    else if (strncmp(px2,"horde",6)==0)
        g_team = HORDE;
    else if (strncmp(px2,"alliance",9)==0)
        g_team = ALLIANCE;
    else
        return false;

    WorldSafeLocsEntry const* graveyard =  sWorldSafeLocsStore.LookupEntry(g_id);

    if(!graveyard )
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDNOEXIST, g_id);
        SetSentErrorMessage(true);
        return false;
    }

    Player* player = m_session->GetPlayer();

    uint32 zoneId = player->GetZoneId();

    AreaTableEntry const *areaEntry = sAreaTableStore.LookupEntry(zoneId);
    if(!areaEntry || areaEntry->zone !=0 )
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDWRONGZONE, g_id,zoneId);
        SetSentErrorMessage(true);
        return false;
    }

    if(sObjectMgr->AddGraveYardLink(g_id,player->GetZoneId(),g_team))
        PSendSysMessage(LANG_COMMAND_GRAVEYARDLINKED, g_id,zoneId);
    else
        PSendSysMessage(LANG_COMMAND_GRAVEYARDALRLINKED, g_id,zoneId);

    return true;
}

bool ChatHandler::HandleNearGraveCommand(const char* args)
{
    uint32 g_team;

    size_t argslen = strlen(args);

    if(!*args)
        g_team = 0;
    else if (strncmp((char*)args,"horde",argslen)==0)
        g_team = HORDE;
    else if (strncmp((char*)args,"alliance",argslen)==0)
        g_team = ALLIANCE;
    else
        return false;

    Player* player = m_session->GetPlayer();

    WorldSafeLocsEntry const* graveyard = sObjectMgr->GetClosestGraveYard(
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),player->GetMapId(),g_team);

    if(graveyard)
    {
        uint32 g_id = graveyard->ID;

        GraveYardData const* data = sObjectMgr->FindGraveYardData(g_id,player->GetZoneId());
        if (!data)
        {
            PSendSysMessage(LANG_COMMAND_GRAVEYARDERROR,g_id);
            SetSentErrorMessage(true);
            return false;
        }

        g_team = data->team;

        std::string team_name = GetTrinityString(LANG_COMMAND_GRAVEYARD_NOTEAM);

        if(g_team == 0)
            team_name = GetTrinityString(LANG_COMMAND_GRAVEYARD_ANY);
        else if(g_team == HORDE)
            team_name = GetTrinityString(LANG_COMMAND_GRAVEYARD_HORDE);
        else if(g_team == ALLIANCE)
            team_name = GetTrinityString(LANG_COMMAND_GRAVEYARD_ALLIANCE);

        PSendSysMessage(LANG_COMMAND_GRAVEYARDNEAREST, g_id,team_name.c_str(),player->GetZoneId());
    }
    else
    {
        std::string team_name;

        if(g_team == 0)
            team_name = GetTrinityString(LANG_COMMAND_GRAVEYARD_ANY);
        else if(g_team == HORDE)
            team_name = GetTrinityString(LANG_COMMAND_GRAVEYARD_HORDE);
        else if(g_team == ALLIANCE)
            team_name = GetTrinityString(LANG_COMMAND_GRAVEYARD_ALLIANCE);

        if(g_team == ~uint32(0))
            PSendSysMessage(LANG_COMMAND_ZONENOGRAVEYARDS, player->GetZoneId());
        else
            PSendSysMessage(LANG_COMMAND_ZONENOGRAFACTION, player->GetZoneId(),team_name.c_str());
    }

    return true;
}

bool ChatHandler::HandleHoverCommand(const char* args)
{
    char* px = strtok((char*)args, " ");
    uint32 flag;
    if (!px)
        flag = 1;
    else
        flag = atoi(px);

    m_session->GetPlayer()->SetHover(flag);

    if (flag)
        SendSysMessage(LANG_HOVER_ENABLED);
    else
        SendSysMessage(LANG_HOVER_DISABLED);

    return true;
}

bool ChatHandler::HandleLevelUpCommand(const char* args)
{
    char* px = strtok((char*)args, " ");
    char* py = strtok((char*)nullptr, " ");

    // command format parsing
    char* pname = (char*)nullptr;
    int addlevel = 1;

    if(px && py)                                            // .levelup name level
    {
        addlevel = atoi(py);
        pname = px;
    }
    else if(px && !py)                                      // .levelup name OR .levelup level
    {
        if(isalpha(px[0]))                                  // .levelup name
            pname = px;
        else                                                // .levelup level
            addlevel = atoi(px);
    }
    // else .levelup - nothing do for preparing

    // player
    Player *chr = nullptr;
    ObjectGuid chr_guid;

    std::string name;

    if(pname)                                               // player by name
    {
        name = pname;
        if(!normalizePlayerName(name))
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        chr = ObjectAccessor::FindConnectedPlayerByName(name.c_str());
        if(!chr)                                            // not in game
        {
            chr_guid = sCharacterCache->GetCharacterGuidByName(name);
            if (chr_guid == 0)
            {
                SendSysMessage(LANG_PLAYER_NOT_FOUND);
                SetSentErrorMessage(true);
                return false;
            }
        }
    }
    else                                                    // player by selection
    {
        chr = GetSelectedPlayerOrSelf();

        if (chr == nullptr)
        {
            SendSysMessage(LANG_NO_CHAR_SELECTED);
            SetSentErrorMessage(true);
            return false;
        }

        name = chr->GetName();
    }

    assert(chr || chr_guid);

    int32 oldlevel = chr ? chr->GetLevel() : Player::GetLevelFromStorage(chr_guid);
    int32 newlevel = oldlevel + addlevel;
    if(newlevel < 1)
        newlevel = 1;
    if(newlevel > STRONG_MAX_LEVEL)                         // hardcoded maximum level
        newlevel = STRONG_MAX_LEVEL;

    if(chr)
    {
        chr->GiveLevel(newlevel);
        chr->InitTalentForLevel();
        chr->SetUInt32Value(PLAYER_XP,0);

        if(oldlevel == newlevel)
            ChatHandler(chr).SendSysMessage(LANG_YOURS_LEVEL_PROGRESS_RESET);
        else
        if(oldlevel < newlevel)
            ChatHandler(chr).PSendSysMessage(LANG_YOURS_LEVEL_UP,newlevel-oldlevel);
        else
        if(oldlevel > newlevel)
            ChatHandler(chr).PSendSysMessage(LANG_YOURS_LEVEL_DOWN,newlevel-oldlevel);
    }
    else
    {
        // update level and XP at level, all other will be updated at loading
        CharacterDatabase.PExecute("UPDATE characters SET level = '%u', xp = 0 WHERE guid = '%u'", newlevel, chr_guid.GetCounter());
    }

    sCharacterCache->UpdateCharacterLevel(chr_guid.GetCounter(), newlevel);

    if(m_session && m_session->GetPlayer() != chr)                       // including chr==NULL
        PSendSysMessage(LANG_YOU_CHANGE_LVL,name.c_str(),newlevel);
    return true;
}

bool ChatHandler::HandleShowAreaCommand(const char* args)
{
    ARGS_CHECK

    int area = atoi((char*)args);

    Player *chr = GetSelectedPlayerOrSelf();
    if (chr == nullptr)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    int offset = area / 32;
    uint32 val = (uint32)(1 << (area % 32));

    if(offset >= 128)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 currFields = chr->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset);
    chr->SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset, (uint32)(currFields | val));

    SendSysMessage(LANG_EXPLORE_AREA);
    return true;
}

bool ChatHandler::HandleHideAreaCommand(const char* args)
{
    ARGS_CHECK

    int area = atoi((char*)args);

    Player *chr = GetSelectedPlayerOrSelf();
    if (chr == nullptr)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    int offset = area / 32;
    uint32 val = (uint32)(1 << (area % 32));

    if(offset >= 128)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 currFields = chr->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset);
    chr->SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset, (uint32)(currFields ^ val));

    SendSysMessage(LANG_UNEXPLORE_AREA);
    return true;
}

bool ChatHandler::HandleBankCommand(const char* /*args*/)
{
    m_session->SendShowBank( m_session->GetPlayer()->GetGUID() );

    return true;
}

bool ChatHandler::HandleChangeWeather(const char* args)
{
    ARGS_CHECK

    //Weather is OFF
    if (!sWorld->getConfig(CONFIG_WEATHER))
    {
        SendSysMessage(LANG_WEATHER_DISABLED);
        SetSentErrorMessage(true);
        return false;
    }

    //*Change the weather of a cell
    char* px = strtok((char*)args, " ");
    char* py = strtok(nullptr, " ");

    if (!px || !py)
        return false;

    uint32 type = (uint32)atoi(px);                         //0 to 3, 0: fine, 1: rain, 2: snow, 3: sand
    float grade = (float)atof(py);                          //0 to 1, sending -1 is instand good weather

    Player *player = m_session->GetPlayer();
    uint32 zoneid = player->GetZoneId();

    Weather* wth = sWorld->FindWeather(zoneid);

    if(!wth)
        wth = sWorld->AddWeather(zoneid);
    if(!wth)
    {
        SendSysMessage(LANG_NO_WEATHER);
        SetSentErrorMessage(true);
        return false;
    }

    wth->SetWeather(WeatherType(type), grade);

    return true;
}

bool ChatHandler::HandleRespawnCommand(const char* /*args*/)
{
    Player* player = m_session->GetPlayer();

    // accept only explicitly selected target (not implicitly self targeting case)
    Unit* target = GetSelectedUnit();
    if(player->GetTarget() && target)
    {
        if(target->GetTypeId()!=TYPEID_UNIT)
        {
            SendSysMessage(LANG_SELECT_CREATURE);
            SetSentErrorMessage(true);
            return false;
        }

        if(target->IsDead())
            (target->ToCreature())->Respawn(true);
        return true;
    }

    // First handle any creatures that still have a corpse around
    Trinity::RespawnDo u_do;
    Trinity::WorldObjectWorker<Trinity::RespawnDo> worker(player, u_do);
    Cell::VisitGridObjects(player, worker, player->GetGridActivationRange());

    // Now handle any that had despawned, but had respawn time logged.
    std::vector<RespawnInfo*> data;
    player->GetMap()->GetRespawnInfo(data, SPAWN_TYPEMASK_ALL, 0);
    if (!data.empty())
    {
        uint32 const gridId = Trinity::ComputeGridCoord(player->GetPositionX(), player->GetPositionY()).GetId();
        for (RespawnInfo* info : data)
            if (info->gridId == gridId)
                player->GetMap()->RemoveRespawnTime(info, true);
    }

    return true;
}

bool ChatHandler::HandleMovegensCommand(const char* /*args*/)
{
    Unit* unit = GetSelectedUnit();
    if(!unit)
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_MOVEGENS_LIST,(unit->GetTypeId()==TYPEID_PLAYER ? "Player" : "Creature" ),unit->GetGUID().GetCounter());

    if (unit->GetMotionMaster()->Empty())
    {
        SendSysMessage("Empty");
        return true;
    }

    MotionMaster* motionMaster = unit->GetMotionMaster();
    float x, y, z;
    motionMaster->GetDestination(x, y, z);

    std::vector<MovementGeneratorInformation> list = unit->GetMotionMaster()->GetMovementGeneratorsInformation();
    for (MovementGeneratorInformation info : list)
    {
        switch (info.Type)
        {
            case IDLE_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_IDLE);
                break;
            case RANDOM_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_RANDOM);
                break;
            case WAYPOINT_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_WAYPOINT);
                break;
            case ANIMAL_RANDOM_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_ANIMAL_RANDOM);
                break;
            case CONFUSED_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_CONFUSED);
                break;
            case CHASE_MOTION_TYPE:
            {
                if (info.TargetGUID.IsEmpty())
                    SendSysMessage(LANG_MOVEGENS_CHASE_NULL);
                else if (info.TargetGUID.IsPlayer())
                    PSendSysMessage(LANG_MOVEGENS_CHASE_PLAYER, info.TargetName.c_str(), info.TargetGUID.GetCounter());
                else
                    PSendSysMessage(LANG_MOVEGENS_CHASE_CREATURE, info.TargetName.c_str(), info.TargetGUID.GetCounter());
                break;
            }
            case FOLLOW_MOTION_TYPE:
            {
                if(info.TargetGUID.IsEmpty())
                    SendSysMessage(LANG_MOVEGENS_FOLLOW_NULL);
                else if (info.TargetGUID.IsPlayer())
                    PSendSysMessage(LANG_MOVEGENS_FOLLOW_PLAYER, info.TargetName.c_str(), info.TargetGUID.GetCounter());
                else
                    PSendSysMessage(LANG_MOVEGENS_FOLLOW_CREATURE, info.TargetName.c_str(), info.TargetGUID.GetCounter());
                break;
            }
            case HOME_MOTION_TYPE:
            {
                if (unit->GetTypeId() == TYPEID_UNIT)
                    PSendSysMessage(LANG_MOVEGENS_HOME_CREATURE, x, y, z);
                else
                    SendSysMessage(LANG_MOVEGENS_HOME_PLAYER);
                break;
            }
            case FLIGHT_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_FLIGHT);
                break;
            case POINT_MOTION_TYPE:
                PSendSysMessage(LANG_MOVEGENS_POINT, x, y, z);
                break;
            case FLEEING_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_FEAR);
                break;
            case DISTRACT_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_DISTRACT);
                break;
            case EFFECT_MOTION_TYPE:
                SendSysMessage(LANG_MOVEGENS_EFFECT);
                break;
            default:
                PSendSysMessage(LANG_MOVEGENS_UNKNOWN, info.Type);
                break;
        }
    }
    
    return true;
}

bool ChatHandler::HandlePLimitCommand(const char *args)
{
    if(*args)
    {
        char* param = strtok((char*)args, " ");
        if(!param)
            return false;

        int l = strlen(param);

        if(     strncmp(param,"player",l) == 0 )
            sWorld->SetPlayerLimit(-SEC_PLAYER);
        else if(strncmp(param,"moderator",l) == 0 )
            sWorld->SetPlayerLimit(-SEC_GAMEMASTER1);
        else if(strncmp(param,"gamemaster",l) == 0 )
            sWorld->SetPlayerLimit(-SEC_GAMEMASTER2);
        else if(strncmp(param,"administrator",l) == 0 )
            sWorld->SetPlayerLimit(-SEC_GAMEMASTER3);
        else if(strncmp(param,"reset",l) == 0 )
            sWorld->SetPlayerLimit( sConfigMgr->GetIntDefault("PlayerLimit", DEFAULT_PLAYER_LIMIT) );
        else
        {
            int val = atoi(param);
            if(val < -SEC_GAMEMASTER3) val = -SEC_GAMEMASTER3;

            sWorld->SetPlayerLimit(val);
        }

        // kick all low security level players
        if(sWorld->GetPlayerAmountLimit() > SEC_PLAYER)
            sWorld->KickAllLess(sWorld->GetPlayerSecurityLimit());
    }

    uint32 pLimit = sWorld->GetPlayerAmountLimit();
    AccountTypes allowedAccountType = sWorld->GetPlayerSecurityLimit();
    char const* secName = "";
    switch(allowedAccountType)
    {
        case SEC_PLAYER:        secName = "Player";        break;
        case SEC_GAMEMASTER1:     secName = "Moderator";     break;
        case SEC_GAMEMASTER2:    secName = "Gamemaster";    break;
        case SEC_GAMEMASTER3: secName = "Administrator"; break;
        default:                secName = "<unknown>";     break;
    }

    PSendSysMessage("Player limits: amount %u, min. security level %s.",pLimit,secName);

    return true;
}

bool ChatHandler::HandleComeToMeCommand(const char *args)
{
    char* newFlagStr = strtok((char*)args, " ");

    if(!newFlagStr)
        return false;

    uint32 newFlags = (uint32)strtoul(newFlagStr, nullptr, 0);

    Creature* caster = GetSelectedCreature();
    if(!caster)
    {
        m_session->GetPlayer()->SetUnitMovementFlags(newFlags);
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    caster->SetUnitMovementFlags(newFlags);

    Player* pl = m_session->GetPlayer();

    caster->GetMotionMaster()->MovePoint(0, pl->GetPositionX(), pl->GetPositionY(), pl->GetPositionZ());
    return true;
}


//Send items by mail
bool ChatHandler::HandleSendItemsCommand(const char* args)
{
    ARGS_CHECK

    // format: name "subject text" "mail text" item1[:count1] item2[:count2] ... item12[:count12]

    char* pName = strtok((char*)args, " ");
    if(!pName)
        return false;

    char* tail1 = strtok(nullptr, "");
    if(!tail1)
        return false;

    char* msgSubject;
    if(*tail1=='"')
        msgSubject = strtok(tail1+1, "\"");
    else
    {
        char* space = strtok(tail1, "\"");
        if(!space)
            return false;
        msgSubject = strtok(nullptr, "\"");
    }

    if (!msgSubject)
        return false;

    char* tail2 = strtok(nullptr, "");
    if(!tail2)
        return false;

    char* msgText;
    if(*tail2=='"')
        msgText = strtok(tail2+1, "\"");
    else
    {
        char* space = strtok(tail2, "\"");
        if(!space)
            return false;
        msgText = strtok(nullptr, "\"");
    }

    if (!msgText)
        return false;

    // pName, msgSubject, msgText isn't NUL after prev. check
    std::string name    = pName;
    std::string subject = msgSubject;
    std::string text    = msgText;

    // extract items
    typedef std::pair<uint32,uint32> ItemPair;
    typedef std::list< ItemPair > ItemPairs;
    ItemPairs items;

    // get all tail string
    char* tail = strtok(nullptr, "");

    // get from tail next item str
    while(char* itemStr = strtok(tail, " "))
    {
        // and get new tail
        tail = strtok(nullptr, "");

        // parse item str
        char* itemIdStr = strtok(itemStr, ":");
        char* itemCountStr = strtok(nullptr, " ");

        uint32 item_id = atoi(itemIdStr);
        if(!item_id)
            return false;

        ItemTemplate const* item_proto = sObjectMgr->GetItemTemplate(item_id);
        if(!item_proto)
        {
            PSendSysMessage(LANG_COMMAND_ITEMIDINVALID, item_id);
            SetSentErrorMessage(true);
            return false;
        }

        uint32 item_count = itemCountStr ? atoi(itemCountStr) : 1;
        if(item_count < 1 || (item_proto->MaxCount && item_count > item_proto->MaxCount))
        {
            PSendSysMessage(LANG_COMMAND_INVALID_ITEM_COUNT, item_count,item_id);
            SetSentErrorMessage(true);
            return false;
        }

        while(item_count > item_proto->Stackable)
        {
            items.push_back(ItemPair(item_id,item_proto->Stackable));
            item_count -= item_proto->Stackable;
        }

        items.push_back(ItemPair(item_id,item_count));

        if(items.size() > MAX_MAIL_ITEMS)
        {
            PSendSysMessage(LANG_COMMAND_MAIL_ITEMS_LIMIT, MAX_MAIL_ITEMS);
            SetSentErrorMessage(true);
            return false;
        }
    }

    if(!normalizePlayerName(name))
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    ObjectGuid receiver_guid = sCharacterCache->GetCharacterGuidByName(name);
    if(!receiver_guid)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    // from console show not existed sender
    MailSender sender(MAIL_NORMAL,GetSession() ? GetSession()->GetPlayer()->GetGUID().GetCounter() : 0, MAIL_STATIONERY_GM);

    // fill mail
    MailDraft draft(subject, text);

    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    for (ItemPairs::const_iterator itr = items.begin(); itr != items.end(); ++itr)
    {
        if (Item* item = Item::CreateItem(itr->first, itr->second, GetSession() ? GetSession()->GetPlayer() : 0))
        {
            item->SaveToDB(trans);              // Save to prevent being lost at next mail load. If send fails, the item will be deleted.
            draft.AddItem(item);
        }
    }

    draft.SendMailTo(trans, MailReceiver(nullptr, receiver_guid), sender, MAIL_CHECK_MASK_COPIED);
    CharacterDatabase.CommitTransaction(trans);
    PSendSysMessage(LANG_MAIL_SENT, name.c_str());
    return true;
}

///Send money by mail
bool ChatHandler::HandleSendMoneyCommand(const char* args)
{
    /// format: name "subject text" "mail text" money
    Player* receiver;
    ObjectGuid receiverGuid;
    std::string receiverName;
    if (!extractPlayerTarget((char*)args, &receiver, &receiverGuid, &receiverName))
        return false;

    char* pName = strtok((char*)args, " ");
    if (!pName)
        return false;

    char* tail1 = strtok(nullptr, "");
    if (!tail1)
        return false;

    char* msgSubject;
    if (*tail1=='"')
        msgSubject = strtok(tail1+1, "\"");
    else
    {
        char* space = strtok(tail1, "\"");
        if (!space)
            return false;
        msgSubject = strtok(nullptr, "\"");
    }

    if (!msgSubject)
        return false;

    char* tail2 = strtok(nullptr, "");
    if (!tail2)
        return false;

    char* msgText;
    if (*tail2=='"')
        msgText = strtok(tail2+1, "\"");
    else
    {
        char* space = strtok(tail2, "\"");
        if (!space)
            return false;
        msgText = strtok(nullptr, "\"");
    }

    if (!msgText)
        return false;

    char* money_str = strtok(nullptr, "");
    int32 money = money_str ? atoi(money_str) : 0;
    if (money <= 0)
        return false;

    // msgSubject, msgText isn't NUL after prev. check
    std::string subject = msgSubject;
    std::string text = msgText;

    // from console show nonexisting sender
    MailSender sender(MAIL_NORMAL, GetSession() ? GetSession()->GetPlayer()->GetGUID().GetCounter() : 0, MAIL_STATIONERY_GM);

    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    MailDraft(subject, text)
        .AddMoney(money)
        .SendMailTo(trans, MailReceiver(receiver, receiverGuid.GetCounter()), sender, MAIL_CHECK_MASK_COPIED);

    CharacterDatabase.CommitTransaction(trans);

    PSendSysMessage(LANG_MAIL_SENT, receiverName.c_str());
    return true;
}

/// Send a message to a player in game
bool ChatHandler::HandleSendMessageCommand(const char* args)
{
    ///- Get the command line arguments
    char* name_str = strtok((char*)args, " ");
    char* msg_str = strtok(nullptr, "");

    if(!name_str || !msg_str)
        return false;

    std::string name = name_str;

    if(!normalizePlayerName(name))
        return false;

    ///- Find the player and check that he is not logging out.
    Player *rPlayer = ObjectAccessor::FindConnectedPlayerByName(name.c_str());
    if(!rPlayer)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    if(rPlayer->GetSession()->isLogingOut())
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    ///- Send the message
    //Use SendAreaTriggerMessage for fastest delivery.
    rPlayer->GetSession()->SendAreaTriggerMessage("%s", msg_str);
    rPlayer->GetSession()->SendAreaTriggerMessage("|cffff0000[Message from administrator]:|r");

    //Confirmation message
    PSendSysMessage(LANG_SENDMESSAGE,name.c_str(),msg_str);
    return true;
}

bool ChatHandler::HandleFlushArenaPointsCommand(const char * /*args*/)
{
    sArenaTeamMgr->DistributeArenaPoints();
    return true;
}

bool ChatHandler::HandlePlayAllCommand(const char* args)
{
    ARGS_CHECK

    uint32 soundId = atoi((char*)args);

    if(!sSoundEntriesStore.LookupEntry(soundId))
    {
        PSendSysMessage(LANG_SOUND_NOT_EXIST, soundId);
        SetSentErrorMessage(true);
        return false;
    }

    WorldPacket data(SMSG_PLAY_SOUND, 12);
    data << uint32(soundId) << m_session->GetPlayer()->GetGUID();
    sWorld->SendGlobalMessage(&data);

    PSendSysMessage(LANG_COMMAND_PLAYED_TO_ALL, soundId);
    return true;
}

bool ChatHandler::HandleFreezeCommand(const char *args)
{
    std::string name;
    Player* player;
    char* TargetName = strtok((char*)args, " "); //get entered name
    if (!TargetName) //if no name entered use target
    {
        player = GetSelectedPlayer();
        if (player) //prevent crash with creature as target
        {
            name = player->GetName();
            normalizePlayerName(name);
        }
    }
    else // if name entered
    {
        name = TargetName;
        normalizePlayerName(name);
        player = ObjectAccessor::FindConnectedPlayerByName(name.c_str()); //get player by name
    }

    if (!player)
    {
        SendSysMessage(LANG_COMMAND_FREEZE_WRONG);
        return true;
    }

    if (player==m_session->GetPlayer())
    {
        SendSysMessage(LANG_COMMAND_FREEZE_ERROR);
        return true;
    }

    //effect
    if ((player) && (!(player==m_session->GetPlayer())))
    {
        PSendSysMessage(LANG_COMMAND_FREEZE,name.c_str());

        //stop combat + make player unattackable + duel stop + stop some spells
        player->SetFaction(FACTION_FRIENDLY);
        player->CombatStop();
        if(player->IsNonMeleeSpellCast(true))
            player->InterruptNonMeleeSpells(true);
        player->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
        //player->SetUInt32Value(PLAYER_DUEL_TEAM, 1);

        //if player class = hunter || warlock remove pet if alive
        if((player->GetClass() == CLASS_HUNTER) || (player->GetClass() == CLASS_WARLOCK))
        {
            if(Pet* pet = player->GetPet())
            {
                pet->SavePetToDB(PET_SAVE_AS_CURRENT);
                // not let dismiss dead pet
                if(pet && pet->IsAlive())
                    player->RemovePet(pet, PET_SAVE_NOT_IN_SLOT);
            }
        }

        Aura* freeze = player->AddAura(9454, player);
        if (freeze)
        {
            freeze->SetDuration(-1);
            PSendSysMessage(LANG_COMMAND_FREEZE, player->GetName().c_str());
            // save player
            player->SaveToDB();
            return true;
        }
    }
    return true;
}

bool ChatHandler::HandleUnFreezeCommand(const char *args)
{
    std::string name;
    Player* player;
    char* TargetName = strtok((char*)args, " "); //get entered name
    if (!TargetName) //if no name entered use target
    {
        player = GetSelectedPlayer();
        if (player) //prevent crash with creature as target
        {
            name = player->GetName();
        }
    }

    else // if name entered
    {
        name = TargetName;
        normalizePlayerName(name);
        player = ObjectAccessor::FindConnectedPlayerByName(name.c_str()); //get player by name
    }

    //effect
    if (player)
    {
        PSendSysMessage(LANG_COMMAND_UNFREEZE,name.c_str());

        //Reset player faction + allow combat + allow duels
        player->SetFactionForRace(player->GetRace());
        player->RemoveFlag (UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);

        //allow movement and spells
        uint32 spellID = 9454;
        player->RemoveAurasDueToSpell(spellID);

        //save player
        player->SaveToDB();
    }

    if (!player)
    {
        if (TargetName)
        {
            //check for offline players
            QueryResult result = CharacterDatabase.PQuery("SELECT characters.guid FROM `characters` WHERE characters.name = '%s'",name.c_str());
            if(!result)
            {
                SendSysMessage(LANG_COMMAND_FREEZE_WRONG);
                return true;
            }
            //if player found: delete his freeze aura
            Field *fields=result->Fetch();
            ObjectGuid pguid = ObjectGuid(fields[0].GetUInt64());
            CharacterDatabase.PQuery("DELETE FROM `character_aura` WHERE character_aura.spell = 9454 AND character_aura.guid = '%u'",pguid);
            PSendSysMessage(LANG_COMMAND_UNFREEZE,name.c_str());
            return true;
        }
        else
        {
            SendSysMessage(LANG_COMMAND_FREEZE_WRONG);
            return true;
        }
    }

    return true;
}

bool ChatHandler::HandleListFreezeCommand(const char* args)
{
    //Get names from DB
    QueryResult result = CharacterDatabase.PQuery("SELECT characters.name FROM `characters` LEFT JOIN `character_aura` ON (characters.guid = character_aura.guid) WHERE character_aura.spell = 9454");
    if(!result)
    {
        SendSysMessage(LANG_COMMAND_NO_FROZEN_PLAYERS);
        return true;
    }
    //Header of the names
    PSendSysMessage(LANG_COMMAND_LIST_FREEZE);

    //Output of the results
    do
    {
        Field *fields = result->Fetch();
        std::string fplayers = fields[0].GetString();
        PSendSysMessage(LANG_COMMAND_FROZEN_PLAYERS,fplayers.c_str());
    } while (result->NextRow());

    return true;
}

bool ChatHandler::HandlePossessCommand(const char* args)
{
    Unit* pUnit = GetSelectedUnit();
    if(!pUnit)
        return false;

    m_session->GetPlayer()->CastSpell(pUnit, 530, true);
    return true;
}

bool ChatHandler::HandleUnPossessCommand(const char* args)
{
    Unit* pUnit = GetSelectedUnit();
    if(!pUnit) pUnit = m_session->GetPlayer();

    pUnit->RemoveAurasByType(SPELL_AURA_MOD_CHARM);
    pUnit->RemoveAurasByType(SPELL_AURA_MOD_POSSESS_PET);
    pUnit->RemoveAurasByType(SPELL_AURA_MOD_POSSESS);

    return true;
}

bool ChatHandler::HandleBindSightCommand(const char* args)
{
    Unit* pUnit = GetSelectedUnit();
    if (!pUnit)
        return false;

    m_session->GetPlayer()->CastSpell(pUnit, 6277, true);
    return true;
}

bool ChatHandler::HandleUnbindSightCommand(const char* args)
{
    if (m_session->GetPlayer()->IsPossessing())
        return false;

    m_session->GetPlayer()->StopCastingBindSight();
    return true;
}

bool ChatHandler::HandleGetMoveFlagsCommand(const char* args)
{
    Unit* target = GetSelectedUnit();
    if (!target)
        target = m_session->GetPlayer();

    PSendSysMessage("Target (%u) moveflags = %u",target->GetGUID().GetCounter(),target->GetUnitMovementFlags());

    return true;
}

bool ChatHandler::HandleSetMoveFlagsCommand(const char* args)
{
    ARGS_CHECK

    Unit* target = GetSelectedUnit();
    if (!target)
        target = m_session->GetPlayer();

    if(strcmp(args,"") == 0)
        return false;

    uint32 moveFlags;
    std::stringstream ss(args);
    ss >> moveFlags;

    target->SetUnitMovementFlags(moveFlags);

    PSendSysMessage("Target (%u) moveflags set to %u",target->GetGUID().GetCounter(),moveFlags);

    return true;
}

bool ChatHandler::HandleGetMaxCreaturePoolIdCommand(const char* args)
{
    QueryResult result = WorldDatabase.PQuery("SELECT MAX(pool_id) FROM creature");
    Field *fields = result->Fetch();
    
    uint32 maxId = fields[0].GetUInt32();
    
    PSendSysMessage("Current max creature pool id: %u", maxId);
    
    return true;
}

bool ChatHandler::HandleSetTitleCommand(const char* args)
{
    ARGS_CHECK

    uint32 titleId = atoi(args);

    if(CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(titleId)) {
        if (Player* plr = GetSelectedUnit()->ToPlayer())
            plr->SetTitle(titleEntry,true);
        else if (Player* _plr = m_session->GetPlayer())
            _plr->SetTitle(titleEntry,true);
    }

    return true;
}

bool ChatHandler::HandleRemoveTitleCommand(const char* args)
{
    ARGS_CHECK

    uint32 titleId = atoi(args);

    if(CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(titleId)) {
        if (Player* plr = m_session->GetPlayer())
            if (plr->HasTitle(titleEntry))
                plr->RemoveTitle(titleEntry);
    }

    return true;
}

#ifdef TESTS
bool ChatHandler::HandleTestsStartCommand(const char* args)
{
    bool ok = sTestMgr->Run(args);
    if (ok)
    {
        SendSysMessage("Tests started. Results will be dumped to all players.");
    } else {
        std::string testStatus = sTestMgr->GetStatusString();
        SendSysMessage("Tests currently running, failed to start or failed to join. Current status:");
        PSendSysMessage("%s", testStatus.c_str());
    }
    return true;
}

bool ChatHandler::HandleTestsListCommand(const char* args)
{
    std::string list_str = sTestMgr->ListAvailable(args);
    PSendSysMessage("%s", list_str.c_str());
    return true;
}

bool ChatHandler::HandleTestsRunningCommand(const char* args)
{
    std::string list_str = sTestMgr->ListRunning(args);
    PSendSysMessage("%s", list_str.c_str());
    return true;
}

bool ChatHandler::HandleTestsCancelCommand(const char* args)
{
    if (!sTestMgr->IsRunning())
    {
        SendSysMessage("Tests are not running");
        return true;
    }

    sTestMgr->Cancel();
    return true;
}

bool ChatHandler::HandleTestsGoCommand(const char* args)
{
    uint32 instanceId = atoi(args);
    if (instanceId == 0)
        return false;

    bool ok = sTestMgr->GoToTest(GetSession()->GetPlayer(), instanceId);
    if(ok)
        PSendSysMessage("Teleporting player to test");
    else
        PSendSysMessage("Failed to teleport player to test");
    return true;
}


bool ChatHandler::HandleTestsJoinCommand(const char* testName) 
{ 
    bool ok = sTestMgr->Run(testName, GetSession()->GetPlayer());
    if (ok)
    {
        SendSysMessage("Test (join) started. Results will be dumped to all players.");
    }
    else {
        std::string testStatus = sTestMgr->GetStatusString();
        SendSysMessage("Test currently running or failed to start. Current status:");
        PSendSysMessage("%s", testStatus.c_str());
    }
    return true;
}

bool ChatHandler::HandleTestsLoopCommand(const char* args) 
{ 
    //TODO
    return true; 
}

#else
bool ChatHandler::HandleTestsStartCommand(const char* args) { SendSysMessage("Core has not been compiled with tests"); return true; }
bool ChatHandler::HandleTestsListCommand(const char* args) { return HandleTestsStartCommand(args); }
bool ChatHandler::HandleTestsRunningCommand(const char* args) { return HandleTestsStartCommand(args); }
bool ChatHandler::HandleTestsGoCommand(const char* args) { return HandleTestsStartCommand(args); }
bool ChatHandler::HandleTestsCancelCommand(const char* args) { return HandleTestsStartCommand(args); }
bool ChatHandler::HandleTestsJoinCommand(const char* args) { return HandleTestsStartCommand(args); }
bool ChatHandler::HandleTestsLoopCommand(const char* args) { return HandleTestsStartCommand(args); }
#endif

bool ChatHandler::HandleYoloCommand(const char* /* args */)
{
    SendSysMessage(LANG_SWAG);

    return true;
}


bool ChatHandler::HandleSpellInfoCommand(const char* args)
{
    ARGS_CHECK
    
    uint32 spellId = uint32(atoi(args));
    if (!spellId)
        return false;
        
    const SpellInfo* spell = sSpellMgr->GetSpellInfo(spellId);
    if (!spell)
        return false;
        
    PSendSysMessage("## Spell %u (%s) ##", spell->Id, spell->SpellName[(uint32)sWorld->GetDefaultDbcLocale()]);
    PSendSysMessage("Icon: %u - Visual: %u", spell->SpellIconID, spell->SpellVisual);
    PSendSysMessage("Attributes: %x %x %x %x %x %x", spell->Attributes, spell->AttributesEx, spell->AttributesEx2, spell->AttributesEx3, spell->AttributesEx4, spell->AttributesEx5);
    PSendSysMessage("Stack amount: %u", spell->StackAmount);
    PSendSysMessage("SpellFamilyName: %u (%x)", spell->SpellFamilyName, spell->SpellFamilyName);
    PSendSysMessage("SpellFamilyFlags: " UI64FMTD " (" UI64FMTD ")", spell->SpellFamilyFlags, spell->SpellFamilyFlags);
    
    return true;
}

bool ChatHandler::HandlePlayerbotConsoleCommand(const char* args)
{
#ifdef PLAYERBOT
    return RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(this, args);
#else
    SendSysMessage("Core not build with playerbot");
    return true;
#endif
}

bool ChatHandler::HandlePlayerbotMgrCommand(const char* args)
{
#ifdef PLAYERBOT
    return PlayerbotMgr::HandlePlayerbotMgrCommand(this, args);
#else
    SendSysMessage("Core not build with playerbot");
    return true;
#endif
}
