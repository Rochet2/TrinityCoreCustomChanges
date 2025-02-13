/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "CreatureAIImpl.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedEscortAI.h"
#include "ScriptedFollowerAI.h"
#include "ScriptedGossip.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

/*######
## Quest 11865: Unfit for Death
######*/

// Gameobjects 187982,187995,187996,187997,187998,187999,188000,188001,188002,188003,188004,188005,188006,188007,188008: Caribou Trap
enum CaribouTrap
{
    EVENT_FUR_SPAWN        = 1,
    EVENT_SPAWN_TRAPPER,
    EVENT_TRAPPER_MOVE,
    EVENT_TRAPPER_TEXT,
    EVENT_TRAPPER_LOOT,
    EVENT_FUR_DESPAWN,
    EVENT_TRAPPER_DIE,
    EVENT_DESPAWN_ALL,

    GO_HIGH_QUALITY_FUR    = 187983,

    NPC_NESINGWARY_TRAPPER = 25835,

    SAY_NESINGWARY_1       = 0,

    SPELL_PLACE_FAKE_FUR   = 46085,
    SPELL_TRAPPED          = 46104,
};

struct go_caribou_trap : public GameObjectAI
{
    go_caribou_trap(GameObject* go) : GameObjectAI(go), _placedFur(false) { }

    void Reset() override
    {
        me->SetGoState(GO_STATE_READY);
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (_placedFur)
            return;

        Player* playerCaster = caster->ToPlayer();
        if (!playerCaster)
            return;

        if (spellInfo->Id == SPELL_PLACE_FAKE_FUR)
        {
            _playerGUID = caster->GetGUID();
            _placedFur = true;
            _events.ScheduleEvent(EVENT_FUR_SPAWN, 1s);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_placedFur)
            return;

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FUR_SPAWN:
                    if (GameObject* fur = me->SummonGameObject(GO_HIGH_QUALITY_FUR, me->GetPosition(), QuaternionData(0.0f, 0.0f, 0.77162457f, 0.63607824f), 20s))
                        _goFurGUID = fur->GetGUID();
                    _events.ScheduleEvent(EVENT_SPAWN_TRAPPER, 1s);
                    break;
                case EVENT_SPAWN_TRAPPER:
                    if (TempSummon* trapper = me->SummonCreature(NPC_NESINGWARY_TRAPPER, me->GetFirstCollisionPosition(21.0f, 0), TEMPSUMMON_DEAD_DESPAWN, 6s))
                    {
                        trapper->SetFacingToObject(me);
                        _trapperGUID = trapper->GetGUID();
                    }
                    _events.ScheduleEvent(EVENT_TRAPPER_MOVE, 1s);
                    break;
                case EVENT_TRAPPER_MOVE:
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                        trapper->GetMotionMaster()->MovePoint(0, trapper->GetFirstCollisionPosition(20.0f, 0));
                    _events.ScheduleEvent(EVENT_TRAPPER_TEXT, 5s);
                    break;
                case EVENT_TRAPPER_TEXT:
                {
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                    {
                        if (trapper->IsAIEnabled())
                            trapper->AI()->Talk(SAY_NESINGWARY_1);
                    }
                    _events.ScheduleEvent(EVENT_TRAPPER_LOOT, 2s);
                    break;
                }
                case EVENT_TRAPPER_LOOT:
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                        trapper->HandleEmoteCommand(EMOTE_ONESHOT_LOOT);
                    _events.ScheduleEvent(EVENT_FUR_DESPAWN, 1s);
                    break;
                case EVENT_FUR_DESPAWN:
                    if (GameObject* fur = ObjectAccessor::GetGameObject(*me, _goFurGUID))
                        fur->Delete();
                    _events.ScheduleEvent(EVENT_TRAPPER_DIE, 1s);
                    break;
                case EVENT_TRAPPER_DIE:
                    me->SetGoState(GO_STATE_ACTIVE);
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                    {
                        if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                            player->KilledMonsterCredit(trapper->GetEntry(), trapper->GetGUID());
                        trapper->CastSpell(trapper, SPELL_TRAPPED);
                    }
                    _events.ScheduleEvent(EVENT_DESPAWN_ALL, 1s);
                    break;
                case EVENT_DESPAWN_ALL:
                    if (Creature* trapper = ObjectAccessor::GetCreature(*me, _trapperGUID))
                        trapper->DespawnOrUnsummon();
                    me->DespawnOrUnsummon(0s, 50s);
                    break;
                default:
                    break;
            }
        }
    }
private:
    EventMap _events;
    bool _placedFur;
    ObjectGuid _goFurGUID;
    ObjectGuid _playerGUID;
    ObjectGuid _trapperGUID;
};

enum red_dragonblood
{
    SPELL_DRAKE_HATCHLING_SUBDUED = 46691,
    SPELL_SUBDUED = 46675
};

// 46620 - Red Dragonblood
class spell_red_dragonblood : public AuraScript
{
    PrepareAuraScript(spell_red_dragonblood);

    void HandleEffectRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE || !GetCaster())
            return;

        Creature* owner = GetOwner()->ToCreature();
        owner->RemoveAllAurasExceptType(SPELL_AURA_DUMMY);
        owner->CombatStop(true);
        owner->GetMotionMaster()->Clear();
        owner->GetMotionMaster()->MoveFollow(GetCaster(), 4.0f, 0.0f);
        owner->CastSpell(owner, SPELL_SUBDUED, true);
        GetCaster()->CastSpell(GetCaster(), SPELL_DRAKE_HATCHLING_SUBDUED, true);
        owner->SetFaction(FACTION_FRIENDLY);
        owner->SetImmuneToAll(true);
        owner->DespawnOrUnsummon(3min);
    }

    void Register()
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_red_dragonblood::HandleEffectRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/*######
## npc_thassarian
######*/

enum Thassarian
{
    QUEST_LAST_RITES        = 12019,

    SPELL_TRANSFORM_VALANAR = 46753,
    SPELL_STUN              = 46957,
    SPELL_SHADOW_BOLT       = 15537,

    NPC_IMAGE_LICH_KING     = 26203,
    NPC_COUNSELOR_TALBOT    = 25301,
    NPC_PRINCE_VALANAR      = 28189,
    NPC_GENERAL_ARLOS       = 25250,
    NPC_LERYSSA             = 25251,

    SAY_THASSARIAN_1        = 0,
    SAY_THASSARIAN_2        = 1,
    SAY_THASSARIAN_3        = 2,
    SAY_THASSARIAN_4        = 3,
    SAY_THASSARIAN_5        = 4,
    SAY_THASSARIAN_6        = 5,
    SAY_THASSARIAN_7        = 6,

