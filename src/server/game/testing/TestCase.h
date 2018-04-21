#ifndef TESTCASE_H
#define TESTCASE_H

#include "Duration.h"
#include <string>
#include "SharedDefines.h"
#include "DBCEnums.h"
#include "SpellMgr.h"
#include "ScriptMgr.h"
#include "Unit.h"
#include <functional>

#define TEST_CREATURE_ENTRY 8

class TestMap;
class TestThread;
class TestPlayer;
class WorldLocation;
class Player;
class TestPlayer;
class Creature;
class GameObject;
struct Position;
class TempSummon;


//input info for next TEST_ASSERT check
#define ASSERT_INFO(expr, ...) _AssertInfo(expr, ## __VA_ARGS__)
#define TEST_ASSERT( expr ) { Assert(__FILE__, __LINE__, __FUNCTION__, (expr == true), #expr); _ResetAssertInfo(); }

template<class T>
bool Between(T value, T from, T to)
{
    if (from > to) //extra protection against both typos and underflow
        return false;
    return value >= from && value <= to;
}

enum TestStatus
{
    //Test is working and should pass. A failure means a regression.
    STATUS_PASSING,
    //Test is working, but failure is expected.
    STATUS_KNOWN_BUG,
    //Test is working and should pass, but still miss some features (handled as STATUS_PASSING by the core, this is just a way to mark test as "need to be refined")
    STATUS_PARTIAL,
    //Test is not yet finished and will be ignored unless directly called
    STATUS_INCOMPLETE,
};

class TC_GAME_API TestCase
{
    friend class TestMgr;
    friend class TestThread;
    friend class TestMap;

public:
    //If needMap is specified, test will be done in an instance of the test map (13). 
    TestCase(TestStatus status, bool needMap = true);
    //Use specific position. If only map was specified in location, default coordinates in map may be chosen instead. If you need creatures and objects, use EnableMapObjects in your test constructor
    TestCase(TestStatus status, WorldLocation const& specificPosition);

    std::string GetName() const { return _testName; }
    bool Failed() const { return _failed; }
    std::string GetError() const { return _errMsg; }
    uint32 GetTestCount() const { return _testsCount; }
    bool IsSetup() const { return _setup; }
    WorldLocation const& GetLocation() const { return _location; }
    //Get a hardcoded default position on map to place the test instead of always going to 0,0,0
    static Position GetDefaultPositionForMap(uint32 mapId);

    // Main test function to be implemented by each test
    virtual void Test() = 0;
    // Cleanup function, always called after the test whatever the result
    virtual void Cleanup() { }


    // Utility functions
    void SetDifficulty(Difficulty diff) { _diff = diff; }
    TestMap* GetMap() const { return _map; }
    void EnableMapObjects();

    //Spawn player. Fail test on failure
    TestPlayer* SpawnRandomPlayer();
    //Spawn player. Fail test on failure
    TestPlayer* SpawnRandomPlayer(Powers power, uint32 level = 70);
    //Spawn player. Fail test on failure
    TestPlayer* SpawnRandomPlayer(Races race, uint32 level = 70);
    //Spawn player. Fail test on failure
    TestPlayer* SpawnRandomPlayer(Classes cls, uint32 level = 70);
    //Spawn player. Fail test on failure
    TestPlayer* SpawnPlayer(Classes cls, Races _race, uint32 level = 70, Position spawnPosition = {});
    //Spawn creature. Fail test on failure
    TempSummon* SpawnCreature(uint32 entry = 0, bool spawnInFront = true);
    //Spawn creature. Fail test on failure
    TempSummon* SpawnCreatureWithPosition(Position spawnPosition, uint32 entry = 0);

    //This checks if item exists in loot (but we cannot say if player can actually loot it)
    bool HasLootForMe(Creature*, Player*, uint32 itemID);
    //Create item and equip it to player. Will remove any item already in slot. Fail test on failure
    #define EQUIP_ITEM(player, itemID) { _SetCaller(__FILE__, __LINE__); _EquipItem(player, itemID); _ResetCaller(); }

