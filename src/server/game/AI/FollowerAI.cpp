/* Copyright (C) 2006 - 2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

/* ScriptData
SDName: FollowerAI
SD%Complete: 50
SDComment: This AI is under development
SDCategory: Npc
EndScriptData */


#include "FollowerAI.h"
#include "Pet.h"

const float MAX_PLAYER_DISTANCE = 100.0f;

enum ePoints
{
    POINT_COMBAT_START  = 0xFFFFFF
};

FollowerAI::FollowerAI(Creature* pCreature) : ScriptedAI(pCreature),
    m_uiLeaderGUID(0),
    m_pQuestForFollow(nullptr),
    m_uiUpdateFollowTimer(2500),
    m_uiFollowState(STATE_FOLLOW_NONE)
{}

void FollowerAI::AttackStart(Unit* pWho)
{
    if (!pWho)
        return;

    if (me->Attack(pWho, true))
    {
        me->AddThreat(pWho, 0.0f);
        me->SetInCombatWith(pWho);
        pWho->SetInCombatWith(me);

        if (me->HasUnitState(UNIT_STATE_FOLLOW))
            me->ClearUnitState(UNIT_STATE_FOLLOW);

        if (IsCombatMovementAllowed())
            me->GetMotionMaster()->MoveChase(pWho);
    }
}

//This part provides assistance to a player that are attacked by pWho, even if out of normal aggro range
//It will cause me to attack pWho that are attacking _any_ player (which has been confirmed may happen also on offi)
//The flag (type_flag) is unconfirmed, but used here for further research and is a good candidate.
bool FollowerAI::AssistPlayerInCombat(Unit* pWho)
{
    if (!pWho || !pWho->GetVictim())
        return false;

    //experimental (unknown) flag not present
    if (!(me->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_CAN_ASSIST))
        return false;

    //not a player
    if (!pWho->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself())
        return false;

    //never attack friendly
    if (me->IsFriendlyTo(pWho))
        return false;

    //too far away and no free sight?
    if (me->IsWithinDistInMap(pWho, MAX_PLAYER_DISTANCE) && me->IsWithinLOSInMap(pWho))
    {
        //already fighting someone?
        if (!me->GetVictim())
        {
            AttackStart(pWho);
            return true;
        }
        else
        {
            pWho->SetInCombatWith(me);
            me->AddThreat(pWho, 0.0f);
            return true;
        }
    }

    return false;
}

void FollowerAI::MoveInLineOfSight(Unit* pWho)
{
    if (!me->HasUnitState(UNIT_STATE_STUNNED) && me->CanAttack(pWho) == CAN_ATTACK_RESULT_OK && pWho->isInAccessiblePlaceFor(me))
    {
        if (HasFollowState(STATE_FOLLOW_INPROGRESS) && AssistPlayerInCombat(pWho))
            return;

        if (!me->CanFly() && me->GetDistanceZ(pWho) > CREATURE_Z_ATTACK_RANGE)
            return;

        if (me->IsHostileTo(pWho))
        {
            float fAttackRadius = me->GetAttackDistance(pWho);
            if (me->IsWithinDistInMap(pWho, fAttackRadius) && me->IsWithinLOSInMap(pWho))
            {
                if (!me->GetVictim())
                {
                    pWho->RemoveAurasDueToSpell(SPELL_AURA_MOD_STEALTH);
                    AttackStart(pWho);
                }
                else if (me->GetMap()->IsDungeon())
                {
                    pWho->SetInCombatWith(me);
                    me->AddThreat(pWho, 0.0f);
                }
            }
        }
    }
}

void FollowerAI::JustDied(Unit* pKiller)
{
    if (!HasFollowState(STATE_FOLLOW_INPROGRESS) || !m_uiLeaderGUID || !m_pQuestForFollow)
        return;

    //TODO: need a better check for quests with time limit.
    if (Player* pPlayer = GetLeaderForFollower())
    {
        if (Group* pGroup = pPlayer->GetGroup())
        {
            for (GroupReference* pRef = pGroup->GetFirstMember(); pRef != nullptr; pRef = pRef->next())
            {
                if (Player* pMember = pRef->GetSource())
                {
                    if (pPlayer->GetQuestStatus(m_pQuestForFollow->GetQuestId()) == QUEST_STATUS_INCOMPLETE)
                        pPlayer->FailQuest(m_pQuestForFollow->GetQuestId());
                }
            }
        }
        else
        {
            if (pPlayer->GetQuestStatus(m_pQuestForFollow->GetQuestId()) == QUEST_STATUS_INCOMPLETE)
                pPlayer->FailQuest(m_pQuestForFollow->GetQuestId());
        }
    }
}

void FollowerAI::JustRespawned()
{
    m_uiFollowState = STATE_FOLLOW_NONE;

    if (!IsCombatMovementAllowed())
        SetCombatMovementAllowed(true);

    if (me->GetFaction() != me->GetCreatureTemplate()->faction)
        me->SetFaction(me->GetCreatureTemplate()->faction);

    Reset();
}

void FollowerAI::EnterEvadeMode(EvadeReason /* why */)
{
    me->RemoveAllAuras();
    me->DeleteThreatList();
    me->CombatStop(true);
    me->SetLootRecipient(nullptr);

    if (HasFollowState(STATE_FOLLOW_INPROGRESS))
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        {
            float fPosX, fPosY, fPosZ;
            me->GetPosition(fPosX, fPosY, fPosZ);
            me->GetMotionMaster()->MovePoint(POINT_COMBAT_START, fPosX, fPosY, fPosZ);
        }
    }
    else
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            me->GetMotionMaster()->MoveTargetedHome();
    }

    Reset();
}