    SAY_TALBOT_1            = 0,
    SAY_TALBOT_2            = 1,
    SAY_TALBOT_3            = 2,
    SAY_TALBOT_4            = 3,

    SAY_LICH_1              = 0,
    SAY_LICH_2              = 1,
    SAY_LICH_3              = 2,

    SAY_ARLOS_1             = 0,
    SAY_ARLOS_2             = 1,

    SAY_LERYSSA_1           = 0,
    SAY_LERYSSA_2           = 1,
    SAY_LERYSSA_3           = 2,
    SAY_LERYSSA_4           = 3,

    GOSSIP_THASSARIAN_MENU  = 9418, //Let's do this, Thassarian.  It's now or never.
    GOSSIP_THASSARIAN_OP    = 0
};

class npc_thassarian : public CreatureScript
{
public:
    npc_thassarian() : CreatureScript("npc_thassarian") { }

    struct npc_thassarianAI : public EscortAI
    {
        npc_thassarianAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            arthasGUID.Clear();
            talbotGUID.Clear();
            leryssaGUID.Clear();
            arlosGUID.Clear();

            arthasInPosition = false;
            arlosInPosition = false;
            leryssaInPosition = false;
            talbotInPosition = false;

            phase = 0;
            phaseTimer = 0;
        }

        ObjectGuid arthasGUID;
        ObjectGuid talbotGUID;
        ObjectGuid leryssaGUID;
        ObjectGuid arlosGUID;

        bool arthasInPosition;
        bool arlosInPosition;
        bool leryssaInPosition;
        bool talbotInPosition;

        uint32 phase;
        uint32 phaseTimer;

        void Reset() override
        {
            me->RestoreFaction();
            me->SetStandState(UNIT_STAND_STATE_STAND);

            Initialize();
        }

        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            Player* player = GetPlayerForEscort();
            if (!player)
                return;

            switch (waypointId)
            {
                case 3:
                    SetEscortPaused(true);
                    if (Creature* arthas = me->SummonCreature(NPC_IMAGE_LICH_KING, 3730.313f, 3518.689f, 473.324f, 1.562f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2min))
                    {
                        arthasGUID = arthas->GetGUID();
                        arthas->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                        arthas->SetReactState(REACT_PASSIVE);
                        arthas->SetWalk(true);
                        arthas->GetMotionMaster()->MovePoint(0, 3737.374756f, 3564.841309f, 477.433014f);
                    }
                    if (Creature* talbot = me->SummonCreature(NPC_COUNSELOR_TALBOT, 3747.23f, 3614.936f, 473.321f, 4.462012f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2min))
                    {
                        talbotGUID = talbot->GetGUID();
                        talbot->SetWalk(true);
                        talbot->GetMotionMaster()->MovePoint(0, 3738.000977f, 3568.882080f, 477.433014f);
                    }
                    me->SetWalk(false);
                    break;
                case 4:
                    SetEscortPaused(true);
                    phase = 7;
                    break;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            EscortAI::UpdateAI(diff);

            if (arthasInPosition && talbotInPosition)
            {
                phase = 1;
                arthasInPosition = false;
                talbotInPosition = false;
            }

            if (arlosInPosition && leryssaInPosition)
            {
                arlosInPosition   = false;
                leryssaInPosition = false;
                Talk(SAY_THASSARIAN_1);
                SetEscortPaused(false);
            }

            if (phaseTimer <= diff)
            {
                Creature* talbot = ObjectAccessor::GetCreature(*me, talbotGUID);
                Creature* arthas = ObjectAccessor::GetCreature(*me, arthasGUID);
                switch (phase)
                {
                    case 1:
                        if (talbot)
                            talbot->SetStandState(UNIT_STAND_STATE_KNEEL);
                        phaseTimer = 3000;
                        ++phase;
                        break;

                    case 2:
                        if (talbot)
                        {
                            talbot->UpdateEntry(NPC_PRINCE_VALANAR);
                            talbot->SetFaction(FACTION_MONSTER);
                            talbot->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                            talbot->SetReactState(REACT_PASSIVE);
                        }
                        phaseTimer = 5000;
                        ++phase;
                        break;

                    case 3:
                        if (talbot)
                            talbot->AI()->Talk(SAY_TALBOT_1);
                        phaseTimer = 5000;
                        ++phase;
                        break;

                    case 4:
                        if (arthas)
                            arthas->AI()->Talk(SAY_LICH_1);
                        phaseTimer = 5000;
                        ++phase;
                        break;

                    case 5:
                        if (talbot)
                            talbot->AI()->Talk(SAY_TALBOT_2);
                        phaseTimer = 5000;
                        ++phase;
                        break;

                    case 6:
                        if (Creature* arlos = me->SummonCreature(NPC_GENERAL_ARLOS, 3745.527100f, 3615.655029f, 473.321533f, 4.447805f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2min))
                        {
                            arlosGUID = arlos->GetGUID();
                            arlos->SetWalk(true);
                            arlos->GetMotionMaster()->MovePoint(0, 3735.570068f, 3572.419922f, 477.441010f);
                        }
                        if (Creature* leryssa = me->SummonCreature(NPC_LERYSSA, 3749.654541f, 3614.959717f, 473.323486f, 4.524959f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2min))
                        {
                            leryssaGUID = leryssa->GetGUID();
                            leryssa->SetWalk(false);
                            leryssa->SetReactState(REACT_PASSIVE);
                            leryssa->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                            leryssa->GetMotionMaster()->MovePoint(0, 3741.969971f, 3571.439941f, 477.441010f);
                        }
                        phaseTimer = 2000;
                        phase = 0;
                        break;

                    case 7:
                        Talk(SAY_THASSARIAN_2);
                        phaseTimer = 5000;
                        ++phase;
                        break;

                    case 8:
                        if (arthas && talbot)
                        {
                            arthas->SetInFront(me); //The client doesen't update with the new orientation :l
                            talbot->SetStandState(UNIT_STAND_STATE_STAND);
                            arthas->AI()->Talk(SAY_LICH_2);
                        }
                        phaseTimer = 5000;
                        phase = 9;
                        break;

                   case 9:
                        Talk(SAY_THASSARIAN_3);
                        phaseTimer = 5000;
                        phase = 10;
                        break;

                   case 10:
                        if (talbot)
                            talbot->AI()->Talk(SAY_TALBOT_3);
                        phaseTimer = 5000;
                        phase = 11;
                        break;

                   case 11:
                        if (arthas)
                            arthas->AI()->Talk(SAY_LICH_3);
                        phaseTimer = 5000;
                        phase = 12;
                        break;

                    case 12:
                        if (talbot)
                            talbot->AI()->Talk(SAY_TALBOT_4);
                        phaseTimer = 2000;
                        phase = 13;
                        break;

                    case 13:
                        if (arthas)
                            arthas->RemoveFromWorld();
                        ++phase;
                        break;

                    case 14:
                        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                        if (talbot)
                        {
                            talbot->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                            talbot->SetReactState(REACT_AGGRESSIVE);
                            talbot->CastSpell(me, SPELL_SHADOW_BOLT, false);
                        }
                        phaseTimer = 1500;
                        ++phase;
                        break;

                    case 15:
                        me->SetReactState(REACT_AGGRESSIVE);
                        AttackStart(talbot);
                        phase = 0;
                        break;

                    case 16:
                        me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                        phaseTimer = 20000;
                        ++phase;
                        break;

                   case 17:
                        if (Creature* leryssa = ObjectAccessor::GetCreature(*me, leryssaGUID))
                            leryssa->RemoveFromWorld();
                        if (Creature* arlos= ObjectAccessor::GetCreature(*me, arlosGUID))
                            arlos->RemoveFromWorld();
                        if (talbot)
                            talbot->RemoveFromWorld();
                        me->SetStandState(UNIT_STAND_STATE_STAND);
                        SetEscortPaused(false);
                        phaseTimer = 0;
                        phase = 0;
                }
            } else phaseTimer -= diff;

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

        void JustDied(Unit* /*killer*/) override
        {
            if (Creature* talbot = ObjectAccessor::GetCreature(*me, talbotGUID))
                talbot->RemoveFromWorld();

            if (Creature* leryssa = ObjectAccessor::GetCreature(*me, leryssaGUID))
                leryssa->RemoveFromWorld();

            if (Creature* arlos = ObjectAccessor::GetCreature(*me, arlosGUID))
                arlos->RemoveFromWorld();

            if (Creature* arthas = ObjectAccessor::GetCreature(*me, arthasGUID))
                arthas->RemoveFromWorld();
        }

        bool OnGossipHello(Player* player) override
        {
            if (me->IsQuestGiver())
                player->PrepareQuestMenu(me->GetGUID());

            if (player->GetQuestStatus(QUEST_LAST_RITES) == QUEST_STATUS_INCOMPLETE && me->GetAreaId() == 4128)
                AddGossipItemFor(player, GOSSIP_THASSARIAN_MENU, GOSSIP_THASSARIAN_OP, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);
            switch (action)
            {
                case GOSSIP_ACTION_INFO_DEF + 1:
                    Start(true, false, player->GetGUID());
                    SetMaxPlayerDistance(200.0f);
                    break;
            }
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_thassarianAI(creature);
    }
};

/*######
## npc_image_lich_king
######*/

class npc_image_lich_king : public CreatureScript
{
public:
    npc_image_lich_king() : CreatureScript("npc_image_lich_king") { }