    void RemoveAllEquipedItems(TestPlayer* player);
    void RemoveItem(TestPlayer* player, uint32 itemID, uint32 count);
    void LearnTalent(TestPlayer* p, uint32 spellID);
    void EnableCriticals(Unit* caster, bool crit);
    //Invite player into leader group. Group is created if not yet existing.
    void GroupPlayer(TestPlayer* leader, Player* player);
    static std::string StringifySpellCastResult(uint32 result) { return StringifySpellCastResult(SpellCastResult(result)); }
    static std::string StringifySpellCastResult(SpellCastResult result);

    /* Cast a spell and check for spell start return value (SpellCastResult)
    Usage:
    TEST_CAST(caster, victim, spellID)
    TEST_CAST(caster, victim, spellID, expectedCode)
    TEST_CAST(caster, victim, spellID, expectedCode, triggeredFlags)
    */
    #define TEST_CAST( ... ) { _SetCaller(__FILE__, __LINE__);  _TestCast(__VA_ARGS__); _ResetCaller(); }

    /* Cast a spell with forced hit result (SpellMissInfo). Fails test if spell fail to launch.
    Usage:
    FORCE_CAST(caster, victim, spellID)
    FORCE_CAST(caster, victim, spellID, forcedMissInfo)
    FORCE_CAST(caster, victim, spellID, forcedMissInfo, triggeredFlags)
    */
    #define FORCE_CAST( ... ) { _SetCaller(__FILE__, __LINE__);  _ForceCast(__VA_ARGS__); _ResetCaller(); }

    /* Usage:
    TEST_HAS_AURA(target, spellID)
    */
    #define TEST_HAS_AURA( ... ) { _SetCaller(__FILE__, __LINE__); _EnsureHasAura(__VA_ARGS__); _ResetCaller(); }
    /* Usage:
    TEST_HAS_NOT_AURA(target, spellID)
    */
    #define TEST_HAS_NOT_AURA( ... ) { _SetCaller(__FILE__, __LINE__); _EnsureHasNotAura(__VA_ARGS__); _ResetCaller(); }

    /* check if target has aura and if maximum duration match given duration
    durationMS: can be either uint32 or std::chrono::duration (such as Milliseconds)
    */
    #define TEST_AURA_MAX_DURATION(target, spellID, durationMS) { _SetCaller(__FILE__, __LINE__); _TestAuraMaxDuration(target, spellID, durationMS); _ResetCaller(); }

    #define TEST_AURA_STACK(target, spellID, stacks) { _SetCaller(__FILE__, __LINE__); _TestAuraStack(target, spellID, stacks, true); _ResetCaller(); }
    #define TEST_AURA_CHARGE(target, spellID, stacks) { _SetCaller(__FILE__, __LINE__); _TestAuraStack(target, spellID, stacks, false); _ResetCaller(); }

    float CalcChance(uint32 iterations, const std::function<bool()>& f);
    ///!\ This is VERY slow, do not abuse of this function. Randomize talents, spells, stuff for this player
    void RandomizePlayer(TestPlayer* player);

    // Testing functions 
    // Test must use macro so that we can store from which line their calling. If calling function does a direct call without using the macro, we just print the internal line */

