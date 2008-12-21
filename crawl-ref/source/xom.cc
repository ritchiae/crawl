/*
 *  File:       xom.cc
 *  Summary:    All things Xomly
 *  Written by: Zooko
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 */

#include "AppHdr.h"

#include <algorithm>

#include "beam.h"
#include "branch.h"
#include "database.h"
#include "effects.h"
#include "it_use2.h"
#include "items.h"
#include "makeitem.h"
#include "message.h"
#include "misc.h"
#include "mon-util.h"
#include "monplace.h"
#include "monstuff.h"
#include "mutation.h"
#include "ouch.h"
#include "player.h"
#include "randart.h"
#include "religion.h"
#include "spells1.h"
#include "spells2.h"
#include "spells3.h"
#include "spl-cast.h"
#include "spl-mis.h"
#include "spl-util.h"
#include "state.h"
#include "stuff.h"
#include "view.h"
#include "xom.h"

#if DEBUG_RELIGION
#    define DEBUG_DIAGNOSTICS 1
#    define DEBUG_GIFTS       1
#endif

#if DEBUG_XOM
#    define DEBUG_DIAGNOSTICS 1
#    define DEBUG_RELIGION    1
#    define DEBUG_GIFTS       1
#endif

// Which spells?  First I copied all spells from your_spells(), and then
// I filtered some out, especially conjurations.  Then I sorted them in
// roughly ascending order of power.
static const spell_type _xom_spells[] =
{
    SPELL_BLINK, SPELL_CONFUSING_TOUCH, SPELL_MAGIC_MAPPING,
    SPELL_DETECT_ITEMS, SPELL_DETECT_CREATURES, SPELL_MASS_CONFUSION,
    SPELL_MASS_SLEEP, SPELL_DISPERSAL, SPELL_STONESKIN, SPELL_RING_OF_FLAMES,
    SPELL_OLGREBS_TOXIC_RADIANCE, SPELL_TUKIMAS_VORPAL_BLADE,
    SPELL_MAXWELLS_SILVER_HAMMER, SPELL_FIRE_BRAND, SPELL_FREEZING_AURA,
    SPELL_POISON_WEAPON, SPELL_STONEMAIL, SPELL_LETHAL_INFUSION,
    SPELL_EXCRUCIATING_WOUNDS, SPELL_WARP_BRAND, SPELL_TUKIMAS_DANCE,
    SPELL_RECALL, SPELL_SUMMON_BUTTERFLIES, SPELL_SUMMON_SMALL_MAMMALS,
    SPELL_SUMMON_SCORPIONS, SPELL_SUMMON_SWARM, SPELL_FLY, SPELL_SPIDER_FORM,
    SPELL_STATUE_FORM, SPELL_ICE_FORM, SPELL_DRAGON_FORM, SPELL_ANIMATE_DEAD,
    SPELL_SUMMON_WRAITHS, SPELL_SHADOW_CREATURES, SPELL_SUMMON_HORRIBLE_THINGS,
    SPELL_CALL_CANINE_FAMILIAR, SPELL_SUMMON_ICE_BEAST, SPELL_SUMMON_UGLY_THING,
    SPELL_CONJURE_BALL_LIGHTNING, SPELL_SUMMON_DRAGON, SPELL_DEATH_CHANNEL,
    SPELL_NECROMUTATION
};

static const char *_xom_message_arrays[NUM_XOM_MESSAGE_TYPES][6] =
{
    // XM_NORMAL
    {
        "Xom roars with laughter!",
        "Xom thinks this is hilarious!",
        "Xom is highly amused!",
        "Xom is amused.",
        "Xom is mildly amused.",
        "Xom is interested."
    },

    // XM_INTRIGUED
    {
        "Xom is fascinated!",
        "Xom is very intrigued!",
        "Xom is intrigued!",
        "Xom is extremely interested.",
        "Xom is very interested.",
        "Xom is interested."
    }
};

const char *describe_xom_favour()
{
    if (you.gift_timeout < 1)
        return "A BORING thing.";
    else
        return (you.piety > 180) ? "A beloved toy of Xom." :
           (you.piety > 160) ? "A favourite toy of Xom." :
           (you.piety > 140) ? "A very special toy of Xom." :
           (you.piety > 120) ? "A special toy of Xom." :
           (you.piety > 100) ? "A toy of Xom." :
           (you.piety >  80) ? "A plaything of Xom." :
           (you.piety >  60) ? "A special plaything of Xom." :
           (you.piety >  40) ? "A very special plaything of Xom." :
           (you.piety >  20) ? "A favourite plaything of Xom."
                             : "A beloved plaything of Xom.";
}

static std::string _get_xom_speech(const std::string key)
{
    std::string result = getSpeakString("Xom " + key);

    if (result.empty())
        result = getSpeakString("Xom general effect");

    if (result.empty())
        return ("Xom makes something happen.");

    return (result);
}

bool xom_is_nice()
{
    // If you.gift_timeout was 0, then Xom was BORED.  He HATES that.
    return (you.gift_timeout > 0 && you.piety > (MAX_PIETY / 2));
}

static void _xom_is_stimulated(int maxinterestingness,
                               const char *message_array[],
                               bool force_message)
{
    if (you.religion != GOD_XOM || maxinterestingness <= 0)
        return;

    // Xom is not directly stimulated by his own acts.
    if (crawl_state.which_god_acting() == GOD_XOM)
        return;

    int interestingness = random2(maxinterestingness);

#if DEBUG_RELIGION || DEBUG_GIFTS || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS,
         "Xom: maxinterestingness = %d, interestingness = %d",
         maxinterestingness, interestingness);
#endif

    interestingness = std::min(255, interestingness);

    bool was_stimulated = false;
    if (interestingness > you.gift_timeout && interestingness >= 12)
    {
        you.gift_timeout = interestingness;
        was_stimulated = true;
    }

    if (was_stimulated || force_message)
        god_speaks(GOD_XOM,
                   ((interestingness > 200) ? message_array[5] :
                    (interestingness > 100) ? message_array[4] :
                    (interestingness >  75) ? message_array[3] :
                    (interestingness >  50) ? message_array[2] :
                    (interestingness >  25) ? message_array[1]
                                            : message_array[0]));
}

void xom_is_stimulated(int maxinterestingness, xom_message_type message_type,
                       bool force_message)
{
    _xom_is_stimulated(maxinterestingness, _xom_message_arrays[message_type],
                       force_message);
}