    struct npc_image_lich_kingAI : public ScriptedAI
    {
        npc_image_lich_kingAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            me->RestoreFaction();
        }

        void MovementInform(uint32 uiType, uint32 /*uiId*/) override
        {
            if (uiType != POINT_MOTION_TYPE)
                return;

            if (me->IsSummon())
                if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                    ENSURE_AI(npc_thassarian::npc_thassarianAI, summoner->ToCreature()->AI())->arthasInPosition = true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_image_lich_kingAI(creature);
    }
};

/*######
## npc_general_arlos
######*/

class npc_general_arlos : public CreatureScript
{
public:
    npc_general_arlos() : CreatureScript("npc_general_arlos") { }

    struct npc_general_arlosAI : public ScriptedAI
    {
        npc_general_arlosAI(Creature* creature) : ScriptedAI(creature) { }

        void MovementInform(uint32 uiType, uint32 /*uiId*/) override
        {
            if (uiType != POINT_MOTION_TYPE)
                return;

            me->AddUnitState(UNIT_STATE_STUNNED);
            me->CastSpell(me, SPELL_STUN, true);
            if (me->IsSummon())
                if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                    ENSURE_AI(npc_thassarian::npc_thassarianAI, summoner->ToCreature()->AI())->arlosInPosition = true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_general_arlosAI(creature);
    }
};

/*######
## npc_counselor_talbot
######*/

enum CounselorTalbot
{
    SPELL_DEFLECTION    = 51009,
    SPELL_SOUL_BLAST    = 50992,
};

class npc_counselor_talbot : public CreatureScript
{
public:
    npc_counselor_talbot() : CreatureScript("npc_counselor_talbot") { }

    struct npc_counselor_talbotAI : public ScriptedAI
    {
        npc_counselor_talbotAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            leryssaGUID.Clear();
            arlosGUID.Clear();
            bCheck = false;
            shadowBoltTimer = urand(5000, 12000);
            deflectionTimer = urand(20000, 25000);
            soulBlastTimer = urand(12000, 18000);
        }

        ObjectGuid leryssaGUID;
        ObjectGuid arlosGUID;

        bool bCheck;

        uint32 shadowBoltTimer;
        uint32 deflectionTimer;
        uint32 soulBlastTimer;

        void Reset() override
        {
            Initialize();
        }
        void MovementInform(uint32 uiType, uint32 /*uiId*/) override
        {
            if (uiType != POINT_MOTION_TYPE)
                return;

            if (me->IsSummon())
                if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                    ENSURE_AI(npc_thassarian::npc_thassarianAI, summoner->ToCreature()->AI())->talbotInPosition = true;
        }

        void UpdateAI(uint32 diff) override
        {
            if (bCheck)
            {
                if (Creature* leryssa = me->FindNearestCreature(NPC_LERYSSA, 50.0f, true))
                    leryssaGUID = leryssa->GetGUID();
                if (Creature* arlos = me->FindNearestCreature(NPC_GENERAL_ARLOS, 50.0f, true))
                    arlosGUID = arlos->GetGUID();
                bCheck = false;
            }

            if (!UpdateVictim())
                return;

            if (me->GetAreaId() == 4125)
            {
                if (shadowBoltTimer <= diff)
                {
                    DoCastVictim(SPELL_SHADOW_BOLT);
                    shadowBoltTimer = urand(5000, 12000);
                }
                else
                    shadowBoltTimer -= diff;

                if (deflectionTimer <= diff)
                {
                    DoCastVictim(SPELL_DEFLECTION);
                    deflectionTimer = urand(20000, 25000);
                }
                else
                    deflectionTimer -= diff;

                if (soulBlastTimer <= diff)
                {
                    DoCastVictim(SPELL_SOUL_BLAST);
                    soulBlastTimer = urand(12000, 18000);
                }
                else
                    soulBlastTimer -= diff;
            }

            DoMeleeAttackIfReady();
        }

