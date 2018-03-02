#include "player.h"
#include "../engine/threadmanager.h"
#include "../fagui/dialogmanager.h"
#include "../fasavegame/gameloader.h"
#include "actorstats.h"
#include "boost/algorithm/clamp.hpp"
#include "diabloexe/characterstats.h"
#include "equiptarget.h"
#include "itembonus.h"
#include "itemenums.h"
#include "itemmap.h"
#include "misc/random.h"
#include "playerbehaviour.h"
#include "world.h"
#include <misc/assert.h>
#include <misc/stringops.h>
#include <string>

namespace FAWorld
{
    const std::string Player::typeId = "player";

    const char* toString(PlayerClass value)
    {
        switch (value)
        {
            case PlayerClass::warrior:
                return "warrior";
            case PlayerClass::rogue:
                return "rogue";
            case PlayerClass::sorcerer:
                return "sorceror";
        }
        return "unknown";
    }

    Player::Player(World& world) : Actor(world)
    {
        // TODO: hack - need to think of some more elegant way of handling Actors in general
        DiabloExe::CharacterStats stats;
        init(stats);
        initCommon();
    }

    Player::Player(World& world, const std::string& className, const DiabloExe::CharacterStats& charStats) : Actor(world)
    {
        init(charStats);
        initCommon();
    }

    void Player::initCommon()
    {
        mWorld.registerPlayer(this);
        mInventory.equipChanged.connect([this]() { updateSprites(); });
        mMoveHandler.positionReached.connect(positionReached);
    }

    int Player::getTotalGold() const
    {
        int totalCnt = 0;
        for (auto target : mInventory.getBeltAndInventoryItemPositions())
        {
            auto& item = mInventory.getItemAt(target);
            if (item.getType() == ItemType::gold)
                totalCnt += item.mCount;
        }
        return totalCnt;
    }

    void Player::setPlayerClass(PlayerClass playerClass)
    {
        mPlayerClass = playerClass;
        updateSprites();
    }

    double Player::meleeDamageVs(const Actor* /*actor*/) const
    {
        auto bonus = getItemBonus();
        auto dmg = Random::randomInRange(bonus.minAttackDamage, bonus.maxAttackDamage);
        dmg += dmg * getPercentDamageBonus() / 100;
        dmg += getCharacterBaseDamage();
        dmg += getDamageBonus();
        // critical hit for warriors:
        if (mPlayerClass == PlayerClass::warrior && Random::randomInRange(0, 99) < getCharacterLevel())
            dmg *= 2;
        return dmg;
    }

    ItemBonus Player::getItemBonus() const { return mInventory.getTotalItemBonus(); }

    void Player::init(const DiabloExe::CharacterStats& charStats)
    {
        mPlayerStats = {charStats};
        mFaction = Faction::heaven();
        mMoveHandler = MovementHandler(World::getTicksInPeriod(0.1f)); // allow players to repath much more often than other actors

        mStats.mAttackDamage = 60;

        mBehaviour.reset(new PlayerBehaviour(this));
    }

    Player::Player(World& world, FASaveGame::GameLoader& loader, const DiabloExe::DiabloExe& exe) : Actor(world, loader, exe)
    {
        mPlayerClass = static_cast<PlayerClass>(loader.load<int32_t>());
        mPlayerStats = {loader};
        initCommon();
    }

    void Player::save(FASaveGame::GameSaver& saver)
    {
        Serial::ScopedCategorySaver cat("Player", saver);

        Actor::save(saver);
        saver.save(static_cast<int32_t>(mPlayerClass));
        mPlayerStats.save(saver);
    }

    bool Player::checkHit(Actor* enemy)
    {
        // let's throw some formulas, parameters will be placeholders for now
        auto roll = Random::randomInRange(0, 99);
        auto toHit = mPlayerStats.mDexterity / 2;
        toHit += getArmorPenetration();
        toHit -= enemy->getArmor();
        toHit += getCharacterLevel();
        toHit += 50;
        if (mPlayerClass == PlayerClass::warrior)
            toHit += 20;
        toHit = boost::algorithm::clamp(toHit, 5, 95);
        return roll < toHit;
    }

    Player::~Player() { mWorld.deregisterPlayer(this); }

    char getClassCode(PlayerClass playerClass)
    {
        switch (playerClass)
        {
            case PlayerClass::warrior:
                return 'w';
            case PlayerClass::rogue:
                return 'r';
            case PlayerClass::sorcerer:
                return 's';
        }
        release_assert("Class unsupported");
        return '\0';
    }