void FollowerAI::UpdateAI(const uint32 uiDiff)
{
    if (HasFollowState(STATE_FOLLOW_INPROGRESS) && !me->GetVictim())
    {
        if (m_uiUpdateFollowTimer <= uiDiff)
        {
            if (HasFollowState(STATE_FOLLOW_COMPLETE) && !HasFollowState(STATE_FOLLOW_POSTEVENT))
            {
                me->ForcedDespawn();
                return;
            }

            bool bIsMaxRangeExceeded = true;

            if (Player* pPlayer = GetLeaderForFollower())
            {
                if (HasFollowState(STATE_FOLLOW_RETURNING))
                {
                    RemoveFollowState(STATE_FOLLOW_RETURNING);
                    me->GetMotionMaster()->MoveFollow(pPlayer, PET_FOLLOW_DIST, me->GetFollowAngle());
                    return;
                }

                if (Group* pGroup = pPlayer->GetGroup())
                {
                    for (GroupReference* pRef = pGroup->GetFirstMember(); pRef != nullptr; pRef = pRef->next())
                    {
                        Player* pMember = pRef->GetSource();

                        if (pMember && me->IsWithinDistInMap(pMember, MAX_PLAYER_DISTANCE))
                        {
                            bIsMaxRangeExceeded = false;
                            break;
                        }
                    }
                }
                else
                {
                    if (me->IsWithinDistInMap(pPlayer, MAX_PLAYER_DISTANCE))
                        bIsMaxRangeExceeded = false;
                }
            }

            if (bIsMaxRangeExceeded)
            {
                me->ForcedDespawn();
                return;
            }

            m_uiUpdateFollowTimer = 1000;
        }
        else
            m_uiUpdateFollowTimer -= uiDiff;
    }

    UpdateFollowerAI(uiDiff);
}

void FollowerAI::UpdateFollowerAI(const uint32 uiDiff)
{
    if (!UpdateVictim())
        return;

    DoMeleeAttackIfReady();
}

void FollowerAI::MovementInform(uint32 uiMotionType, uint32 uiPointId)
{
    if (uiMotionType != POINT_MOTION_TYPE || !HasFollowState(STATE_FOLLOW_INPROGRESS))
        return;

    if (uiPointId == POINT_COMBAT_START)
    {
        if (GetLeaderForFollower())
        {
            if (!HasFollowState(STATE_FOLLOW_PAUSED))
                AddFollowState(STATE_FOLLOW_RETURNING);
        }
        else
            me->ForcedDespawn();
    }
}

void FollowerAI::StartFollow(Player* pLeader, uint32 uiFactionForFollower, const Quest* pQuest)
{
    if (me->GetVictim())
        return;

    if (HasFollowState(STATE_FOLLOW_INPROGRESS))
    {
        TC_LOG_ERROR("scripts","FollowerAI attempt to StartFollow while already following for creature %u.", me->GetEntry());
        return;
    }

    //set variables
    m_uiLeaderGUID = pLeader->GetGUID();

    if (uiFactionForFollower)
        me->SetFaction(uiFactionForFollower);

    m_pQuestForFollow = pQuest;

    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
    {
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveIdle();
    }

    me->SetUInt32Value(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_NONE);

    AddFollowState(STATE_FOLLOW_INPROGRESS);

    me->GetMotionMaster()->MoveFollow(pLeader, PET_FOLLOW_DIST, me->GetFollowAngle());
}

Player* FollowerAI::GetLeaderForFollower()
{
    if (Player* pLeader = ObjectAccessor::GetPlayer(*me, m_uiLeaderGUID))
    {
        if (pLeader->IsAlive())
            return pLeader;
        else
        {
            if (Group* pGroup = pLeader->GetGroup())
            {
                for (GroupReference* pRef = pGroup->GetFirstMember(); pRef != nullptr; pRef = pRef->next())
                {
                    Player* pMember = pRef->GetSource();

                    if (pMember && pMember->IsAlive() && me->IsWithinDistInMap(pMember, MAX_PLAYER_DISTANCE))
                    {
                        m_uiLeaderGUID = pMember->GetGUID();
                        return pMember;
                        break;
                    }
                }
            }
        }
    }

    TC_LOG_ERROR("scripts","FollowerAI GetLeader can not find suitable leader for creature %u.", me->GetEntry());
    return nullptr;
}

void FollowerAI::SetFollowComplete(bool bWithEndEvent)
{
    if (me->HasUnitState(UNIT_STATE_FOLLOW))
    {
        me->ClearUnitState(UNIT_STATE_FOLLOW);

        me->StopMoving();
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveIdle();
    }

    if (bWithEndEvent)
        AddFollowState(STATE_FOLLOW_POSTEVENT);
    else
    {
        if (HasFollowState(STATE_FOLLOW_POSTEVENT))
            RemoveFollowState(STATE_FOLLOW_POSTEVENT);
    }

    AddFollowState(STATE_FOLLOW_COMPLETE);
}

void FollowerAI::SetFollowPaused(bool bPaused)
{
    if (!HasFollowState(STATE_FOLLOW_INPROGRESS) || HasFollowState(STATE_FOLLOW_COMPLETE))
        return;

    if (bPaused)
    {
        AddFollowState(STATE_FOLLOW_PAUSED);

        if (me->HasUnitState(UNIT_STATE_FOLLOW))
        {
            me->ClearUnitState(UNIT_STATE_FOLLOW);

            me->StopMoving();
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MoveIdle();
        }
    }
    else
    {
        RemoveFollowState(STATE_FOLLOW_PAUSED);

        if (Player* pLeader = GetLeaderForFollower())
            me->GetMotionMaster()->MoveFollow(pLeader, PET_FOLLOW_DIST, me->GetFollowAngle());
    }
}