        void JustDied(Unit* killer) override
        {
            if (!leryssaGUID || !arlosGUID)
                return;

            Creature* leryssa = ObjectAccessor::GetCreature(*me, leryssaGUID);
            Creature* arlos = ObjectAccessor::GetCreature(*me, arlosGUID);
            if (!leryssa || !arlos)
                return;

            arlos->AI()->Talk(SAY_ARLOS_1);
            arlos->AI()->Talk(SAY_ARLOS_2);
            leryssa->AI()->Talk(SAY_LERYSSA_1);
            arlos->KillSelf(false);
            leryssa->RemoveAura(SPELL_STUN);
            leryssa->ClearUnitState(UNIT_STATE_STUNNED);
            leryssa->SetWalk(false);
            leryssa->GetMotionMaster()->MovePoint(0, 3722.114502f, 3564.201660f, 477.441437f);

            if (killer && killer->GetTypeId() == TYPEID_PLAYER)
                killer->ToPlayer()->RewardPlayerAndGroupAtEvent(NPC_PRINCE_VALANAR, 0);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_counselor_talbotAI(creature);
    }
};

/*######
## npc_leryssa
######*/

class npc_leryssa : public CreatureScript
{
public:
    npc_leryssa() : CreatureScript("npc_leryssa") { }

    struct npc_leryssaAI : public ScriptedAI
    {
        npc_leryssaAI(Creature* creature) : ScriptedAI(creature)
        {
            bDone = false;
            phase = 0;
            phaseTimer = 0;

            creature->SetStandState(UNIT_STAND_STATE_STAND);
        }

        bool bDone;

        uint32 phase;
        uint32 phaseTimer;

        void MovementInform(uint32 type, uint32 /*uiId*/) override
        {
            if (type != POINT_MOTION_TYPE)
                return;

            if (!bDone)
            {
                if (Creature* talbot = me->FindNearestCreature(NPC_PRINCE_VALANAR, 50.0f, true))
                    ENSURE_AI(npc_counselor_talbot::npc_counselor_talbotAI, talbot->GetAI())->bCheck = true;

                me->AddUnitState(UNIT_STATE_STUNNED);
                me->CastSpell(me, SPELL_STUN, true);

                if (me->IsSummon())
                    if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                        ENSURE_AI(npc_thassarian::npc_thassarianAI, summoner->GetAI())->leryssaInPosition = true;
                bDone = true;
            }
            else
            {
                me->SetStandState(UNIT_STAND_STATE_SIT);
                if (me->IsSummon())
                    if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                    summoner->SetStandState(UNIT_STAND_STATE_SIT);
                phaseTimer = 1500;
                phase = 1;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            ScriptedAI::UpdateAI(diff);

            if (phaseTimer <= diff)
            {
                switch (phase)
                {
                    case 1:
                        if (me->IsSummon())
                            if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                                if (Creature* thassarian = summoner->ToCreature())
                                    thassarian->AI()->Talk(SAY_THASSARIAN_4);
                        phaseTimer = 5000;
                        ++phase;
                        break;
                    case 2:
                        Talk(SAY_LERYSSA_2);
                        phaseTimer = 5000;
                        ++phase;
                        break;
                    case 3:
                        if (me->IsSummon())
                            if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                                if (Creature* thassarian = summoner->ToCreature())
                                    thassarian->AI()->Talk(SAY_THASSARIAN_5);
                        phaseTimer = 5000;
                        ++phase;
                        break;
                    case 4:
                        Talk(SAY_LERYSSA_3);
                        phaseTimer = 5000;
                        ++phase;
                        break;
                    case 5:
                        if (me->IsSummon())
                            if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                                if (Creature* thassarian = summoner->ToCreature())
                                    thassarian->AI()->Talk(SAY_THASSARIAN_6);
                        phaseTimer = 5000;
                        ++phase;
                        break;

                    case 6:
                        Talk(SAY_LERYSSA_4);
                        phaseTimer = 5000;
                        ++phase;
                        break;
                    case 7:
                        if (me->IsSummon())
                            if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                                if (Creature* thassarian = summoner->ToCreature())
                                {
                                    thassarian->AI()->Talk(SAY_THASSARIAN_7);
                                    ENSURE_AI(npc_thassarian::npc_thassarianAI, thassarian->GetAI())->phase = 16;
                                }
                        phaseTimer = 5000;
                        phase = 0;
                        break;
                }
            } else phaseTimer -= diff;

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_leryssaAI(creature);
    }
};

/*######
## npc_beryl_sorcerer
######*/

enum BerylSorcerer
{
    NPC_CAPTURED_BERLY_SORCERER         = 25474,
    NPC_LIBRARIAN_DONATHAN              = 25262,

    SPELL_ARCANE_CHAINS                 = 45611,
    SPELL_COSMETIC_CHAINS               = 54324,
    SPELL_COSMETIC_ENSLAVE_CHAINS_SELF  = 45631
};

struct npc_beryl_sorcerer : public FollowerAI
{
    npc_beryl_sorcerer(Creature* creature) : FollowerAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        bEnslaved = false;
    }

    bool bEnslaved;

    void Reset() override
    {
        me->SetReactState(REACT_AGGRESSIVE);
        Initialize();
    }

    void JustEngagedWith(Unit* who) override
    {
        if (me->IsValidAttackTarget(who))
            AttackStart(who);
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Player* playerCaster = caster->ToPlayer();
        if (!playerCaster)
            return;

        if (spellInfo->Id == SPELL_ARCANE_CHAINS && !HealthAbovePct(50) && !bEnslaved)
        {
            EnterEvadeMode(); //We make sure that the npc is not attacking the player!
            me->SetReactState(REACT_PASSIVE);
            StartFollow(playerCaster);
            me->UpdateEntry(NPC_CAPTURED_BERLY_SORCERER);
            DoCast(me, SPELL_COSMETIC_ENSLAVE_CHAINS_SELF, true);

            playerCaster->KilledMonsterCredit(NPC_CAPTURED_BERLY_SORCERER);

            bEnslaved = true;
        }
    }