void xom_tick()
{
    // Xom semi-randomly drifts your piety.
    int delta;
    const char *origfavour = describe_xom_favour();
    const bool good = you.piety > (MAX_PIETY / 2);
    int size = abs(you.piety - 100);
    delta = (x_chance_in_y(511, 1000) ? 1 : -1);
    size += delta;
    you.piety = (MAX_PIETY / 2) + (good ? size : -size);
    const char *newfavour = describe_xom_favour();
    if (strcmp(origfavour, newfavour))
    {
        // Dampen oscillation across announcement boundaries.
        size += delta * 8;
        you.piety = (MAX_PIETY / 2) + (good ? size : -size);
    }

    // ...but he gets bored...
    if (one_chance_in(2))
        you.gift_timeout--;

    newfavour = describe_xom_favour();

    if (strcmp(origfavour, newfavour)) {
        char buf[8192];
        strcpy(buf, "Your title is now: ");
        strcat(buf, newfavour);
        god_speaks( you.religion, buf );
    }

    if (you.gift_timeout == 1)
        simple_god_message(" is getting BORED.");

    if (one_chance_in(20))
        xom_acts(abs(you.piety - 100));
}

void xom_is_stimulated(int maxinterestingness, const std::string& message,
                       bool force_message)
{
    const char *message_array[6];

    for (int i = 0; i < 6; ++i)
        message_array[i] = message.c_str();

    _xom_is_stimulated(maxinterestingness, message_array, force_message);
}

static void _xom_makes_you_cast_random_spell(int sever)
{
    int spellenum = sever;

    god_acting gdact(GOD_XOM);

    const int nxomspells = ARRAYSZ(_xom_spells);
    spellenum = std::min(nxomspells, spellenum);

    const spell_type spell = _xom_spells[random2(spellenum)];

    god_speaks(GOD_XOM, _get_xom_speech("spell effect").c_str());

#if DEBUG_DIAGNOSTICS || DEBUG_RELIGION || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS,
         "_xom_makes_you_cast_random_spell(); spell: %d, spellenum: %d",
         spell, spellenum);
#endif

    your_spells(spell, sever, false);
}

static void _try_brand_switch(const int item_index)
{
    if (item_index == NON_ITEM)
        return;

    item_def &item(mitm[item_index]);

    // Only apply to melee weapons for the player.
    if (item.base_type != OBJ_WEAPONS || is_range_weapon(item))
        return;

    if (is_unrandom_artefact(item) || is_fixed_artefact(item))
        return;

    // Only do it some of the time.
    if (one_chance_in(5))
        return;

    int brand;
    if (item.base_type == OBJ_WEAPONS)
    {
        // Only switch already branded items.
        if (get_weapon_brand(item) == SPWPN_NORMAL)
           return;

        brand = (int) SPWPN_CHAOS;
    }
    else
    {
        // Only switch already branded items.
        if (get_ammo_brand(item) == SPWPN_NORMAL)
           return;

        brand = (int) SPMSL_CHAOS;
    }

    if (is_random_artefact(item))
        randart_set_property(item, RAP_BRAND, brand);
    else
        item.special = brand;
}

static void _xom_make_item(object_class_type base, int subtype, int power)
{
    god_acting gdact(GOD_XOM);

    int thing_created =
        items(true, base, subtype, true, power, MAKE_ITEM_RANDOM_RACE,
              0, 0, GOD_XOM);

    if (thing_created == NON_ITEM)
    {
        god_speaks(GOD_XOM, "\"No, never mind.\"");
        return;
    }

    _try_brand_switch(thing_created);

    move_item_to_grid(&thing_created, you.pos());
    mitm[thing_created].inscription = "god gift";
    canned_msg(MSG_SOMETHING_APPEARS);
    stop_running();
}

static void _xom_acquirement(object_class_type force_class)
{
    god_acting gdact(GOD_XOM);

    int item_index = NON_ITEM;

    if (!acquirement(force_class, GOD_XOM, false, &item_index)
        || item_index == NON_ITEM)
    {
       god_speaks(GOD_XOM, "\"No, never mind.\"");
       return;
    }

    _try_brand_switch(item_index);

    stop_running();
}

static object_class_type _get_unrelated_wield_class(object_class_type ref)
{
    object_class_type objtype = OBJ_WEAPONS;
    if (ref == OBJ_WEAPONS)
    {
        if (one_chance_in(10))
            objtype = OBJ_MISCELLANY;
        else
            objtype = OBJ_STAVES;
    }
    else if (ref == OBJ_STAVES)
    {
        if (one_chance_in(10))
            objtype = OBJ_MISCELLANY;
        else
            objtype = OBJ_WEAPONS;
    }
    else
    {
        const int temp_rand = random2(3);
        objtype = (temp_rand == 0) ? OBJ_WEAPONS :
                  (temp_rand == 1) ? OBJ_STAVES
                                   : OBJ_MISCELLANY;
    }

    return (objtype);
}

static bool _xom_annoyance_gift(int power)
{
    god_acting gdact(GOD_XOM);

    if (coinflip() && player_in_a_dangerous_place())
    {
        const item_def *weapon = you.weapon();

        // Xom has a sense of humour.
        if (coinflip() && weapon && weapon->cursed())
        {
            // If you are wielding a cursed item then Xom will give you
            // an item of that same type.  Ha ha!
            god_speaks(GOD_XOM, _get_xom_speech("cursed gift").c_str());
            if (coinflip())
                // For added humour, give the same sub-type.
                _xom_make_item(weapon->base_type, weapon->sub_type, power * 3);
            else
                _xom_acquirement(weapon->base_type);
            return (true);
        }

        const item_def *gloves = you.slot_item(EQ_GLOVES);
        if (coinflip() && gloves && gloves->cursed())
        {
            // If you are wearing cursed gloves, then Xom will give you
            // a ring.  Ha ha!
            god_speaks(GOD_XOM, _get_xom_speech("cursed gift").c_str());
            _xom_make_item(OBJ_JEWELLERY, get_random_ring_type(), power * 3);
            return (true);
        };

        const item_def *amulet = you.slot_item(EQ_AMULET);
        if (coinflip() && amulet && amulet->cursed())
        {
            // If you are wearing a cursed amulet, then Xom will give
            // you an amulet.  Ha ha!
            god_speaks(GOD_XOM, _get_xom_speech("cursed gift").c_str());
            _xom_make_item(OBJ_JEWELLERY, get_random_amulet_type(), power * 3);
            return (true);
        };

        const item_def *left_ring = you.slot_item(EQ_LEFT_RING);
        const item_def *right_ring = you.slot_item(EQ_RIGHT_RING);
        if (coinflip() && ((left_ring && left_ring->cursed())
                           || (right_ring && right_ring->cursed())))
        {
            // If you are wearing a cursed ring, then Xom will give you
            // a ring.  Ha ha!
            god_speaks(GOD_XOM, _get_xom_speech("ring gift").c_str());
            _xom_make_item(OBJ_JEWELLERY, get_random_ring_type(), power * 3);
            return (true);
        }

        if (one_chance_in(5) && weapon)
        {
            // Xom will give you a wielded item of a type different from
            // what you are currently wielding.
            god_speaks(GOD_XOM, _get_xom_speech("weapon gift").c_str());

            const object_class_type objtype =
                _get_unrelated_wield_class(weapon->base_type);

            if (x_chance_in_y(power, 256))
                _xom_acquirement(objtype);
            else
                _xom_make_item(objtype, OBJ_RANDOM, power * 3);
            return (true);
        }
    }

    return (false);
}