    /* Will cast the spell a bunch of time and test if results match the expected damage.
     Caster must be a TestPlayer or a pet/summon of him
     Note for multithread: You can only have only one TestDirectSpellDamage function running for each caster/target combination at the same time*/
    #define TEST_DIRECT_SPELL_DAMAGE(caster, target, spellID, expectedMinDamage, expectedMaxDamage, crit) { _SetCaller(__FILE__, __LINE__); _TestDirectValue(caster, target, spellID, expectedMinDamage, expectedMaxDamage, crit, true, {}); _ResetCaller(); }
    // same as TEST_DIRECT_SPELL_DAMAGE but you can give a callbck function to use before each cast, with the type std::function<void(Unit*, Unit*)>
    typedef std::function<void(Unit*, Unit*)> TestCallback;
    #define TEST_DIRECT_SPELL_DAMAGE_CALLBACK(caster, target, spellID, expectedMinDamage, expectedMaxDamage, crit, callback) { _SetCaller(__FILE__, __LINE__); _TestDirectValue(caster, target, spellID, expectedMinDamage, expectedMaxDamage, crit, true, Optional<TestCallback>(callback)); _ResetCaller(); }
     //Caster must be a TestPlayer or a pet/summon of him
    #define TEST_DIRECT_HEAL(caster, target, spellID, expectedHealMin, expectedHealMax, crit) { _SetCaller(__FILE__, __LINE__); _TestDirectValue(caster, target, spellID, expectedHealMin, expectedHealMax, crit, false, {}); _ResetCaller(); }
    //Caster must be a TestPlayer or a pet/summon of him
    #define TEST_MELEE_DAMAGE(player, target, attackType, expectedMin, expectedMax, crit) { _SetCaller(__FILE__, __LINE__); _TestMeleeDamage(player, target, attackType, expectedMin, expectedMax, crit); _ResetCaller(); }
  
    /* 
    @expectedAmount negative values for healing
    @crit Set crit score of caster to maximum
    */
    #define TEST_DOT_DAMAGE(caster, target, spellID, expectedAmount, crit) { _SetCaller(__FILE__, __LINE__); _TestDotDamage(caster, target, spellID, expectedAmount, crit); _ResetCaller(); }
  
    #define TEST_CHANNEL_DAMAGE(caster, target, spellID, testedSpellID, tickCount, expectedAmount) { _SetCaller(__FILE__, __LINE__); _TestChannelDamage(caster, target, spellID, testedSpellID, tickCount, expectedAmount); _ResetCaller(); }
    #define TEST_CHANNEL_HEALING(caster, target, spellID, testedSpellID, tickCount, expectedAmount) { _SetCaller(__FILE__, __LINE__); _TestChannelDamage(caster, target, spellID, testedSpellID, tickCount, expectedAmount, true); _ResetCaller(); }

    /* Cast given spells a bunch of time from caster on victim, and test if results are chance% given missInfo
    chance: 0-100
    */
    #define TEST_SPELL_HIT_CHANCE(caster, victim, spellID, chance, missInfo) { _SetCaller(__FILE__, __LINE__); _TestSpellHitChance(caster, victim, spellID, chance, missInfo); _ResetCaller(); }
    /* Triggers attack from caster on victim, and test if results are chance% given missInfo
    chance: 0-100
    */
    #define TEST_MELEE_HIT_CHANCE(caster, victim, weaponAttackType, chance, missInfo) { _SetCaller(__FILE__, __LINE__); _TestMeleeHitChance(caster, victim, weaponAttackType, chance, missInfo); _ResetCaller(); }
    // Test the percentage of a melee hit outcome for already done attacks
    #define TEST_MELEE_OUTCOME_PERCENTAGE(attacker, victim, weaponAttackType, meleeHitOutcome, expectedResult, allowedError)  { _SetCaller(__FILE__, __LINE__); _TestMeleeOutcomePercentage(attacker, victim, weaponAttackType, meleeHitOutcome, expectedResult, allowedError);  _ResetCaller(); }
        // Test the percentage of a spell hit outcome for already done attacks
    #define TEST_SPELL_OUTCOME_PERCENTAGE(attacker, victim, spellId, missType, expectedResult, allowedError)  { _SetCaller(__FILE__, __LINE__); _TestSpellOutcomePercentage(attacker, victim, spellId, missType, expectedResult, allowedError);  _ResetCaller(); }

    #define TEST_STACK_COUNT(caster, target, talent, castSpellID, testSpellID, requireCount) { _SetCaller(__FILE__, __LINE__); _TestStacksCount(caster, target, castSpellID, testSpellID, requireCount); _ResetCaller(); }

    //Cast given spell and check its power cost
    #define TEST_POWER_COST(caster, target, castSpellID, powerType, expectedPowerCost) { _SetCaller(__FILE__, __LINE__); _TestPowerCost(caster, target, castSpellID, powerType, expectedPowerCost); _ResetCaller(); }