    void MoveInLineOfSight(Unit* who) override
    {
        FollowerAI::MoveInLineOfSight(who);

        if (who->GetEntry() == NPC_LIBRARIAN_DONATHAN && me->IsWithinDistInMap(who, INTERACTION_DISTANCE))
        {
            SetFollowComplete();
            me->DisappearAndDie();
        }
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

/*######
## Help Those That Cannot Help Themselves, Quest 11876
######*/

enum HelpThemselves
{
    QUEST_CANNOT_HELP_THEMSELVES                  =  11876,
    GO_MAMMOTH_TRAP_1                             = 188022,
    GO_MAMMOTH_TRAP_2                             = 188024,
    GO_MAMMOTH_TRAP_3                             = 188025,
    GO_MAMMOTH_TRAP_4                             = 188026,
    GO_MAMMOTH_TRAP_5                             = 188027,
    GO_MAMMOTH_TRAP_6                             = 188028,
    GO_MAMMOTH_TRAP_7                             = 188029,
    GO_MAMMOTH_TRAP_8                             = 188030,
    GO_MAMMOTH_TRAP_9                             = 188031,
    GO_MAMMOTH_TRAP_10                            = 188032,
    GO_MAMMOTH_TRAP_11                            = 188033,
    GO_MAMMOTH_TRAP_12                            = 188034,
    GO_MAMMOTH_TRAP_13                            = 188035,
    GO_MAMMOTH_TRAP_14                            = 188036,
    GO_MAMMOTH_TRAP_15                            = 188037,
    GO_MAMMOTH_TRAP_16                            = 188038,
    GO_MAMMOTH_TRAP_17                            = 188039,
    GO_MAMMOTH_TRAP_18                            = 188040,
    GO_MAMMOTH_TRAP_19                            = 188041,
    GO_MAMMOTH_TRAP_20                            = 188042,
    GO_MAMMOTH_TRAP_21                            = 188043,
    GO_MAMMOTH_TRAP_22                            = 188044,
};

#define MammothTrapsNum 22
const uint32 MammothTraps[MammothTrapsNum] =
{
    GO_MAMMOTH_TRAP_1, GO_MAMMOTH_TRAP_2, GO_MAMMOTH_TRAP_3, GO_MAMMOTH_TRAP_4, GO_MAMMOTH_TRAP_5,
    GO_MAMMOTH_TRAP_6, GO_MAMMOTH_TRAP_7, GO_MAMMOTH_TRAP_8, GO_MAMMOTH_TRAP_9, GO_MAMMOTH_TRAP_10,
    GO_MAMMOTH_TRAP_11, GO_MAMMOTH_TRAP_12, GO_MAMMOTH_TRAP_13, GO_MAMMOTH_TRAP_14, GO_MAMMOTH_TRAP_15,
    GO_MAMMOTH_TRAP_16, GO_MAMMOTH_TRAP_17, GO_MAMMOTH_TRAP_18, GO_MAMMOTH_TRAP_19, GO_MAMMOTH_TRAP_20,
    GO_MAMMOTH_TRAP_21, GO_MAMMOTH_TRAP_22
};

struct npc_trapped_mammoth_calf : public ScriptedAI
{
    npc_trapped_mammoth_calf(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        uiTimer = 1500;
        bStarted = false;
    }

    uint32 uiTimer;
    bool bStarted;

    void Reset() override
    {
        Initialize();

        GameObject* pTrap = nullptr;
        for (uint8 i = 0; i < MammothTrapsNum; ++i)
        {
            pTrap = me->FindNearestGameObject(MammothTraps[i], 11.0f);
            if (pTrap)
            {
                pTrap->SetGoState(GO_STATE_ACTIVE);
                return;
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (bStarted)
        {
            if (uiTimer <= diff)
            {
                Position pos = me->GetRandomNearPosition(10.0f);
                me->GetMotionMaster()->MovePoint(0, pos);
                bStarted = false;
            }
            else uiTimer -= diff;
        }
    }

    void DoAction(int32 param) override
    {
        if (param == 1)
            bStarted = true;
    }

    void MovementInform(uint32 uiType, uint32 /*uiId*/) override
    {
        if (uiType != POINT_MOTION_TYPE)
            return;

        me->DisappearAndDie();

        GameObject* pTrap = nullptr;
        for (uint8 i = 0; i < MammothTrapsNum; ++i)
        {
            pTrap = me->FindNearestGameObject(MammothTraps[i], 11.0f);
            if (pTrap)
            {
                pTrap->SetLootState(GO_JUST_DEACTIVATED);
                return;
            }
        }
    }
};

/*######
## Valiance Keep Cannoneer script to activate cannons
######*/

enum Valiancekeepcannons
{
    GO_VALIANCE_KEEP_CANNON_1                     = 187560,
    GO_VALIANCE_KEEP_CANNON_2                     = 188692
};

struct npc_valiance_keep_cannoneer : public ScriptedAI
{
    npc_valiance_keep_cannoneer(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        uiTimer = urand(13000, 18000);
    }

    uint32 uiTimer;

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        if (uiTimer <= diff)
        {
            me->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
            GameObject* pCannon = me->FindNearestGameObject(GO_VALIANCE_KEEP_CANNON_1, 10);
            if (!pCannon)
                pCannon = me->FindNearestGameObject(GO_VALIANCE_KEEP_CANNON_2, 10);
            if (pCannon)
                pCannon->Use(me);
            uiTimer = urand(13000, 18000);
        }
        else uiTimer -= diff;

        if (!UpdateVictim())
            return;
    }
};

/*######
## npc_hidden_cultist
######*/

enum HiddenCultist
{
    SPELL_SHROUD_OF_THE_DEATH_CULTIST           = 46077, //not working
    SPELL_RIGHTEOUS_VISION                      = 46078, //player aura

    QUEST_THE_HUNT_IS_ON                        = 11794,

    GOSSIP_TEXT_SALTY_JOHN_THORPE               = 12529,
    GOSSIP_TEXT_GUARD_MITCHELSS                 = 12530,
    GOSSIP_TEXT_TOM_HEGGER                      = 12528,

    NPC_TOM_HEGGER                              = 25827,
    NPC_SALTY_JOHN_THORPE                       = 25248,
    NPC_GUARD_MITCHELLS                         = 25828,

    SAY_HIDDEN_CULTIST_1                        = 0,
    SAY_HIDDEN_CULTIST_2                        = 1,
    SAY_HIDDEN_CULTIST_3                        = 2,
    SAY_HIDDEN_CULTIST_4                        = 3,

    GOSSIP_ITEM_TOM_HEGGER_MENUID               = 9217, //What do you know about the Cult of the Damned?
    GOSSIP_ITEM_GUARD_MITCHELLS_MENUID          = 9219, //How long have you worked for the Cult of the Damned?
    GOSSIP_ITEM_SALTY_JOHN_THORPE_MENUID        = 9218, //I have a reason to believe you're involved in the cultist activity
    GOSSIP_ITEM_HIDDEN_CULTIST_OPTIONID         = 0
};

struct npc_hidden_cultist : public ScriptedAI
{
    npc_hidden_cultist(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        uiEmoteState = creature->GetEmoteState();
        uiNpcFlags = creature->GetNpcFlags();
    }

    void Initialize()
    {
        uiEventTimer = 0;
        uiEventPhase = 0;

        uiPlayerGUID.Clear();
    }

    Emote uiEmoteState;
    NPCFlags uiNpcFlags;

    uint32 uiEventTimer;
    uint8 uiEventPhase;

    ObjectGuid uiPlayerGUID;

    void Reset() override
    {
        if (uiEmoteState)
            me->SetEmoteState(uiEmoteState);

        if (uiNpcFlags)
            me->ReplaceAllNpcFlags(uiNpcFlags);

        Initialize();

        DoCast(SPELL_SHROUD_OF_THE_DEATH_CULTIST);

        me->RestoreFaction();
    }

    void DoAction(int32 /*iParam*/) override
    {
        me->StopMoving();
        me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
        me->SetEmoteState(EMOTE_ONESHOT_NONE);
        if (Player* player = ObjectAccessor::GetPlayer(*me, uiPlayerGUID))
            me->SetFacingToObject(player);
        uiEventTimer = 3000;
        uiEventPhase = 1;
    }

    void AttackPlayer()
    {
        me->SetFaction(FACTION_MONSTER);
        if (Player* player = ObjectAccessor::GetPlayer(*me, uiPlayerGUID))
            AttackStart(player);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (uiEventTimer && uiEventTimer <= uiDiff)
        {
            switch (uiEventPhase)
            {
                case 1:
                    switch (me->GetEntry())
                    {
                        case NPC_SALTY_JOHN_THORPE:
                            Talk(SAY_HIDDEN_CULTIST_1);
                            uiEventTimer = 5000;
                            uiEventPhase = 2;
                            break;
                        case NPC_GUARD_MITCHELLS:
                            Talk(SAY_HIDDEN_CULTIST_2);
                            uiEventTimer = 5000;
                            uiEventPhase = 2;
                            break;
                        case NPC_TOM_HEGGER:
                            if (Player* player = ObjectAccessor::GetPlayer(*me, uiPlayerGUID))
                                Talk(SAY_HIDDEN_CULTIST_3, player);
                            uiEventTimer = 5000;
                            uiEventPhase = 2;
                            break;
                    }
                    break;
                case 2:
                    switch (me->GetEntry())
                    {
                        case NPC_SALTY_JOHN_THORPE:
                            Talk(SAY_HIDDEN_CULTIST_4);
                            if (Player* player = ObjectAccessor::GetPlayer(*me, uiPlayerGUID))
                                me->SetFacingToObject(player);
                            uiEventTimer = 3000;
                            uiEventPhase = 3;
                            break;
                        case NPC_GUARD_MITCHELLS:
                        case NPC_TOM_HEGGER:
                            AttackPlayer();
                            uiEventPhase = 0;
                            break;
                    }
                    break;
                case 3:
                    if (me->GetEntry() == NPC_SALTY_JOHN_THORPE)
                    {
                        AttackPlayer();
                        uiEventPhase = 0;
                    }
                    break;
            }
        }else uiEventTimer -= uiDiff;

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

    bool OnGossipHello(Player* player) override
    {
        uint32 uiGossipText = 0;
        uint32 charGossipItem = 0;

        switch (me->GetEntry())
        {
            case NPC_TOM_HEGGER:
                uiGossipText = GOSSIP_TEXT_TOM_HEGGER;
                charGossipItem = GOSSIP_ITEM_TOM_HEGGER_MENUID;
                break;
            case NPC_SALTY_JOHN_THORPE:
                uiGossipText = GOSSIP_TEXT_SALTY_JOHN_THORPE;
                charGossipItem = GOSSIP_ITEM_SALTY_JOHN_THORPE_MENUID;
                break;
            case NPC_GUARD_MITCHELLS:
                uiGossipText = GOSSIP_TEXT_GUARD_MITCHELSS;
                charGossipItem = GOSSIP_ITEM_GUARD_MITCHELLS_MENUID;
                break;
            default:
                return false;
        }

        if (player->HasAura(SPELL_RIGHTEOUS_VISION) && player->GetQuestStatus(QUEST_THE_HUNT_IS_ON) == QUEST_STATUS_INCOMPLETE)
            AddGossipItemFor(player, charGossipItem, GOSSIP_ITEM_HIDDEN_CULTIST_OPTIONID, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

        if (me->IsVendor())
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, GOSSIP_TEXT_BROWSE_GOODS, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

        SendGossipMenuFor(player, uiGossipText, me->GetGUID());

        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        ClearGossipMenuFor(player);

        if (action == GOSSIP_ACTION_INFO_DEF + 1)
        {
            CloseGossipMenuFor(player);
            uiPlayerGUID = player->GetGUID();
            DoAction(1);
        }

        if (action == GOSSIP_ACTION_TRADE)
            player->GetSession()->SendListInventory(me->GetGUID());

        return true;
    }
};

enum WindsoulTotemAura
{
    SPELL_WINDSOUL_CREDT = 46378
};

// 46374 - Windsoul Totem Aura
class spell_windsoul_totem_aura : public AuraScript
{
    PrepareAuraScript(spell_windsoul_totem_aura);

    void OnRemove(AuraEffect const*, AuraEffectHandleModes)
    {
        if (GetTarget()->isDead())
            if (Unit* caster = GetCaster())
                caster->CastSpell(nullptr, SPELL_WINDSOUL_CREDT);
    }

    void Register() override
    {
        OnEffectRemove += AuraEffectRemoveFn(spell_windsoul_totem_aura::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum BloodsporeRuination
{
    NPC_BLOODMAGE_LAURITH   = 25381,
    SAY_BLOODMAGE_LAURITH   = 0,
    EVENT_TALK              = 1,
    EVENT_RESET_ORIENTATION
};

// 45997 - Bloodspore Ruination
class spell_q11719_bloodspore_ruination_45997 : public SpellScript
{
    PrepareSpellScript(spell_q11719_bloodspore_ruination_45997);

    void HandleEffect(SpellEffIndex /*effIndex*/)
    {
        if (Unit* caster = GetCaster())
            if (Creature* laurith = caster->FindNearestCreature(NPC_BLOODMAGE_LAURITH, 100.0f))
                laurith->AI()->SetGUID(caster->GetGUID());
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_q11719_bloodspore_ruination_45997::HandleEffect, EFFECT_1, SPELL_EFFECT_SEND_EVENT);
    }
};

struct npc_bloodmage_laurith : public ScriptedAI
{
    npc_bloodmage_laurith(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _events.Reset();
        _playerGUID.Clear();
    }

    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        if (!_playerGUID.IsEmpty())
            return;

        _playerGUID = guid;

        if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
            me->SetFacingToObject(player);

        _events.ScheduleEvent(EVENT_TALK, 1s);
    }

    void UpdateAI(uint32 diff) override
    {
        if (UpdateVictim())
        {
            DoMeleeAttackIfReady();
            return;
        }

        _events.Update(diff);

        if (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_TALK:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                        Talk(SAY_BLOODMAGE_LAURITH, player);
                    _playerGUID.Clear();
                    _events.ScheduleEvent(EVENT_RESET_ORIENTATION, 5s);
                    break;
                case EVENT_RESET_ORIENTATION:
                    me->SetFacingTo(me->GetHomePosition().GetOrientation());
                    break;
            }
        }
    }

private:
    EventMap _events;
    ObjectGuid _playerGUID;
};

enum ShorteningBlaster
{
    SPELL_SHORTENING_BLASTER_BIGGER1    = 45674,
    SPELL_SHORTENING_BLASTER_SHRUNK1    = 45675,
    SPELL_SHORTENING_BLASTER_YELLOW1    = 45678,
    SPELL_SHORTENING_BLASTER_GHOST1     = 45682,
    SPELL_SHORTENING_BLASTER_POLYMORPH1 = 45684,

    SPELL_SHORTENING_BLASTER_BIGGER2    = 45673,
    SPELL_SHORTENING_BLASTER_SHRUNK2    = 45672,
    SPELL_SHORTENING_BLASTER_YELLOW2    = 45677,
    SPELL_SHORTENING_BLASTER_GHOST2     = 45682,
    SPELL_SHORTENING_BLASTER_POLYMORPH2 = 45683
};

// 45668 - Crafty's Ultra-Advanced Proto-Typical Shortening Blaster
class spell_q11653_shortening_blaster : public SpellScript
{
    PrepareSpellScript(spell_q11653_shortening_blaster);

    void HandleScript(SpellEffIndex /* effIndex */)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();

        uint32 spellId = RAND(SPELL_SHORTENING_BLASTER_BIGGER1, SPELL_SHORTENING_BLASTER_SHRUNK1, SPELL_SHORTENING_BLASTER_YELLOW1,
            SPELL_SHORTENING_BLASTER_GHOST1, SPELL_SHORTENING_BLASTER_POLYMORPH1);
        uint32 spellId2 = RAND(SPELL_SHORTENING_BLASTER_BIGGER2, SPELL_SHORTENING_BLASTER_SHRUNK2, SPELL_SHORTENING_BLASTER_YELLOW2,
            SPELL_SHORTENING_BLASTER_GHOST2, SPELL_SHORTENING_BLASTER_POLYMORPH2);

        caster->CastSpell(caster, spellId, true);
        target->CastSpell(target, spellId2, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_q11653_shortening_blaster::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 11611: Taken by the Scourge
######*/

enum TakenByTheScourge
{
    SPELL_FREED_WARSONG_MAGE        = 45526,
    SPELL_FREED_WARSONG_SHAMAN      = 45527,
    SPELL_FREED_WARSONG_WARRIOR     = 45514,
    SPELL_FREED_WARSONG_PEON        = 45532,
    SPELL_FREED_SOLDIER_DEBUFF      = 45523
};

std::array<uint32, 3> const CocoonSummonSpells =
{
    SPELL_FREED_WARSONG_MAGE, SPELL_FREED_WARSONG_SHAMAN, SPELL_FREED_WARSONG_WARRIOR
};

// 45516 - Nerub'ar Web Random Unit (Not On Quest, Script Effect)
class spell_borean_tundra_nerubar_web_random_unit_not_on_quest : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_nerubar_web_random_unit_not_on_quest);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->GetEffect(EFFECT_0).CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), GetEffectInfo().CalcValue(), true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_borean_tundra_nerubar_web_random_unit_not_on_quest::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 45515 - Nerub'ar Web Random Unit (Not On Quest, Dummy)
class spell_borean_tundra_nerubar_web_random_unit_not_on_quest_dummy : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_nerubar_web_random_unit_not_on_quest_dummy);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells) && ValidateSpellInfo({ SPELL_FREED_SOLDIER_DEBUFF });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();

        // Do nothing if has 3 soldiers
        Aura* aura = caster->GetAura(SPELL_FREED_SOLDIER_DEBUFF);
        if (!aura || aura->GetStackAmount() < 3)
            caster->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_borean_tundra_nerubar_web_random_unit_not_on_quest_dummy::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 45535 - Nerub'ar Web Random Unit (On Quest, Dummy)
class spell_borean_tundra_nerubar_web_random_unit_on_quest_dummy : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_nerubar_web_random_unit_on_quest_dummy);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells) && ValidateSpellInfo({ SPELL_FREED_SOLDIER_DEBUFF, SPELL_FREED_WARSONG_PEON });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();