static bool _xom_gives_item(int power)
{
    if (_xom_annoyance_gift(power))
        return (true);

    const item_def *cloak = you.slot_item(EQ_CLOAK);
    if (coinflip() && cloak && cloak->cursed())
    {
        // If you are wearing a cursed cloak, then Xom will give you a
        // cloak or body armour.  Ha ha!
        god_speaks(GOD_XOM, _get_xom_speech("armour gift").c_str());
        _xom_make_item(OBJ_ARMOUR,
                       one_chance_in(10) ? ARM_CLOAK :
                                get_random_body_armour_type(power * 2),
                       power * 3);
        return (true);
    }

    god_speaks(GOD_XOM, _get_xom_speech("general gift").c_str());

    // There are two kinds of Xom gifts: acquirement and random object.
    // The result from acquirement is very good (usually as good or
    // better than random object), and it is sometimes tuned to the
    // player's skills and nature.  Being tuned to the player's skills
    // and nature is not very Xomlike...
    if (x_chance_in_y(power, 256))
    {
        // Random-type acquirement.
        const int r = random2(7);
        const object_class_type objtype = (r == 0) ? OBJ_WEAPONS :
                                          (r == 1) ? OBJ_ARMOUR :
                                          (r == 2) ? OBJ_JEWELLERY :
                                          (r == 3) ? OBJ_BOOKS :
                                          (r == 4) ? OBJ_STAVES :
                                          (r == 5) ? OBJ_FOOD :
                                          (r == 6) ? OBJ_MISCELLANY
                                                   : OBJ_GOLD;

        god_acting gdact(GOD_XOM);

        _xom_acquirement(objtype);
    }
    else
    {
        // Random-type random object.
        _xom_make_item(OBJ_RANDOM, OBJ_RANDOM, power * 3);
    }

    more();

    return (true);
}

static bool _choose_mutatable_monster(const monsters* mon)
{
    return (mon->alive() && mon->can_safely_mutate()
            && !mons_is_submerged(mon));
}

static bool _is_chaos_upgradeable(const item_def &item,
                                  const monsters* mon)
{
    // Since Xom is a god he is capable of changing randarts, but not
    // other artifacts.
    if (is_artefact(item) && !is_random_artefact(item))
       return (false);

    // Only upgrade permanent items, since the player should get a
    // chance to use the item if s/he can defeat the monster.
    if (item.flags & ISFLAG_SUMMONED)
        return (false);

    // Don't know how to downgrade blessed blades to normal blades.
    // Can be justified as good gods protecting blessed blades.
    if (is_blessed_blade(item))
        return (false);

    // God gifts from good gods are protected.  Also, Beogh hates all
    // the other gods so he'll protect his gifts as well.
    if (item.orig_monnum < 0)
    {
        god_type iorig = static_cast<god_type>(-item.orig_monnum);
        if ((iorig > GOD_NO_GOD && iorig < NUM_GODS)
            && (is_good_god(iorig) || iorig == GOD_BEOGH))
        {
            return (false);
        }
    }

    // Leave branded items alone, since this is supposed to be an
    // upgrade.
    if (item.base_type == OBJ_MISSILES)
    {
        // Don't make boulders or throwing nets of chaos.
        if (item.sub_type == MI_LARGE_ROCK
            || item.sub_type == MI_THROWING_NET)
        {
            return (false);
        }

        if (get_ammo_brand(item) == SPMSL_NORMAL)
            return (true);
    }
    else
    {
        // If the weapon is a launcher and the monster is either out
        // of ammo or is carrying javelins then don't bother upgrading
        // launcher.
        if (is_range_weapon(item)
            && (mon->inv[MSLOT_MISSILE] == NON_ITEM
                || !has_launcher(mitm[mon->inv[MSLOT_MISSILE]])))
        {
            return (false);
        }
        if (get_weapon_brand(item) == SPWPN_NORMAL)
            return (true);
    }

    return (false);
}

static bool _choose_chaos_upgrade(const monsters* mon)
{
    // Only choose monsters that will attack.
    if (!mon->alive() || mons_attitude(mon) != ATT_HOSTILE
        || mons_is_fleeing(mon) || mons_is_panicking(mon))
    {
       return (false);
    }

    if (mons_itemuse(mon) < MONUSE_STARTING_EQUIPMENT)
        return (false);

    // Holy beings are presumably protected by another god, unless they're
    // gifts from Xom.
    if (mons_is_holy(mon) && mon->god != GOD_XOM)
        return (false);

    // God gifts from good gods will be protected by their god from being
    // given chaos weapons, while other gods won't mind the help in their
    // servants killing the player.
    if (mon->god != GOD_NO_GOD && is_good_god(mon->god))
       return (false);

    // Beogh presumably doesn't want Xom messing with his orcs, even if
    // it would give them a better weapon.
    if (mons_genus(mon->type) == MONS_ORC)
        return (false);

    mon_inv_type slots[] = {MSLOT_WEAPON, MSLOT_ALT_WEAPON, MSLOT_MISSILE};

    // NOTE: Code assumes that the monster will only be carrying one
    // missile launcher at a time.
    bool special_launcher = false;
    for (int i = 0; i < 3; ++i)
    {
        const mon_inv_type slot = slots[i];
        const int          midx = mon->inv[slot];

        if (midx == NON_ITEM)
            continue;
        const item_def &item(mitm[midx]);

        // Monster already has a chaos weapon, give upgrade to a different
        // monster.
        if (is_chaotic_item(item))
            return (false);

        if (_is_chaos_upgradeable(item, mon))
        {
            if (item.base_type != OBJ_MISSILES)
                return (true);

            // If for some weird reason a monster is carrying a bow
            // and javelins then branding the javelins is okay since
            // they won't be fired by the bow.
            if (!special_launcher || !has_launcher(item))
                return (true);
        }

        if (is_range_weapon(item))
        {
            // If the launcher alters its ammo then branding the monster's
            // ammo won't be an upgrade.
            int brand = get_weapon_brand(item);
            if (brand == SPWPN_FLAME || brand == SPWPN_FROST
                || brand == SPWPN_VENOM)
            {
                special_launcher = true;
            }
        }
    }

    return (false);
}