    void Player::updateSprites()
    {
        auto classCode = getClassCode(mPlayerClass);

        std::string armour = "l", weapon;
        if (!mInventory.getBody().isEmpty())
        {
            switch (mInventory.getBody().getType())
            {
                case ItemType::heavyArmor:
                {
                    armour = "h";
                    break;
                }

                case ItemType::mediumArmor:
                {
                    armour = "m";
                    break;
                }

                case ItemType::lightArmor:
                default:
                {
                    armour = "l";
                    break;
                }
            }
        }
        if (mInventory.getLeftHand().isEmpty() && mInventory.getRightHand().isEmpty())
        {
            weapon = "n";
        }
        else if ((mInventory.getLeftHand().isEmpty() && !mInventory.getRightHand().isEmpty()) ||
                 (!mInventory.getLeftHand().isEmpty() && mInventory.getRightHand().isEmpty()))
        {
            const Item* hand = nullptr;

            if (!mInventory.getLeftHand().isEmpty())
                hand = &mInventory.getLeftHand();
            else
                hand = &mInventory.getRightHand();
            switch (hand->getType())
            {
                case ItemType::axe:
                {
                    if (hand->getEquipLoc() == ItemEquipType::oneHanded)
                        weapon = "s";
                    else
                        weapon = "a";
                    break;
                }

                case ItemType::mace:
                {
                    weapon = "m";
                    break;
                }

                case ItemType::bow:
                {
                    weapon = "b";
                    break;
                }

                case ItemType::shield:
                {
                    weapon = "u";
                    break;
                }

                case ItemType::sword:
                {
                    weapon = "s";
                    break;
                }

                default:
                {
                    weapon = "n";
                    break;
                }
            }
        }

        else if (!mInventory.getLeftHand().isEmpty() && !mInventory.getRightHand().isEmpty())
        {
            if ((mInventory.getLeftHand().getType() == ItemType::sword && mInventory.getRightHand().getType() == ItemType::shield) ||
                (mInventory.getLeftHand().getType() == ItemType::shield && mInventory.getRightHand().getType() == ItemType::sword))
                weapon = "d";

            else if (mInventory.getLeftHand().getType() == ItemType::bow && mInventory.getRightHand().getType() == ItemType::bow)
                weapon = "b";

            else if (mInventory.getLeftHand().getType() == ItemType::staff && mInventory.getRightHand().getType() == ItemType::staff)
                weapon = "t";
            else if (mInventory.getLeftHand().getType() == ItemType::mace || mInventory.getRightHand().getType() == ItemType::mace)
                weapon = "h";

            release_assert(!weapon.empty()); // Empty weapon format
        }
        auto weaponCode = weapon;
        auto armourCode = armour;

        auto helper = [&](bool isDie) {
            std::string weapFormat = weaponCode;

            if (isDie)
                weapFormat = "n";

            boost::format fmt("plrgfx/%s/%s%s%s/%s%s%s%s.cl2");
            fmt % toString(mPlayerClass) % classCode % armourCode % weapFormat % classCode % armourCode % weapFormat;
            return fmt;
        };

        auto renderer = FARender::Renderer::get();

        mAnimation.setAnimation(AnimState::dead, renderer->loadImage((helper(true) % "dt").str()));
        mAnimation.setAnimation(AnimState::attack, renderer->loadImage((helper(false) % "at").str()));
        mAnimation.setAnimation(AnimState::hit, renderer->loadImage((helper(false) % "ht").str()));

        if (getLevel() && getLevel()->isTown())
        {
            mAnimation.setAnimation(AnimState::walk, renderer->loadImage((helper(false) % "wl").str()));
            mAnimation.setAnimation(AnimState::idle, renderer->loadImage((helper(false) % "st").str()));
        }
        else
        {
            mAnimation.setAnimation(AnimState::walk, renderer->loadImage((helper(false) % "aw").str()));
            mAnimation.setAnimation(AnimState::idle, renderer->loadImage((helper(false) % "as").str()));
        }
    }

    bool Player::dropItem(const FAWorld::Tile& clickedTile)
    {
        auto cursorItem = mInventory.getItemAt(MakeEquipTarget<EquipTargetType::cursor>());
        auto initialDir = Misc::getVecDir(Misc::getVec(getPos().current(), {clickedTile.x, clickedTile.y}));
        auto curPos = getPos().current();
        auto tryDrop = [&](const std::pair<int32_t, int32_t>& pos) {
            if (getLevel()->dropItem(std::unique_ptr<Item>{new Item(cursorItem)}, *this, FAWorld::Tile(pos.first, pos.second)))
            {
                mInventory.setCursorHeld({});
                return true;
            }
            return false;
        };

        auto isPosOk = [&](std::pair<int32_t, int32_t> pos) {
            return getLevel()->isPassableFor(pos.first, pos.second, this) && !getLevel()->getItemMap().getItemAt({pos.first, pos.second});
        };

        if (clickedTile == FAWorld::Tile{curPos.first, curPos.second})
        {
            if (isPosOk(curPos))
                return tryDrop(curPos);
            initialDir = Misc::Direction::south;
        }

        constexpr auto directionCnt = 8;
        for (auto diff : {0, -1, 1})
        {
            auto dir = static_cast<Misc::Direction>((static_cast<int32_t>(initialDir) + diff + directionCnt) % directionCnt);
            auto pos = Misc::getNextPosByDir(curPos, dir);
            if (isPosOk(pos))
                return tryDrop(pos);
        }

        if (isPosOk(curPos))
            return tryDrop(curPos);
        return false;
    }

    bool Player::canTalkTo(Actor* actor)
    {
        if (actor == nullptr)
            return false;

        if (this == actor)
            return false;

        if (!actor->canTalk())
            return false;

        if (isEnemy(actor))
            return false;

        return true;
    }

    void Player::update(bool noclip)
    {
        Actor::update(noclip);

        // handle talking to npcs
        if (mTarget.getType() == Target::Type::Actor)
        {
            Actor* target = mTarget.get<Actor*>();

            if (target && target->getPos().isNear(this->getPos()) && canTalkTo(target))
            {
                mWorld.mDlgManager->talk(target);
                mTarget.clear();
            }
        }
    }
}