        // Always summon peon if has 3 soldiers
        Aura* aura = caster->GetAura(SPELL_FREED_SOLDIER_DEBUFF);
        if ((!aura || aura->GetStackAmount() < 3) && roll_chance_i(75))
            caster->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
        else
            caster->CastSpell(nullptr, SPELL_FREED_WARSONG_PEON, true);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_borean_tundra_nerubar_web_random_unit_on_quest_dummy::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 45522 - Dispel Freed Soldier Debuff
class spell_borean_tundra_dispel_freed_soldier_debuff : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_dispel_freed_soldier_debuff);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->GetEffect(EFFECT_0).CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Aura* aura = GetHitUnit()->GetAura(GetEffectInfo().CalcValue()))
            aura->ModStackAmount(-1);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_borean_tundra_dispel_freed_soldier_debuff::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 11690: Bring 'Em Back Alive
######*/

enum BringEmBackAlive
{
    SPELL_KODO_DELIVERED   = 48203,

    TEXT_DELIVERED_1       = 24881,
    TEXT_DELIVERED_2       = 24882,
    TEXT_DELIVERED_3       = 26284,
    TEXT_DELIVERED_4       = 26285,
    TEXT_DELIVERED_5       = 26286
};

// 45877 - Deliver Kodo
class spell_borean_tundra_deliver_kodo : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_deliver_kodo);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_KODO_DELIVERED });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_KODO_DELIVERED, true);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_borean_tundra_deliver_kodo::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 48204 - Kodo Delivered
class spell_borean_tundra_kodo_delivered : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_kodo_delivered);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return sObjectMgr->GetBroadcastText(TEXT_DELIVERED_1) &&
            sObjectMgr->GetBroadcastText(TEXT_DELIVERED_2) &&
            sObjectMgr->GetBroadcastText(TEXT_DELIVERED_3) &&
            sObjectMgr->GetBroadcastText(TEXT_DELIVERED_4) &&
            sObjectMgr->GetBroadcastText(TEXT_DELIVERED_5);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->Unit::Say(RAND(TEXT_DELIVERED_1, TEXT_DELIVERED_2, TEXT_DELIVERED_3, TEXT_DELIVERED_4, TEXT_DELIVERED_5), caster);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_borean_tundra_kodo_delivered::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 11648: The Art of Persuasion