static void _do_chaos_upgrade(item_def &item, const monsters* mon)
{
    ASSERT(item.base_type == OBJ_MISSILES
           || item.base_type == OBJ_WEAPONS);
    ASSERT(!is_unrandom_artefact(item) && !is_fixed_artefact(item));

    bool seen = false;
    if (mon && you.can_see(mon) && item.base_type == OBJ_WEAPONS)
    {
        seen = true;

        description_level_type desc = mons_friendly(mon) ? DESC_CAP_YOUR :
                                                           DESC_CAP_THE;
        std::string msg = mon->name(desc);
        msg += "'s ";
        msg = replace_all(msg, "s's", "s'"); // Proper posessive.

        msg += item.name(DESC_PLAIN, false, false, false);

        msg += " is briefly surrounded by a scintillating aura of "
               "random colours.";

        mpr(msg.c_str());
    }

    const int brand = (item.base_type == OBJ_WEAPONS) ? (int) SPWPN_CHAOS :
                                                        (int) SPMSL_CHAOS;

    if (is_random_artefact(item))
    {
        randart_set_property(item, RAP_BRAND, brand);
        if (seen)
            randart_wpn_learn_prop(item, RAP_BRAND);
    }
    else
    {
        item.special = brand;
        if (seen)
            set_ident_flags(item, ISFLAG_KNOW_TYPE);

        // Make sure it's visibly special
        if (!(item.flags & ISFLAG_COSMETIC_MASK))
            item.flags |= ISFLAG_GLOWING;

        // Make the pluses more like a randomly generated ego item
        item.plus  += random2(5);
        item.plus2 += random2(5);
    }
}

static monster_type _xom_random_demon(int sever, bool use_greater_demons = true)
{
    const int roll = random2(1000 - (MAX_PIETY - sever) * 3);
#ifdef DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS, "_xom_random_demon(); sever = %d, roll: %d",
         sever, roll);
#endif
    const demon_class_type dct =
        (roll >= 850) ? DEMON_GREATER :
        (roll >= 340) ? DEMON_COMMON
                      : DEMON_LESSER;

    monster_type demon = MONS_PROGRAM_BUG;

    // Sometimes, send a holy warrior instead.
    if (dct == DEMON_GREATER && coinflip())
        demon = summon_any_holy_being(HOLY_BEING_WARRIOR);
    else
    {
        const demon_class_type dct2 =
            (!use_greater_demons && dct == DEMON_GREATER) ? DEMON_COMMON : dct;

        if (dct2 == DEMON_COMMON && one_chance_in(10))
            demon = MONS_CHAOS_SPAWN;
        else
            demon = summon_any_demon(dct2);
    }

    return (demon);
}