    /* Check remaining cooldown for given spellID
    cooldownSecond: can be either uint32 or std::chrono::duration (such as Seconds)
    */
    #define TEST_HAS_COOLDOWN(caster, spellID, cooldownSecond) { _SetCaller(__FILE__, __LINE__); _TestHasCooldown(caster, spellID, cooldownSecond); _ResetCaller(); }

    //crit: get only spells that made crit / only spells that did not
    void GetDamagePerSpellsTo(TestPlayer* caster, Unit* to, uint32 spellID, uint32& minDamage, uint32& maxDamage, Optional<bool> crit, uint32 expectedCount = 0);
    void GetHealingPerSpellsTo(TestPlayer* caster, Unit* target, uint32 spellID, uint32& minHeal, uint32& maxHeal, Optional<bool> crit, uint32 expectedCount = 0);
    void GetWhiteDamageDoneTo(TestPlayer* caster, Unit* target, WeaponAttackType attackType, bool critical, uint32& minDealt, uint32& maxDealt, uint32 expectedCount = 0);
    float GetChannelDamageTo(TestPlayer* caster, Unit* to, uint32 spellID, uint32 tickCount, bool& mustRetry);
    float GetChannelHealingTo(TestPlayer* caster, Unit* to, uint32 spellID, uint32 tickCount, bool& mustRetry);

    static uint32 GetTestBotAccountId();
    TestStatus GetTestStatus() const { return _testStatus; }

protected:

    //Scripting function
    void Wait(uint32 ms);
    void Wait(Seconds s);
    void Wait(Milliseconds s);
    //Main check function, used by TEST_ASSERT macro. Will stop execution on failure
    void Assert(std::string file, int32 line, std::string function, bool condition, std::string failedCondition);

    // Test Map
    TestMap*                 _map;
    uint32                   _testMapInstanceId;
    Difficulty               _diff;
    WorldLocation            _location;

    void _AssertInfo(const char* err, ...) ATTR_PRINTF(2, 3);
    void _ResetAssertInfo();
    void _SetCaller(std::string callerFile, int32 callerLine);
    void _ResetCaller();
    void Celebrate();

    void _TestDirectValue(Unit* caster, Unit* target, uint32 spellID, uint32 expectedMin, uint32 expectedMax, bool crit, bool damage, Optional<TestCallback> callback); //if !damage, then use healing
    void _TestMeleeDamage(Unit* caster, Unit* target, WeaponAttackType attackType, uint32 expectedMin, uint32 expectedMax, bool crit);
    void _TestDotDamage(TestPlayer* caster, Unit* target, uint32 spellID, int32 expectedAmount, bool crit = false);
    void _TestChannelDamage(TestPlayer* caster, Unit* target, uint32 spellID, uint32 testedSpell, uint32 tickCount, int32 expectedTickAmount, bool healing = false);
    /* if sampleSize != 0, check if results count = sampleSize
    expectedResult: 0 - 100
    allowedError: 0 - 100
    */
    void _TestMeleeOutcomePercentage(TestPlayer* attacker, Unit* victim, WeaponAttackType weaponAttackType, MeleeHitOutcome meleeHitOutcome, float expectedResult, float allowedError, uint32 sampleSize = 0);
    /* if sampleSize != 0, check if results count = sampleSize
    expectedResult: 0 - 100
    allowedError: 0 - 100
    */
    void _TestSpellOutcomePercentage(TestPlayer* attacker, Unit* victim, uint32 spellId, SpellMissInfo hitInfo, float expectedResult, float allowedError, uint32 sampleSize = 0);
    void _TestSpellHitChance(TestPlayer* caster, TestPlayer* victim, uint32 spellID, float chance, SpellMissInfo missInfo);
    void _TestMeleeHitChance(TestPlayer* caster, TestPlayer* victim, WeaponAttackType weaponAttackType, float chance, MeleeHitOutcome meleeHitOutcome);