######*/

enum TheArtOfPersuasion
{
    WHISPER_TORTURE_1                      = 0,
    WHISPER_TORTURE_2                      = 1,
    WHISPER_TORTURE_3                      = 2,
    WHISPER_TORTURE_4                      = 3,
    WHISPER_TORTURE_5                      = 4,
    WHISPER_TORTURE_RANDOM_1               = 5,
    WHISPER_TORTURE_RANDOM_2               = 6,
    WHISPER_TORTURE_RANDOM_3               = 7,

    WHISPER_TORTURE_PROTO_1                = 8,
    WHISPER_TORTURE_PROTO_2                = 9,
    WHISPER_TORTURE_PROTO_3                = 10,
    WHISPER_TORTURE_PROTO_4                = 11,
    WHISPER_TORTURE_PROTO_5                = 12,
    WHISPER_TORTURE_PROTO_6                = 13,
    WHISPER_TORTURE_PROTO_7                = 14,
    WHISPER_TORTURE_PROTO_8                = 15,
    WHISPER_TORTURE_PROTO_9                = 16,
    WHISPER_TORTURE_PROTO_10               = 17,

    SPELL_NEURAL_NEEDLE_IMPACT             = 45702,
    SPELL_PROTOTYPE_NEURAL_NEEDLE_IMPACT   = 48254
};