// The nicer stuff.  Note: these things are not necessarily nice.
static bool _xom_is_good(int sever, int tension)
{
    bool done = false;

    // Did Xom (already) kill the player?
    if (you.hp <= 0)
        return (true);

    god_acting gdact(GOD_XOM);

    // This series of random calls produces a poisson-looking
    // distribution: initial hump, plus a long-ish tail.

    // Don't make player berserk if there's no danger.
    if (tension > 0 && x_chance_in_y(2, sever))
    {
        potion_type pot =
            static_cast<potion_type>(
                random_choose(POT_HEALING, POT_HEAL_WOUNDS, POT_SPEED,
                              POT_MIGHT, POT_INVISIBILITY, POT_BERSERK_RAGE,
                              POT_EXPERIENCE, -1));

        if (pot == POT_EXPERIENCE && !one_chance_in(6))
            pot = POT_BERSERK_RAGE;

        if (pot != POT_BERSERK_RAGE || you.can_go_berserk(false))
        {
            god_speaks(GOD_XOM, _get_xom_speech("potion effect").c_str());

            if (pot == POT_BERSERK_RAGE)
                you.berserk_penalty = NO_BERSERK_PENALTY;

            potion_effect(pot, 150);

            done = true;
        }
    }
    else if (x_chance_in_y(3, sever))
    {
        _xom_makes_you_cast_random_spell(sever);

        done = true;
    }
    else if (x_chance_in_y(4, sever))
    {
        monsters *monster;
        for (unsigned i = 0; i < MAX_MONSTERS; ++i)
        {
            monster = &menv[i];

            if (monster->type == -1 || !mons_near(monster) || mons_wont_attack(monster) || one_chance_in(20))
                continue;

            if (monster->add_ench(mon_enchant(ENCH_CONFUSION, 0, KC_FRIENDLY, random2(sever))))
            {
                if (!done)
                    god_speaks(GOD_XOM, _get_xom_speech("confusion").c_str());
                done = true;
                if (player_monster_visible( monster ))
                    simple_monster_message(monster, " looks rather confused.");
            }
        }
    }
    // Pointless to send in help if there's no danger.
    else if (tension > 0 && x_chance_in_y(5, sever))
    {
        // XXX: Can we clean up this ugliness, please?
        const int numdemons =
            std::min(random2(random2(random2(sever+1)+1)+1)+2, 16);
        int numdifferent = 0;

        // If we have a mix of demons and non-demons, there's a chance
        // that one or both of the factions may be hostile.
        int hostile = random2(12);
        int hostiletype =
            (hostile <  3) ? 0 :                  //  1/4: both are friendly
            (hostile < 11) ? (coinflip() ? 1 : 2) //  2/3: one is hostile
                           : 3;                   // 1/12: both are hostile

        bool *is_demonic = new bool[numdemons];
        int *summons = new int[numdemons];

        bool success = false;

        for (int i = 0; i < numdemons; ++i)
        {
            monster_type monster = _xom_random_demon(sever);

            is_demonic[i] = (mons_class_holiness(monster) == MH_DEMONIC);

            // If it's not a demon, Xom got it someplace else, so use
            // different messages below.
            if (!is_demonic[i])
                numdifferent++;

            summons[i] =
                create_monster(
                    mgen_data(monster, BEH_FRIENDLY, 3, MON_SUMM_AID,
                        you.pos(), you.pet_target, MG_FORCE_BEH, GOD_XOM));

            if (summons[i] != -1)
                success = true;
        }

        if (success)
        {
            if (numdifferent == numdemons)
                god_speaks(GOD_XOM, _get_xom_speech("multiple holy summons").c_str());
            else if (numdifferent > 0)
                god_speaks(GOD_XOM, _get_xom_speech("multiple mixed summons").c_str());
            else
                god_speaks(GOD_XOM, _get_xom_speech("multiple summons").c_str());

            // If we have only non-demons, there's a chance that they
            // may be hostile.
            if (numdifferent == numdemons && one_chance_in(4))
                hostiletype = 2;
            // If we have only demons, they'll always be friendly.
            else if (numdifferent == 0)
                hostiletype = 0;

            for (int i = 0; i < numdemons; ++i)
            {
                monsters *mon = &menv[i];

                if (hostiletype != 0)
                {
                    // Mark factions hostile as appropriate.
                    if (hostiletype == 3
                        || (is_demonic[i] && hostiletype == 1)
                        || (!is_demonic[i] && hostiletype == 2))
                    {
                        mon->attitude = ATT_HOSTILE;
                        behaviour_event(mon, ME_ALERT, MHITYOU);
                    }
                }

                player_angers_monster(mon);
            }

            done = true;
        }

        delete[] is_demonic;
        delete[] summons;
    }
    else if (x_chance_in_y(6, sever))
    {
        _xom_gives_item(sever);

        done = true;
    }
    else if (x_chance_in_y(7, sever))
    {
        monster_type mon = _xom_random_demon(sever);
        const bool is_demonic = (mons_class_holiness(mon) == MH_DEMONIC);

        // If we have a non-demon, Xom got it someplace else, so use
        // different messages below.
        bool different = !is_demonic;

        beh_type beha = BEH_FRIENDLY;
        unsigned short hitting = you.pet_target;

        // There's a chance that a non-demon may be hostile.
        if (different && one_chance_in(4))
        {
            beha = BEH_HOSTILE;
            hitting = MHITYOU;
        }

        const int summons =
            create_monster(
                mgen_data(mon, beha, 6, MON_SUMM_AID,
                    you.pos(), hitting, MG_FORCE_BEH, GOD_XOM));

        if (summons != -1)
        {
            if (different)
                god_speaks(GOD_XOM, _get_xom_speech("single holy summon").c_str());
            else
                god_speaks(GOD_XOM, _get_xom_speech("single summon").c_str());

            player_angers_monster(&menv[summons]);

            done = true;
        }
    }
    else if (x_chance_in_y(8, sever))
    {
        if (there_are_monsters_nearby(false, false))
        {
            monsters *mon =
                choose_random_nearby_monster(0, _choose_mutatable_monster);

            if (mon)
            {
                god_speaks(GOD_XOM,
                           _get_xom_speech("good monster polymorph").c_str());

                monster_polymorph(mon, RANDOM_MONSTER,
                    mons_wont_attack(mon) ? PPT_MORE : PPT_LESS, true);

                if (one_chance_in(8) && !mons_is_shapeshifter(mon))
                {
                    mon->add_ench(one_chance_in(3) ?
                        ENCH_GLOWING_SHAPESHIFTER : ENCH_SHAPESHIFTER);
                }

                // player_angers_monster() will turn the monster against you
                // only if the monster hates your religion.  No monsters hate
                // Xom-religion, so this will only have an effect if you are not
                // currently a worshipper of Xom, e.g. if you just abandoned him
                // or if you drew a Card of Xom or something.
                player_angers_monster(mon);

                done = true;
            }
        }
    }
    else if (x_chance_in_y(9, sever))
    {
        _xom_gives_item(sever);

        done = true;
    }
    else if (x_chance_in_y(10, sever) && (you.level_type != LEVEL_ABYSS))
    {
        // rearrange the pieces -- blink every monster on this level and the player

        // Every now and then, Xom also confuses them all.
        bool confusem = one_chance_in(10);
	
        // Not just every monster in sight -- oh no.  Every monster on this level!
        monsters *monster;
        for (unsigned i = 0; i < MAX_MONSTERS; ++i)
        {
            monster = &menv[i];

            if (monster->type == -1)
                continue;

            if (monster_blink(monster))
            {
                if (!done)
                    god_speaks(GOD_XOM, _get_xom_speech("rearrange the pieces").c_str());
                done = true;
                if (confusem)
                    if (monster->add_ench(mon_enchant(ENCH_CONFUSION, 0, KC_FRIENDLY, random2(sever))))
                        if (player_monster_visible( monster ))
                            simple_monster_message(monster, " looks rather confused.");
            }
            // If he blinked at least one monster, blink the player too and this act is considered "done".
            if (done)
                random_blink(false);
        }
    }
    else if (x_chance_in_y(11, sever) && (you.level_type != LEVEL_ABYSS))
    {
        // The Xom teleportation train takes you on instant teleportation to
        // a few random areas, stopping randomly but mostly likely in an area
        // that is not dangerous to you.
        god_speaks(GOD_XOM, _get_xom_speech("teleportation journey").c_str());
        do
        {
            you_teleport_now(false);
            more();
            if (one_chance_in(10))
                break;
        }
        while (x_chance_in_y(3, 4) || player_in_a_dangerous_place());
        done = true;
    }
    else if (x_chance_in_y(12, sever))
    {
        // This can fail with radius 1, or in open areas.
        if (vitrify_area(random2avg(sever / 2, 3) + 1))
        {
            god_speaks(GOD_XOM, _get_xom_speech("vitrification").c_str());

            done = true;
        }
    }
    else if (x_chance_in_y(13, sever) && x_chance_in_y(16, how_mutated()))
    {
        if (you.can_safely_mutate()
            && player_mutation_level(MUT_MUTATION_RESISTANCE) < 3)
        {
            god_speaks(GOD_XOM, _get_xom_speech("good mutations").c_str());

            mpr("Your body is suffused with distortional energy.");

            set_hp(1 + random2(you.hp), false);
            deflate_hp(you.hp_max / 2, true);

            bool failMsg = true;

            for (int i = random2(4); i >= 0; --i)
            {
                if (mutate(RANDOM_GOOD_MUTATION, failMsg, false, true, false,
                           false, true))
                {
                    done = true;
                }
                else
                    failMsg = false;
            }
        }
    }
    // Pointless to send in help if there's no danger.
    else if (tension > 0 && x_chance_in_y(14, sever))
    {
        monster_type mon = _xom_random_demon(sever);
        const bool is_demonic = (mons_class_holiness(mon) == MH_DEMONIC);

        // If we have a non-demon, Xom got it someplace else, so use
        // different messages below.
        bool different = !is_demonic;

        beh_type beha = BEH_FRIENDLY;
        unsigned short hitting = you.pet_target;

        // There's a chance that a non-demon may be hostile.
        if (different && one_chance_in(4))
        {
            beha = BEH_HOSTILE;
            hitting = MHITYOU;
        }

        const int summons =
            create_monster(
                mgen_data(_xom_random_demon(sever, one_chance_in(8)), beha, 0,
                    you.pos(), hitting, MG_FORCE_BEH, GOD_XOM));

        if (summons != -1)
        {
            if (different)
                god_speaks(GOD_XOM, _get_xom_speech("single major holy summon").c_str());
            else
                god_speaks(GOD_XOM, _get_xom_speech("single major demon summon").c_str());

            player_angers_monster(&menv[summons]);

            done = true;
        }
    }
    else if (x_chance_in_y(15, sever))
    {
        if (player_in_a_dangerous_place())
        {
            if (you.hp <= random2(201))
                you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION] = 1;

            god_speaks(GOD_XOM, "The area is suffused with divine lightning!");

            bolt beam;
            beam.beam_source  = NON_MONSTER;
            beam.type         = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage       = dice_def(3, 30);
            beam.flavour      = BEAM_ELECTRICITY;
            beam.target       = you.pos();
            beam.name         = "blast of lightning";
            beam.colour       = LIGHTCYAN;
            beam.thrower      = KILL_MISC;
            beam.aux_source   = "Xom's lightning strike";
            beam.ex_size      = 2;
            beam.is_tracer    = false;
            beam.is_explosion = true;
            explosion(beam);

            if (you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION])
            {
                mpr("Your divine protection wanes.");
                you.attribute[ATTR_DIVINE_LIGHTNING_PROTECTION] = 0;
            }

            // Don't accidentally kill the player when doing a good
            // act.
            you.hp  = std::max(1, you.hp);

            done = true;
        }
    }

    return (done);
}

