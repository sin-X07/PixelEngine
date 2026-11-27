#include "CS2D/CS2DGame.hpp"
#include "Core/ActorTypes.hpp"
#include "Core/PresentationContract.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void Check(bool cond, const char* msg) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::printf("[FAIL] %s\n", msg);
    }
}

std::string FindMapPath() {
    for (const char* candidate : {
             "assets/cs2d/maps/dust_2.tmj",
             "../assets/cs2d/maps/dust_2.tmj",
             "../../assets/cs2d/maps/dust_2.tmj",
         }) {
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return "assets/cs2d/maps/dust_2.tmj";
}

bool LoadBombDefusal(CS2DGame& game, unsigned int seed = 1) {
    game.SetRngSeed(seed);
    game.SetMatchMode(MatchMode::BombDefusal);
    return game.LoadMapHeadless(FindMapPath(), true, false);
}

Presentation::BuyRequest WeaponReq(WeaponId weapon) {
    Presentation::BuyRequest req;
    req.kind = Presentation::BuyKind::Weapon;
    req.weapon = weapon;
    return req;
}

bool FindFreeCell(const CS2DWorld& world, float w, float h, float& outX, float& outY) {
    const float tw = world.GetTileWidth();
    const float th = world.GetTileHeight();
    for (int r = 4; r < world.GetMapHeight() - 4; r += 2) {
        for (int c = 4; c < world.GetMapWidth() - 4; c += 2) {
            const float x = (c + 0.5f) * tw;
            const float y = (r + 0.5f) * th;
            if (!world.CheckRectCollision(x - w * 0.5f, y - h * 0.5f, w, h)) {
                outX = x;
                outY = y;
                return true;
            }
        }
    }
    return false;
}

bool FindHorizontalDuelPair(const CS2DWorld& world, float gap, float& ax, float& ay, float& bx, float& by) {
    const float tw = world.GetTileWidth();
    const float th = world.GetTileHeight();
    for (int r = 6; r < world.GetMapHeight() - 6; r += 2) {
        for (int c = 6; c < world.GetMapWidth() - 6; c += 2) {
            const float x1 = (c + 0.5f) * tw;
            const float y1 = (r + 0.5f) * th;
            const float x2 = x1 + gap;
            if (x2 >= (world.GetMapWidth() - 4) * tw) continue;
            // Require clear actor boxes plus unobstructed horizontal corridor between them.
            if (!world.CheckRectCollision(x1 - 12.0f, y1 - 12.0f, gap + 24.0f, 24.0f)) {
                ax = x1;
                ay = y1;
                bx = x2;
                by = y1;
                return true;
            }
        }
    }
    return false;
}

bool FindPlantSpot(const CS2DWorld& world, float& outX, float& outY) {
    const auto& sites = world.GetBombSites();
    if (sites.empty()) return false;
    const auto& site = sites.front();
    for (const auto& rect : site.rects) {
        for (float py = rect.y + 8.0f; py <= rect.y + rect.height - 8.0f; py += 6.0f) {
            for (float px = rect.x + 8.0f; px <= rect.x + rect.width - 8.0f; px += 6.0f) {
                if (world.FindBombSiteAt(px, py) != nullptr &&
                    !world.CheckRectCollision(px - 10.0f, py - 10.0f, 20.0f, 20.0f)) {
                    outX = px;
                    outY = py;
                    return true;
                }
            }
        }
    }
    if (world.FindBombSiteAt(site.cx, site.cy) != nullptr) {
        outX = site.cx;
        outY = site.cy;
        return true;
    }
    return false;
}

void AdvanceToLive(CS2DGame& game, const std::vector<ActorId>& activeHumans) {
    std::vector<std::pair<ActorId, ActorInput>> inputs;
    for (ActorId id : activeHumans) inputs.emplace_back(id, ActorInput{});
    for (int tick = 0; tick < 600 && game.GetRoundPhase() != RoundPhase::Live; ++tick) {
        game.UpdateMultiActor(1.0f / 60.0f, inputs, nullptr);
    }
}

void TestDefaultActorSlotsInvariant() {
    CS2DGame game;
    Check(LoadBombDefusal(game, 1), "slot-invariant: LoadMapHeadless succeeds");
    const auto& actors = game.GetActors();
    Check(static_cast<int>(actors.size()) == kMaxActors, "slot-invariant: defaults to 10 actor slots");
    Check(game.GetLocalActorId() == 0, "slot-invariant: localActorId defaults to 0");
    Check(game.FindActor(kInvalidActorId) == nullptr, "slot-invariant: invalid ActorId returns nullptr");

    bool idsMatch = true;
    bool controllersMatch = true;
    for (int i = 0; i < static_cast<int>(actors.size()); ++i) {
        if (actors[i].id != i) idsMatch = false;
        if (game.FindActor(i) != &actors[i]) idsMatch = false;
        if (game.ActorIdOf(actors[i]) != i) idsMatch = false;
        const bool expectHuman = (i == 0);
        if (actors[i].IsHuman() != expectHuman || actors[i].IsBot() != !expectHuman) {
            controllersMatch = false;
        }
    }
    Check(idsMatch, "slot-invariant: all actor ids match slot index and lookup");
    Check(controllersMatch, "slot-invariant: slot 0 is Human and slots 1..9 are Bot");
}

void TestMultiHumanMoveBuyRefundAndCombat() {
    CS2DGame game;
    Check(LoadBombDefusal(game, 7), "multi-human-combat: LoadMapHeadless succeeds");

    Check(game.SetActorController(1, ControllerKind::Human, 102, "HumanB"),
          "multi-human-combat: promote slot 1 to Human");
    for (int i = 2; i < kMaxActors; ++i) {
        game.SetActorController(i, ControllerKind::Empty);
    }

    auto* a = game.FindActor(0);
    auto* b = game.FindActor(1);
    Check(a != nullptr && b != nullptr, "multi-human-combat: both actors exist");
    if (!a || !b) return;

    a->team = Team::T;
    a->alive = true;
    a->money = 16000;
    b->team = Team::T;
    b->alive = true;
    b->money = 16000;

    // Place both Humans in a T buy zone so purchases and refunds operate under live rules.
    const auto tSpawns = game.GetWorld().SpawnsForTeam(false);
    Check(!tSpawns.empty(), "multi-human-combat: map provides T spawn for buy zone");
    if (!tSpawns.empty()) {
        a->x = tSpawns.front()->x;
        a->y = tSpawns.front()->y;
        b->x = tSpawns.front()->x + 28.0f;
        b->y = tSpawns.front()->y;
    }

    const auto akReq = WeaponReq(WeaponId::AK47);
    const auto buyA = game.ExecutePurchaseFor(0, akReq);
    Check(buyA.code == Presentation::BuyCode::Ok && a->weapons[0] == WeaponId::AK47,
          "multi-human-combat: Human A buys AK47 independently");
    Check(game.EvaluateRefundFor(0, akReq).amount == 2700,
          "multi-human-combat: Human A sees AK47 refund eligibility");
    Check(game.EvaluateRefundFor(1, akReq).amount == 0,
          "multi-human-combat: Human B has zero refund eligibility for A's weapon");

    const auto buyB = game.ExecutePurchaseFor(1, akReq);
    Check(buyB.code == Presentation::BuyCode::Ok && b->weapons[0] == WeaponId::AK47,
          "multi-human-combat: Human B buys AK47 independently");
    Check(a->money == 16000 - 2700 && b->money == 16000 - 2700,
          "multi-human-combat: money balances remain isolated per actor");

    // Cross-actor drop/pickup refund isolation: B refunds its own gun, A drops AK47, B picks up A's AK47.
    Check(game.ExecuteRefundFor(1, akReq), "multi-human-combat: Human B refunds own AK47 to leave empty slot");
    game.SwitchWeaponSlotFor(0, 0);
    game.DropCurrentWeaponFor(0);
    // Position B at A's drop coordinates while moving A away, then wait > 0.8s pickupDelay.
    const float dropAreaX = a->x;
    const float dropAreaY = a->y;
    a->x -= 240.0f;
    b->x = dropAreaX;
    b->y = dropAreaY;
    for (int tick = 0; tick < 65; ++tick) {
        game.UpdateMultiActor(1.0f / 60.0f, {{0, ActorInput{}}, {1, ActorInput{}}}, nullptr);
    }
    game.TryPickupDropsFor(1, true, nullptr);
    Check(b->weapons[0] == WeaponId::AK47, "multi-human-combat: Human B picks up ground AK47");
    Check(game.EvaluateRefundFor(0, akReq).amount == 0 && game.EvaluateRefundFor(1, akReq).amount == 0,
          "multi-human-combat: neither A nor B can refund A's weapon held by B");

    // Move past Freeze and place A & B inside clear map cells.
    AdvanceToLive(game, {0, 1});
    float moveX = 0.0f, moveY = 0.0f;
    Check(FindFreeCell(game.GetWorld(), 28.0f, 48.0f, moveX, moveY),
          "multi-human-combat: found clear cell for independent movement");
    a->x = moveX;
    a->y = moveY;
    b->x = moveX;
    b->y = moveY;
    a->vx = a->vy = b->vx = b->vy = 0.0f;

    const float startAy = a->y;
    const float startBy = b->y;
    ActorInput moveUp{};
    moveUp.moveUp = true;
    ActorInput moveDown{};
    moveDown.moveDown = true;
    for (int tick = 0; tick < 20; ++tick) {
        game.UpdateMultiActor(1.0f / 60.0f, {{0, moveUp}, {1, moveDown}}, nullptr);
    }
    Check(a->y < startAy - 5.0f && b->y > startBy + 5.0f,
          "multi-human-combat: two Humans move independently in opposite directions");

    // Combat: make B CT in an unobstructed horizontal corridor, equip A with AK47, A shoots B until death.
    float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
    Check(FindHorizontalDuelPair(game.GetWorld(), 50.0f, ax, ay, bx, by),
          "multi-human-combat: found clear horizontal corridor for duel");
    b->team = Team::CT;
    a->x = ax;
    a->y = ay;
    b->x = bx;
    b->y = by;
    b->health = 60;
    b->armor = 0;
    a->SetSlotWeapon(0, WeaponId::AK47);
    game.SwitchWeaponSlotFor(0, 0);
    a->ammo = 30;
    a->shootCooldown = 0.0f;
    a->prevMouseDown = false;

    const int killsBefore = a->kills;
    const int deathsBefore = b->deaths;
    for (int tick = 0; tick < 60 && b->alive; ++tick) {
        ActorInput fireInput{};
        fireInput.fire = true;
        fireInput.aimWorldX = b->x;
        fireInput.aimWorldY = b->y;
        game.UpdateMultiActor(1.0f / 60.0f, {{0, fireInput}, {1, ActorInput{}}}, nullptr);
    }
    Check(!b->alive, "multi-human-combat: Human A's fire kills enemy Human B");
    Check(a->kills == killsBefore + 1 && b->deaths == deathsBefore + 1,
          "multi-human-combat: kill/death counters increment on respective Human actors");
}

void TestMultiHumanC4PlantDefuseIsolation() {
    CS2DGame game;
    Check(LoadBombDefusal(game, 13), "multi-human-c4: LoadMapHeadless succeeds");

    Check(game.SetActorController(1, ControllerKind::Human, 201, "T-Mate"),
          "multi-human-c4: slot 1 promoted to Human");
    Check(game.SetActorController(2, ControllerKind::Human, 202, "CT-Star"),
          "multi-human-c4: slot 2 promoted to Human");
    for (int i = 3; i < kMaxActors; ++i) {
        game.SetActorController(i, ControllerKind::Empty);
    }

    auto* t0 = game.FindActor(0);
    auto* t1 = game.FindActor(1);
    auto* ct2 = game.FindActor(2);
    if (!t0 || !t1 || !ct2) return;

    t0->team = Team::T;
    t1->team = Team::T;
    ct2->team = Team::CT;
    t0->alive = t1->alive = ct2->alive = true;

    float plantX = 0.0f, plantY = 0.0f;
    Check(FindPlantSpot(game.GetWorld(), plantX, plantY), "multi-human-c4: found legal bombsite plant spot");
    if (plantX == 0.0f && plantY == 0.0f) return;

    AdvanceToLive(game, {0, 1, 2});

    // Drop C4 away from T0 and have T1 pick it up after the 1.0s bombDropDelay_.
    t0->x = plantX - 180.0f;
    t0->y = plantY;
    ct2->x = plantX + 240.0f;
    ct2->y = plantY + 240.0f;
    game.DropC4(plantX, plantY);
    t1->x = plantX;
    t1->y = plantY;
    for (int tick = 0; tick < 75; ++tick) {
        game.UpdateMultiActor(1.0f / 60.0f, {{0, ActorInput{}}, {1, ActorInput{}}, {2, ActorInput{}}}, nullptr);
    }
    Check(game.ActorHasC4(*t1) && game.GetBombCarrierId() == 1,
          "multi-human-c4: second Human picks up dropped C4");
    Check(game.GetFriendlyC4CarrierNameFor(Team::T) == "T-Mate",
          "multi-human-c4: T side sees Human carrier's name");
    Check(game.GetFriendlyC4CarrierNameFor(Team::CT).empty(),
          "multi-human-c4: CT side cannot inspect enemy C4 carrier");

    // Hold C4 on T1 and plant via UpdateMultiActor interact=true.
    game.ToggleHoldC4For(1);
    Check(t1->holdingC4, "multi-human-c4: ToggleHoldC4For puts C4 in T1's hands");
    for (int tick = 0; tick < 240 && !game.IsBombPlanted(); ++tick) {
        ActorInput plantInput{};
        plantInput.interact = true;
        game.UpdateMultiActor(1.0f / 60.0f, {{0, ActorInput{}}, {1, plantInput}, {2, ActorInput{}}}, nullptr);
    }
    Check(game.IsBombPlanted(), "multi-human-c4: Human T1 plants C4 via UpdateMultiActor");

    // Move T1 out of the way and move CT2 onto the planted bomb to defuse.
    t1->x = plantX - 180.0f;
    ct2->x = game.GetBombX();
    ct2->y = game.GetBombY();
    ct2->hasDefuseKit = false;
    for (int tick = 0; tick < 45; ++tick) {
        ActorInput defuseInput{};
        defuseInput.interact = true;
        game.UpdateMultiActor(1.0f / 60.0f, {{0, ActorInput{}}, {1, ActorInput{}}, {2, defuseInput}}, nullptr);
    }
    Check(game.GetDefuseProgress() > 0.05f, "multi-human-c4: Human CT2 advances defuse progress");
    Check(game.IsDefuseVisibleTo(Team::CT) && !game.IsDefuseVisibleTo(Team::T),
          "multi-human-c4: defuse bar visible to CT and hidden from T");
    Check(game.GetDefuserName() == "CT-Star", "multi-human-c4: GetDefuserName returns Human CT2 name");
}

void TestSpectateHumanTeammateAndControllerSwitch() {
    CS2DGame game;
    Check(LoadBombDefusal(game, 21), "spectate-switch: LoadMapHeadless succeeds");

    Check(game.SetActorController(1, ControllerKind::Human, 301, "BuddyHuman"),
          "spectate-switch: slot 1 is Human teammate");
    Check(game.SetActorController(2, ControllerKind::Human, 302, "ShooterT"),
          "spectate-switch: slot 2 is Human enemy");
    for (int i = 3; i < kMaxActors; ++i) {
        game.SetActorController(i, ControllerKind::Empty);
    }

    auto* local = game.FindActor(0);
    auto* buddy = game.FindActor(1);
    auto* shooter = game.FindActor(2);
    if (!local || !buddy || !shooter) return;

    local->team = Team::CT;
    buddy->team = Team::CT;
    shooter->team = Team::T;
    local->alive = buddy->alive = shooter->alive = true;

    AdvanceToLive(game, {0, 1, 2});

    float ax = 0.0f, ay = 0.0f, sx = 0.0f, sy = 0.0f;
    Check(FindHorizontalDuelPair(game.GetWorld(), 50.0f, ax, ay, sx, sy),
          "spectate-switch: found clear corridor for kill");
    local->x = ax;
    local->y = ay;
    local->health = 30;
    local->armor = 0;
    buddy->x = ax - 20.0f;
    buddy->y = ay;
    shooter->x = sx;
    shooter->y = sy;
    shooter->SetSlotWeapon(0, WeaponId::AWP);
    game.SwitchWeaponSlotFor(2, 0);
    shooter->ammo = 5;
    shooter->shootCooldown = 0.0f;
    shooter->prevMouseDown = false;

    for (int tick = 0; tick < 30 && local->alive; ++tick) {
        ActorInput fire{};
        fire.fire = true;
        fire.aimWorldX = local->x;
        fire.aimWorldY = local->y;
        game.UpdateMultiActor(1.0f / 60.0f, {{0, ActorInput{}}, {1, ActorInput{}}, {2, fire}}, nullptr);
    }
    Check(!local->alive && game.IsDeathFreeze(),
          "spectate-switch: local Human enters death freeze when killed");

    // Advance beyond DeathFreezeSeconds (1.8s) to hand off to live Human teammate.
    for (int tick = 0; tick < 120; ++tick) {
        game.UpdateMultiActor(1.0f / 60.0f, {{0, ActorInput{}}, {1, ActorInput{}}, {2, ActorInput{}}}, nullptr);
    }
    Check(!game.IsDeathFreeze() && game.IsSpectating(),
          "spectate-switch: enters spectate after death freeze");
    Check(game.GetSpectatedPlayer() == buddy,
          "spectate-switch: dead local Human spectates living Human teammate");

    // Runtime Bot <-> Human controller switch on slot 4.
    Check(game.SetActorController(4, ControllerKind::Human, 404, "TempHuman"),
          "spectate-switch: slot 4 switches Empty/Bot -> Human");
    const auto* slot4 = game.FindActor(4);
    Check(slot4 && slot4->IsHuman() && slot4->peerId == 404 && slot4->name == "TempHuman",
          "spectate-switch: slot 4 reflects Human peerId and name");
    Check(game.SetActorController(4, ControllerKind::Bot, 0, "TempBot"),
          "spectate-switch: slot 4 switches Human -> Bot");
    Check(slot4 && slot4->IsBot() && !slot4->IsHuman() && slot4->name == "TempBot",
          "spectate-switch: slot 4 reverts cleanly back to Bot");

    game.SetLocalActorId(kInvalidActorId);
    Check(game.GetLocalActorId() == 0, "spectate-switch: invalid SetLocalActorId is ignored");
}

} // namespace

int main() {
    TestDefaultActorSlotsInvariant();
    TestMultiHumanMoveBuyRefundAndCombat();
    TestMultiHumanC4PlantDefuseIsolation();
    TestSpectateHumanTeammateAndControllerSwitch();

    std::printf("\n===== pixelsim_actor_test: %d passed, %d failed =====\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