// 45634 - Neural Needle
class spell_borean_tundra_neural_needle : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_neural_needle);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_NEURAL_NEEDLE_IMPACT });
    }

    void HandleWhisper()
    {
        Player* caster = GetCaster()->ToPlayer();
        Creature* target = GetHitCreature();
        if (!caster || !target)
            return;

        target->CastSpell(target, SPELL_NEURAL_NEEDLE_IMPACT);

        if (Aura* aura = caster->GetAura(GetSpellInfo()->Id))
        {
            switch (aura->GetStackAmount())
            {
                case 1:
                    target->AI()->Talk(WHISPER_TORTURE_1, caster);
                    break;
                case 2:
                    target->AI()->Talk(WHISPER_TORTURE_2, caster);
                    break;
                case 3:
                    target->AI()->Talk(WHISPER_TORTURE_3, caster);
                    break;
                case 4:
                    target->AI()->Talk(WHISPER_TORTURE_4, caster);
                    break;
                case 5:
                    target->AI()->Talk(WHISPER_TORTURE_5, caster);
                    caster->KilledMonsterCredit(target->GetEntry());
                    break;
                case 6:
                    target->AI()->Talk(RAND(WHISPER_TORTURE_RANDOM_1, WHISPER_TORTURE_RANDOM_2, WHISPER_TORTURE_RANDOM_3), caster);
                    break;
                default:
                    return;
            }
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_borean_tundra_neural_needle::HandleWhisper);
    }
};

// 48252 - Prototype Neural Needle
class spell_borean_tundra_prototype_neural_needle : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_prototype_neural_needle);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PROTOTYPE_NEURAL_NEEDLE_IMPACT });
    }

    void HandleWhisper()
    {
        Player* caster = GetCaster()->ToPlayer();
        Creature* target = GetHitCreature();
        if (!caster || !target)
            return;

        target->CastSpell(target, SPELL_PROTOTYPE_NEURAL_NEEDLE_IMPACT);

        uint32 text = 0;
        if (Aura* aura = caster->GetAura(GetSpellInfo()->Id))
        {
            switch (aura->GetStackAmount())
            {
                case 1: text = WHISPER_TORTURE_PROTO_1; break;
                case 2: text = WHISPER_TORTURE_PROTO_2; break;
                case 3: text = WHISPER_TORTURE_PROTO_3; break;
                case 4: text = WHISPER_TORTURE_PROTO_4; break;
                case 5: text = WHISPER_TORTURE_PROTO_5; break;
                case 6: text = WHISPER_TORTURE_PROTO_6; break;
                case 7: text = WHISPER_TORTURE_PROTO_7; break;
                case 8: text = WHISPER_TORTURE_PROTO_8; break;
                case 9: text = WHISPER_TORTURE_PROTO_9; break;
                case 10: text = WHISPER_TORTURE_PROTO_10; break;
                default: return;
            }
        }

        if (text)
            target->AI()->Talk(text, caster);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_borean_tundra_prototype_neural_needle::HandleWhisper);
    }
};

/*######
## Quest 11587: Prison Break
######*/

enum PrisonBreak
{
    SPELL_SUMMON_ARCANE_PRISONER_1    = 45446,
    SPELL_SUMMON_ARCANE_PRISONER_2    = 45448
};

// 45449 - Arcane Prisoner Rescue
class spell_borean_tundra_arcane_prisoner_rescue : public SpellScript
{
    PrepareSpellScript(spell_borean_tundra_arcane_prisoner_rescue);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_ARCANE_PRISONER_1, SPELL_SUMMON_ARCANE_PRISONER_2 });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), RAND(SPELL_SUMMON_ARCANE_PRISONER_1, SPELL_SUMMON_ARCANE_PRISONER_2));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_borean_tundra_arcane_prisoner_rescue::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

void AddSC_borean_tundra()
{
    RegisterGameObjectAI(go_caribou_trap);
    RegisterSpellScript(spell_red_dragonblood);
    new npc_thassarian();
    new npc_image_lich_king();
    new npc_counselor_talbot();
    new npc_leryssa();
    new npc_general_arlos();
    RegisterCreatureAI(npc_beryl_sorcerer);
    RegisterCreatureAI(npc_trapped_mammoth_calf);
    RegisterCreatureAI(npc_valiance_keep_cannoneer);
    RegisterCreatureAI(npc_hidden_cultist);
    RegisterSpellScript(spell_windsoul_totem_aura);
    RegisterSpellScript(spell_q11719_bloodspore_ruination_45997);
    RegisterCreatureAI(npc_bloodmage_laurith);
    RegisterSpellScript(spell_q11653_shortening_blaster);
    RegisterSpellScript(spell_borean_tundra_nerubar_web_random_unit_not_on_quest);
    RegisterSpellScript(spell_borean_tundra_nerubar_web_random_unit_not_on_quest_dummy);
    RegisterSpellScript(spell_borean_tundra_nerubar_web_random_unit_on_quest_dummy);
    RegisterSpellScript(spell_borean_tundra_dispel_freed_soldier_debuff);
    RegisterSpellScript(spell_borean_tundra_deliver_kodo);
    RegisterSpellScript(spell_borean_tundra_kodo_delivered);
    RegisterSpellScript(spell_borean_tundra_neural_needle);
    RegisterSpellScript(spell_borean_tundra_prototype_neural_needle);
    RegisterSpellScript(spell_borean_tundra_arcane_prisoner_rescue);
}