static bool _xom_is_bad(int sever, int tension)
{
    bool done = false;

    // Xom will only directly kill you with a bad effect if you're under
    // penance from him or he's bored.
    const bool nasty = you.penance[GOD_XOM]
                       || (you.religion == GOD_XOM && you.gift_timeout == 0);

    // If not being nasty then prevent spell miscasts from killing the
    // player.
    int lethality_margin;
    if (nasty)
        lethality_margin = 0;
    else
        lethality_margin = random_range(1, 4);

    god_acting gdact(GOD_XOM);

    while (!done)
    {
        // Did Xom kill the player?
        if (you.hp <= 0)
            return (true);

        if (x_chance_in_y(3, sever))
        {
            god_speaks(GOD_XOM, _get_xom_speech("zero miscast effect").c_str());

            MiscastEffect(&you, -GOD_XOM, SPTYP_RANDOM, 0,
                          "the mischief of Xom");

            done = true;
        }
        else if (x_chance_in_y(4, sever))
        {
            god_speaks(GOD_XOM, _get_xom_speech("minor miscast effect").c_str());

            MiscastEffect(&you, -GOD_XOM, SPTYP_RANDOM, random2(2),
                          "the capriciousness of Xom", NH_DEFAULT,
                          lethality_margin);

            done = true;
        }
        else if (x_chance_in_y(5, sever))
        {
            stat_type stat = STAT_RANDOM;
            int       max  = 3;

            // Don't kill the player unless Xom is being nasty.
            if (!nasty)
            {
                // Make sure not to lower strength so much that the player
                // will die once might wears off.
                char      vals[3] =
                    {you.strength - (you.duration[DUR_MIGHT] ? 5 : 0),
                     you.dex, you.intel};
                stat_type types[3] = {STAT_STRENGTH, STAT_DEXTERITY,
                                      STAT_INTELLIGENCE};

                int count = 0;
                for (int i = 0; i < 3; ++i)
                {
                    int val = vals[i];

                    if (val > 1 && one_chance_in(++count))
                    {
                        stat = types[i];
                        max  = val - 1;
                    }
                }
                if (count == 0)
                    continue;
            }

            god_speaks(GOD_XOM, _get_xom_speech("lose stats").c_str());
            lose_stat(stat, 1 + random2(max), true, "the vengeance of Xom" );

            done = true;
        }
        else if (x_chance_in_y(6, sever))
        {
            god_speaks(GOD_XOM, _get_xom_speech("medium miscast effect").c_str());

            MiscastEffect(&you, -GOD_XOM, SPTYP_RANDOM, random2(3),
                          "the capriciousness of Xom", NH_DEFAULT,
                          lethality_margin);

            done = true;
        }
        else if (x_chance_in_y(7, sever) && (you.level_type != LEVEL_ABYSS))
        {
            // The Xom teleportation train takes you on instant teleportation to
            // a few random areas, stopping if an area is dangerous to you or
            // randomly stopping.
            god_speaks(GOD_XOM, _get_xom_speech("teleportation journey").c_str());
            do
            {
                you_teleport_now(false);
                more();
            }
            while (x_chance_in_y(3, 4) && !player_in_a_dangerous_place());
            done = true;
        }
        else if (x_chance_in_y(8, sever))
        {
            monsters *mon =
                choose_random_nearby_monster(0, _choose_chaos_upgrade);

            if (!mon)
                continue;

            god_speaks(GOD_XOM, _get_xom_speech("chaos upgrade").c_str());

            mon_inv_type slots[] = {MSLOT_WEAPON, MSLOT_ALT_WEAPON,
                                    MSLOT_MISSILE};
            for (int i = 0; i < 3; ++i)
            {
                int idx = mon->inv[slots[i]];
                if (idx == NON_ITEM)
                    continue;

                item_def &item(mitm[idx]);
                if (!_is_chaos_upgradeable(item, mon))
                    continue;

                _do_chaos_upgrade(item, mon);
                done = true;
                break;
            }
            ASSERT(done);

            // Wake the monster up.
            behaviour_event( mon, ME_ALERT, MHITYOU );
        }
        else if (x_chance_in_y(9, sever))
        {
            if (you.can_safely_mutate()
                && player_mutation_level(MUT_MUTATION_RESISTANCE) < 3)
            {
                god_speaks(GOD_XOM, _get_xom_speech("random mutations").c_str());

                mpr("Your body is suffused with distortional energy.");

                set_hp(1 + random2(you.hp), false);
                deflate_hp(you.hp_max / 2, true);

                bool failMsg = true;

                for (int i = random2(4); i >= 0; --i)
                {
                    if (mutate(RANDOM_XOM_MUTATION, failMsg, false, true,
                               false, false, !nasty))
                    {
                        done = true;
                    }
                    else
                        failMsg = false;
                }
            }
        }
        else if (x_chance_in_y(10, sever))
        {
            if (there_are_monsters_nearby(false, false))
            {
                monsters *mon =
                    choose_random_nearby_monster(0, _choose_mutatable_monster);

                if (mon)
                {
                    god_speaks(GOD_XOM, _get_xom_speech("bad monster polymorph").c_str());

                    monster_polymorph(mon, RANDOM_MONSTER,
                        mons_wont_attack(mon) ? PPT_LESS : PPT_MORE, true);

                    if (one_chance_in(8) && !mons_is_shapeshifter(mon))
                    {
                        mon->add_ench(one_chance_in(3) ?
                            ENCH_GLOWING_SHAPESHIFTER : ENCH_SHAPESHIFTER);
                    }

                    player_angers_monster(mon);

                    done = true;
                }
            }
        }
        // Pointless to confuse player if there's no danger nearby.
        else if (tension > 0 && x_chance_in_y(11, sever))
        {
            std::string speech = _get_xom_speech("confusion");
            if (confuse_player(random2(sever)+1, false)) {
                done = true;
                // Well, sometimes Xom gets carried away and starts confusing other
                // creatures too.
                if (coinflip()) {
                    monsters* monster;
                    for (unsigned i = 0; i < MAX_MONSTERS; ++i)
                    {
                        monster = &menv[i];

                        if (monster->type == -1 || !mons_near(monster) || one_chance_in(20))
                            continue;

                        if (monster->add_ench(mon_enchant(ENCH_CONFUSION, 0, KC_FRIENDLY, random2(sever)))) {
                            if (player_monster_visible( monster ))
                                simple_monster_message(monster, " looks rather confused.");
                        }
                    }
                }
            }
        }
        else if (x_chance_in_y(12, sever))
        {
            std::string speech = _get_xom_speech("draining or torment");

            if (one_chance_in(4))
            {
                if (player_prot_life() < 3 && (nasty || you.experience > 0))
                {
                    god_speaks(GOD_XOM, speech.c_str());

                    drain_exp();
                    if (random2(sever) > 3 && (nasty || you.experience > 0))
                        drain_exp();
                    if (random2(sever) > 3 && (nasty || you.experience > 0))
                        drain_exp();

                    done = true;
                }
            }
            else
            {
                if (!player_res_torment())
                {
                    god_speaks(GOD_XOM, speech.c_str());

                    torment_player(0, TORMENT_XOM);

                    done = true;
                }
            }
        }
        else if (x_chance_in_y(13, sever))
        {
            std::string speech = _get_xom_speech("hostile monster");

            // Nasty, but fun.
            if (one_chance_in(4))
            {
                god_speaks(GOD_XOM, speech.c_str());

                cast_tukimas_dance(100, GOD_XOM, true);

                done = true;
            }
            else
            {
                // XXX: Can we clean up this ugliness, please?
                const int numdemons =
                    std::min(random2(random2(random2(sever+1)+1)+1)+1, 14);

                bool success = false;

                for (int i = 0; i < numdemons; ++i)
                {
                    if (create_monster(
                            mgen_data::hostile_at(
                                _xom_random_demon(sever),
                                you.pos(), 4, 0, true, GOD_XOM,
                                MON_SUMM_WRATH)) != -1)
                    {
                        success = true;
                    }
                }

                if (success)
                {
                    god_speaks(GOD_XOM, speech.c_str());

                    done = true;
                }
            }
        }
        else if (x_chance_in_y(14, sever))
        {
            god_speaks(GOD_XOM, _get_xom_speech("major miscast effect").c_str());

            MiscastEffect(&you, -GOD_XOM, SPTYP_RANDOM, random2(4),
                          "the severe capriciousness of Xom", NH_DEFAULT,
                          lethality_margin);

            done = true;
        }
        else if (one_chance_in(sever))
        {
            if (you.level_type != LEVEL_ABYSS)
            {
                god_speaks(GOD_XOM, _get_xom_speech("banishment").c_str());

                banished(DNGN_ENTER_ABYSS, "Xom");

                done = true;
            }
        }
    }

    return (done);
}