	void _TestStacksCount(TestPlayer* caster, Unit* target, uint32 castSpell, uint32 testSpell, uint32 requireCount);
	void _TestPowerCost(TestPlayer* caster, Unit* target, uint32 castSpell, Powers powerType, uint32 expectedPowerCost);
    void _EquipItem(TestPlayer* p, uint32 itemID);
    //if negative, ensure has NOT aura
    void _EnsureHasAura(Unit* target, int32 spellID);
    void _EnsureHasNotAura(Unit* target, int32 spellID) { _EnsureHasAura(target, -spellID); }
    void _TestHasCooldown(TestPlayer* caster, uint32 castSpellID, uint32 cooldownSecond);
    inline void _TestHasCooldown(TestPlayer* caster, uint32 castSpellID, Seconds s) { _TestHasCooldown(caster, castSpellID, uint32(s.count())); }
    void _TestAuraMaxDuration(Unit* target, uint32 spellID, uint32 durationMS);
    inline void _TestAuraMaxDuration(Unit* target, uint32 spellID, Milliseconds ms) { _TestAuraMaxDuration(target, spellID, uint32(ms.count())); }
    void _TestAuraStack(Unit* target, uint32 spellID,uint32 stacks, bool stack);
    void _TestCast(Unit* caster, Unit* victim, uint32 spellID, SpellCastResult expectedCode = SPELL_CAST_OK, TriggerCastFlags triggeredFlags = TRIGGERED_NONE);
    void _ForceCast(Unit* caster, Unit* victim, uint32 spellID, SpellMissInfo forcedMissInfo = SPELL_MISS_NONE, TriggerCastFlags triggeredFlags = TRIGGERED_NONE);

    //Returns how much iterations you should do and how much error you should allow for a given damage range (with a 99.9% certainty)
    static void _GetApproximationParams(uint32& sampleSize, uint32& allowedError, uint32 const expectedMin, uint32 const expectedMax);
    //Returns how much iterations and how much tolerance you should allow for given:
    //expectedResult: % from absoluteTolerance*2 to 1.0f
    //absoluteTolerance: % from 0.0f to 1.0f. Error tolerance.
    void _GetPercentApproximationParams(uint32& sampleSize, float& resultingAbsoluteTolerance, float const expectedResult, float const absoluteTolerance = 0.01f);

private:
    std::string              _testName;
    std::string              _errMsg;
    uint32                   _testsCount;
    bool                     _failed;
    std::atomic<bool>        _setup;
    bool                     _enableMapObjects;
    std::string              _callerFile; //used for error output
    int32                    _callerLine; //used for error output
    std::string              _internalAssertInfo;
    std::string              _assertInfo;
    TestStatus               _testStatus;

    bool _InternalSetup();
    void _Cleanup();
    void _Fail(const char* err, ...) ATTR_PRINTF(2, 3);
    void _FailNoException(std::string);
    void _SetName(std::string name) { _testName = name; }
    void _SetThread(TestThread* testThread) { _testThread = testThread; }
    //if callerFile and callerLine are specified, also print them in message
    void _Assert(std::string file, int32 line, std::string function, bool condition, std::string failedCondition, bool increaseTestCount, std::string callerFile = "", int32 callerLine = 0);
    void _InternalAssertInfo(const char* err, ...) ATTR_PRINTF(2, 3);
    void _ResetInternalAssertInfo();

      std::string _GetCallerFile();
    int32 _GetCallerLine();

    /* return a new randomized test bot. Returned player must be deleted by the caller
    if level == 0, set bot at max player level
    */
    TestPlayer* _CreateTestBot(Position loc, Classes cls, Races race, uint32 level = 0);
    void _GetRandomClassAndRace(Classes& cls, Races& race, bool forcePower = false, Powers power = POWER_MANA);
    Classes _GetRandomClassForRace(Races race);
    Races _GetRandomRaceForClass(Classes race);
    static void _RemoveTestBot(Player* player);
   
    TestThread* _testThread;

    //those two just to help avoiding calling SpawnRandomPlayer with the wrong arguments, SpawnPlayer should be called in those case
    TestPlayer* SpawnRandomPlayer(Races race, Classes cls) { return nullptr; }
    TestPlayer* SpawnRandomPlayer(Classes cls, Races races) { return nullptr; }
};

#endif //TESTCASE_H