void xom_acts(bool niceness, int sever)
{
#if DEBUG_DIAGNOSTICS || DEBUG_RELIGION || DEBUG_XOM
    mprf(MSGCH_DIAGNOSTICS, "xom_acts(%u, %d); piety: %u, interest: %u\n",
         niceness, sever, you.piety, you.gift_timeout);
#endif

    entry_cause_type old_entry_cause = you.entry_cause;

    sever = std::max(1, sever);

    god_type which_god = GOD_XOM;
    // Drawing the Xom card from Nemelex's decks of oddities or punishment.
    if (crawl_state.is_god_acting()
        && crawl_state.which_god_acting() != GOD_XOM)
    {
        which_god = crawl_state.which_god_acting();

        if (crawl_state.is_god_retribution())
        {
            niceness = false;
            simple_god_message(" asks Xom for help in punishing you, and "
                       "Xom happily agrees.", which_god);
        }
        else
        {
            niceness = true;
            simple_god_message(" calls in a favour from Xom.", which_god);
        }
    }

    const int tension = get_tension(which_god);

#if DEBUG_RELIGION || DEBUG_XOM || DEBUG_TENSION
    mprf(MSGCH_DIAGNOSTICS, "xom tension: %d", tension);
#endif

    const dungeon_feature_type orig_feat = grd(you.pos());

    const int orig_hp = you.hp;

          char* stat_ptrs[3]  = {&you.strength, &you.intel, &you.dex};
    const char  orig_stats[3] = {*(stat_ptrs[0]), *(stat_ptrs[1]),
                                 *(stat_ptrs[2])};

    const FixedVector<unsigned char, NUM_MUTATIONS> orig_mutation
        = you.mutation;

    if (niceness && !one_chance_in(5))
    {
        // Good stuff.
        while (!_xom_is_good(sever, tension))
            ;
    }
    else
    {
        // Bad mojo.
        while (!_xom_is_bad(sever, tension))
            ;
    }

    bool already_oopsed = false;
    if (you.hp <= 0 || you.strength <= 0 || you.dex <= 0 || you.intel <= 0)
    {
        // ouch() returned early because the player died from the Xom effect
        // even though neither is the player under penance nor is Xom bored.
        mpr("You die...");
        god_speaks(GOD_XOM, _get_xom_speech("accidental homicide").c_str());
        god_speaks(GOD_XOM, _get_xom_speech("resurrection").c_str());

        already_oopsed = true;

        if (you.hp <= 0)
            you.hp = orig_hp;

        mutation_type dex_muts[5] = {MUT_GREY2_SCALES, MUT_METALLIC_SCALES,
                                     MUT_YELLOW_SCALES, MUT_RED2_SCALES,
                                     MUT_STRONG_STIFF};

        for (int i = 0; i < 5; ++i)
        {
            mutation_type bad = dex_muts[i];

            while (you.dex <= 0 && you.mutation[bad] > orig_mutation[bad])
                delete_mutation(bad, true, true);
        }
        while(you.dex <= 0
              && you.mutation[MUT_FLEXIBLE_WEAK] <
                     orig_mutation[MUT_FLEXIBLE_WEAK])
        {
            mutate(MUT_FLEXIBLE_WEAK, true, true, true);
        }

        while (you.strength <= 0
               && you.mutation[MUT_FLEXIBLE_WEAK] >
                      orig_mutation[MUT_FLEXIBLE_WEAK])
        {
            delete_mutation(MUT_FLEXIBLE_WEAK, true, true);
        }
        while (you.strength <= 0
               && you.mutation[MUT_STRONG_STIFF] <
                      orig_mutation[MUT_STRONG_STIFF])
        {
            mutate(MUT_STRONG_STIFF, true, true, true);
        }

        mutation_type bad_muts[3]  = { MUT_WEAK, MUT_DOPEY, MUT_CLUMSY };
        mutation_type good_muts[3] = { MUT_STRONG, MUT_CLEVER, MUT_AGILE };

        for (int i = 0; i < 3; ++i)
        {
            while (*(stat_ptrs[i]) <= 0)
            {
                mutation_type good = good_muts[i];
                mutation_type bad  = bad_muts[i];
                if (you.mutation[bad] > orig_mutation[bad]
                    || you.mutation[good] < orig_mutation[good])
                {
                    mutate(good, true, true, true);
                }
                else
                {
                    *(stat_ptrs[i]) = orig_stats[i];
                    break;
                }
            }
        }

        you.max_strength = std::max(you.max_strength, you.strength);
        you.max_intel    = std::max(you.max_intel, you.intel);
        you.max_dex      = std::max(you.max_dex, you.dex);
    }

    // Or maybe Xom accidentally tossed you in deep water or lava.
    dungeon_feature_type feat = grd(you.pos());
    if (feat != orig_feat && !you.airborne()
        && (feat == DNGN_DEEP_WATER && !you.swimming()
            || feat == DNGN_LAVA && player_res_fire() <= 0))
    {
        if (!already_oopsed)
        {
            mpr("You die...");
            god_speaks(GOD_XOM,
                       _get_xom_speech("accidental homicide").c_str());
            god_speaks(GOD_XOM, _get_xom_speech("resurrection").c_str());
        }

        you.teleport(true);
    }

    // Drawing the Xom card from Nemelex's decks of oddities or punishment.
    if (crawl_state.is_god_acting()
        && crawl_state.which_god_acting() != GOD_XOM)
    {
        if (old_entry_cause != you.entry_cause
            && you.entry_cause_god == GOD_XOM)
        {
            you.entry_cause_god = crawl_state.which_god_acting();
        }
    }

    if (you.religion == GOD_XOM && one_chance_in(2)) {
        you.piety = MAX_PIETY - you.piety;

        char buf[8192];
        strcpy(buf, "Your title is now: ");
        const char *newfavour = describe_xom_favour();
        strcat(buf, newfavour);
        god_speaks( you.religion, buf );
    }
}

static void _xom_check_less_runes(int runes_gone)
{
    if (player_in_branch(BRANCH_HALL_OF_ZOT)
        || !(branches[BRANCH_HALL_OF_ZOT].branch_flags & BFLAG_HAS_ORB))
    {
        return;
    }

    int runes_avail = you.attribute[ATTR_UNIQUE_RUNES]
        + you.attribute[ATTR_DEMONIC_RUNES]
        + you.attribute[ATTR_ABYSSAL_RUNES]
        - you.attribute[ATTR_RUNES_IN_ZOT];
    int was_avail = runes_avail + runes_gone;

    // No longer enough available runes to get into Zot.
    if (was_avail >= NUMBER_OF_RUNES_NEEDED
        && runes_avail < NUMBER_OF_RUNES_NEEDED)
    {
        xom_is_stimulated(128, "Xom snickers.", true);
    }
}

void xom_check_lost_item(const item_def& item)
{
    if (item.base_type == OBJ_ORBS)
        xom_is_stimulated(255, "Xom laughs nastily.", true);
    else if (is_fixed_artefact(item))
        xom_is_stimulated(128, "Xom snickers.", true);
    else if (is_rune(item))
    {
        // If you'd dropped it, check if that means you'd dropped your
        // third rune, and now you don't have enough to get into Zot.
        if (item.flags & ISFLAG_BEEN_IN_INV)
            _xom_check_less_runes(item.quantity);

        if (is_unique_rune(item))
            xom_is_stimulated(255, "Xom snickers loudly.", true);
        else if (you.entry_cause == EC_SELF_EXPLICIT
            && !(item.flags & ISFLAG_BEEN_IN_INV))
        {
            // Player voluntarily entered Pan or the Abyss looking for
            // runes, yet never found them.
            if (item.plus == RUNE_ABYSSAL
                && you.attribute[ATTR_ABYSSAL_RUNES] == 0)
            {
                // Ignore Abyss area shifts.
                if (you.level_type != LEVEL_ABYSS)
                {
                    // Abyssal runes are a lot more trouble to find than
                    // demonic runes, so they get twice the stimulation.
                    xom_is_stimulated(128, "Xom snickers.", true);
                }
            }
            else if (item.plus == RUNE_DEMONIC
                     && you.attribute[ATTR_DEMONIC_RUNES] == 0)
            {
                xom_is_stimulated(64, "Xom snickers softly.", true);
            }
        }
    }
}

void xom_check_destroyed_item(const item_def& item, int cause)
{
    int amusement = 0;

    if (item.base_type == OBJ_ORBS)
    {
        xom_is_stimulated(255, "Xom laughs nastily.", true);
        return;
    }
    else if (is_fixed_artefact(item))
        xom_is_stimulated(128, "Xom snickers.", true);
    else if (is_rune(item))
    {
        _xom_check_less_runes(item.quantity);

        if (is_unique_rune(item) || item.plus == RUNE_ABYSSAL)
            amusement = 255;
        else
            amusement = 64 * item.quantity;
    }

    xom_is_stimulated(amusement,
                      (amusement > 128) ? "Xom snickers loudly." :
                      (amusement > 64)  ? "Xom snickers."
                                        : "Xom snickers softly.",
                      true);
}
