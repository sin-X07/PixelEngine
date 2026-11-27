#include "CS2DGame.hpp"
#include "CS2DRenderer.hpp"
#include <algorithm>
#include <cmath>

// ============================================================================
// 客户端渲染接线（不在 PixelSim 无头仿真库内编译）
// ============================================================================

bool CS2DGame::LoadMap(const std::string& resolvedTMJPath, CS2DRenderer* renderer,
                       bool resetMatch) {
    renderer_ = renderer;
    const float visionRadius = renderer_ ? renderer_->GetVisionRadius() : Tuning::VisionRadius;
    if (!LoadMapHeadless(resolvedTMJPath, resetMatch, visionRadius)) {
        return false;
    }
    if (renderer_) {
        renderer_->SetWorld(world_.get());
        visibility_.SetVisionRadius(renderer_->GetVisionRadius());
    }
    return true;
}

void CS2DGame::RenderMapPreview(int viewW, int viewH,
                                float centerX, float centerY, float zoom,
                                bool clampToMap) {
    if (!renderer_ || !world_) return;
    renderer_->SetViewportSize(viewW, viewH);
    renderer_->SetPreviewCamera(centerX, centerY, zoom, clampToMap);
    renderer_->DrawWorld();
}

void CS2DGame::PrepareCamera(int viewW, int viewH, float interpolation) {
    if (!renderer_ || !world_) return;
    renderInterpolation_ = std::clamp(interpolation, 0.0f, 1.0f);
    if (IsDeathFreeze()) {
        cameraPrepared_ = true;
        return;
    }
    const auto playerView = GetActorView(player_);
    Presentation::ScopeView scope = GetRenderScopeView();
    const float blend = viewHistoryValid_ ? renderInterpolation_ : 1.0f;
    const float renderEdge = previousEdgeZoom_ + (edgeZoom_ - previousEdgeZoom_) * blend;
    const float edgeFactor = renderEdge <= 1 ? 1.12f + (.70f-1.12f)*renderEdge : .70f + (.50f-.70f)*(renderEdge-1);
    const bool bombView = IsSpectating() && specMode_ == SpectateMode::Bomb;
    const float cone = bombView ? 360.0f : Tuning::VisionConeDeg *
        (1.0f + (Tuning::EdgeZoomFovAngleMul - 1.0f) * std::min(renderEdge,1.0f));
    renderer_->SetConeAngleDeg(cone);
    visibility_.SetConeAngleDeg(cone);
    if (IsSpectating()) {
        const Player* target = GetSpectatedPlayer();
        const auto targetView = target ? GetActorView(*target) : playerView;
        const float x = bombView ? bombX_ : specMode_ == SpectateMode::Free ? specFreeX_ : targetView.x;
        const float y = bombView ? bombY_ : specMode_ == SpectateMode::Free ? specFreeY_ : targetView.y;
        renderer_->UpdateCamera(x,y,viewW,viewH,edgeFactor);
    } else {
        const float lensRadius = Presentation::kScopeCircleRadiusFrac * std::min(viewW,viewH) * scope.transition;
        renderer_->UpdatePlayerCamera(playerView.x,playerView.y,player_.size,
            GetRenderAimX(),GetRenderAimY(),viewW,viewH,edgeFactor,std::max(8.0f,lensRadius),
            {recoilShake_ * std::cos(recoilShakeAge_ * 2.0f * Tuning::Pi * 18.0f),
             recoilShake_ * std::sin(recoilShakeAge_ * 2.0f * Tuning::Pi * 13.0f)});
    }
    visibility_.SetVisionRadius(renderer_->GetVisionRadius());

    cameraPrepared_ = true;
    cameraScopeSettled_ = !scope.active && std::abs(scope.zoomMultiplier - 1.0f) < 0.001f &&
        renderer_->CameraSettled();
}

void CS2DGame::Render(int viewW, int viewH) {
    if (!renderer_ || !world_)
        return;
    if (!cameraPrepared_)
        PrepareCamera(viewW, viewH);
    cameraPrepared_ = false;
    const Presentation::ScopeView scope = GetRenderScopeView();

    // 镜内沿用扇形视野，镜外叠加雾遮罩。
    const float scopeRadiusPx =
        Presentation::kScopeCircleRadiusFrac * static_cast<float>(std::min(viewW, viewH));

    DrawWorldPass();

    // 开镜覆盖层：世界雾之后、闪光覆盖之前（契约 v1.3）
    // 圆心用「准星世界点投影到屏幕」而不是虚拟光标，避免相机被地图边界钳制时镜筒错位。
    // 大十字线宽来自设置；中心缺口在「移动扩散」开启时随移动量张开。
    if (scope.transition > .001f && player_.alive) {
        Presentation::ScopeOverlayParams op;
        op.scope = scope;
        op.motionBlur = aimMotionBlur_;
        op.centerX = (GetRenderAimX() - renderer_->GetCameraX()) * renderer_->GetZoom();
        op.centerY = (GetRenderAimY() - renderer_->GetCameraY()) * renderer_->GetZoom();
        op.radiusPx = scopeRadiusPx;
        op.crossThickness = scopeCrossThickness_;
        op.crossGap = Presentation::ComputeScopeCrossGap(GetCrosshairBloom(), scopeCrossDynamic_);
        if (renderer_->BeginScopeScene(GetRenderAimX(),GetRenderAimY(),op.centerX,op.centerY,scope.zoomMultiplier)) {
            DrawWorldPass(true);
            renderer_->EndScopeScene();
            const float aperture = op.radiusPx * scope.transition;
            renderer_->DrawScopeScene(op.centerX,op.centerY,aperture,scope.transition);
        }
        renderer_->DrawScopeOverlay(op);
        // 镜圈外的玩家按主场景原大小单独贴；镜圈内不贴（镜内那一遍已放大绘制），
        // 否则同一玩家会出现一大一小两重影像。
        const auto playerView = GetActorView(player_);
        const float scopeWorldRadius = scopeRadiusPx / std::max(0.001f, renderer_->GetZoom());
        const float lensDistance = std::hypot(playerView.x - GetRenderAimX(),
                                              playerView.y - GetRenderAimY());
        if (lensDistance > scopeWorldRadius + player_.size) {
            renderer_->SetVisibilityFilter([this,playerView](float x,float y) {
                return visibility_.HasLineOfSight(playerView.x,playerView.y,x,y) &&
                       !SegmentBlockedBySmoke(playerView.x,playerView.y,x,y);
            },true);
            renderer_->DrawCharacter(MakeActorDrawParams(player_,&player_));
            renderer_->SetVisibilityFilter({});
        }
    }

    // 闪光致盲（最上层）：致盲期间完全丧失视野，最后 0.4s 快速淡出
    if (flashTimer_ > 0.0f) {
        // 末尾 0.4 秒淡出，整体再乘以本次距离决定的峰值强度
        const float fade = std::clamp(flashTimer_ / 0.4f, 0.0f, 1.0f);
        const float peak = std::clamp(flashPeak_ > 0.0f ? flashPeak_ : 1.0f, 0.0f, 1.0f);
        renderer_->DrawFlashOverlay(static_cast<Uint8>(255.0f * fade * peak));
    }
}

SpriteDrawParams CS2DGame::MakeActorDrawParams(const Player& actor, const Player* observer) const {
    SpriteDrawParams sp;
    const auto view = GetActorView(actor);
    sp.x = view.x;
    sp.y = view.y;
    sp.size = actor.size;
    sp.aimAngle = &actor == &player_ ? GetRenderAimAngle() : view.aimAngle;
    sp.spriteKey = actor.spriteKey;
    sp.weaponSlot = ActorPoseSlot(actor);
    sp.weaponId = static_cast<int>(sp.weaponSlot == 4 ? WeaponId::C4 : actor.CurrentWeapon().id);
    sp.meleeSwing = actor.shootAnimTimer / .12f;
    sp.animState = static_cast<int>(actor.animState);
    sp.animTimer = view.animTimer;
    sp.deathTimer = actor.deathTimer;
    sp.alpha = 255;
    // 烟雾遮蔽：人物进入烟雾时随烟的稀疏程度渐隐，烟散时同步渐显。
    // 与烟雾自身的 fadeIn/fadeOut 及径向浓度一致（SmokeOpacityAt 与渲染同口径）。
    if (const float smoke = SmokeOpacityAt(view.x, view.y); smoke > 0.001f) {
        // 不完全隐藏：留一点轮廓，避免「人凭空消失」；最浓时约 12% 可见度。
        const float keep = 1.0f - 0.88f * smoke;
        sp.alpha = static_cast<Uint8>(std::clamp(255.0f * keep, 0.0f, 255.0f));
    }
    // 契约 v1 第 5 节：沿用 Team，不建立第二份阵营状态。
    // 修复：死斗是一人成军——不区分队伍，所有人对观察者都是"非友军"（画红环），
    // team 传 None，避免 UI/渲染再按 T/CT 着色。
    const bool deathmatch = (mode_ == MatchMode::DeathMatch);
    sp.team = deathmatch ? Team::None : actor.team;
    sp.friendly = deathmatch ? false : (actor.team == player_.team);
    sp.localPlayer = (&actor == observer);
    return sp;
}

void CS2DGame::DrawWorldPass(bool lens) {
    renderer_->DrawWorld();

    const bool freeView = IsSpectating() && specMode_ == SpectateMode::Free;
    const Player *observer = GetSpectatedPlayer();
    if (!observer)
        observer = &player_;
    const auto observerView = GetActorView(*observer);
    const bool bombView = specMode_ == SpectateMode::Bomb && IsSpectating();
    const float observerX = bombView ? bombX_ : freeView ? specFreeX_ : observerView.x;
    const float observerY = bombView ? bombY_ : freeView ? specFreeY_ : observerView.y;
    const float observerAngle = observer == &player_ ? GetRenderAimAngle() : observerView.aimAngle;

    // 自由视角：视野来自「存活队友视锥的并集」，与最后跟随的那只 BOT 的朝向无关。
    // 没有存活队友时回退为按自己（阵亡位置）的朝向，画面不会黑屏。
    VisibilityQuery::Observer freeObservers[VisibilityQuery::kMaxObservers];
    std::size_t freeObserverCount = 0;
    if (freeView) {
        freeObserverCount = CollectFreeSpectateObservers(freeObservers,
                                                        VisibilityQuery::kMaxObservers);
    }
    const bool useUnion = freeObserverCount > 0;
    // 单一几何来源：雾、角色剔除、掉落物可见性全部走这一个判定。
    const float singleX = useUnion ? freeObservers[0].x : observerX;
    const float singleY = useUnion ? freeObservers[0].y : observerY;
    const float singleAngle = useUnion ? freeObservers[0].angle : observerAngle;
    auto visibilityAt = [&](float x, float y) {
        return useUnion
            ? visibility_.ComputeVisibilityUnion(freeObservers, freeObserverCount, x, y)
            : visibility_.ComputeVisibility(singleX, singleY, singleAngle, x, y);
    };
    // 烟雾遮挡：按各观察者单独判断——任一观察者到目标的连线无烟即不算被烟挡。
    auto smokeBlocked = [&](float x, float y) {
        if (!useUnion) return SegmentBlockedBySmoke(singleX, singleY, x, y);
        for (std::size_t i = 0; i < freeObserverCount; ++i) {
            if (!SegmentBlockedBySmoke(freeObservers[i].x, freeObservers[i].y, x, y))
                return false;
        }
        return true;
    };
    const auto worldVisible = [&](float x, float y) {
        const bool visible = bombView ? visibility_.HasLineOfSight(observerX, observerY, x, y)
                                      : visibilityAt(x, y) > 0;
        return visible && !smokeBlocked(x, y);
    };
    // 角色专用：烟雾**不参与**硬剔除。烟里的人要画出来，靠 SpriteDrawParams::alpha
    // 做渐隐（见 MakeActorDrawParams → SmokeOpacityAt）。若这里也过 smokeBlocked，
    // 角色会在进烟的瞬间整块消失，透明度过渡永远看不到。
    const auto actorVisible = [&](float x, float y) {
        return bombView ? visibility_.HasLineOfSight(observerX, observerY, x, y)
                        : visibilityAt(x, y) > 0;
    };
    const auto ownBodyVisible = [&](float x, float y) {
        return visibility_.HasLineOfSight(observerX, observerY, x, y) &&
               !SegmentBlockedBySmoke(observerX, observerY, x, y);
    };
    renderer_->SetVisibilityFilter(worldVisible);

    // 燃烧弹留下的火区（在地面层、角色之下）
    for (const auto &f : fires_) {
        renderer_->DrawFireCells(f.cells, f.x, f.y, f.radius, f.age, f.life, f.seed);
    }

    for (const auto &bot : bots_) {
        if (!bot.alive && bot.deathTimer > 2.0f)
            continue;
        const SpriteDrawParams sp = MakeActorDrawParams(bot, observer);
        // 角色用 actorVisible：烟雾只影响透明度，不做硬剔除。
        if (&bot == observer)
            renderer_->SetVisibilityFilter(ownBodyVisible, true);
        else
            renderer_->SetVisibilityFilter(actorVisible);
        renderer_->DrawCharacter(sp);
        if (ActorHasC4(bot))
            renderer_->DrawBackpackC4(sp.x, sp.y, sp.size, sp.aimAngle, clockSec_);
        if (&bot == observer)
            renderer_->SetVisibilityFilter(worldVisible, false, true);
        else
            renderer_->SetVisibilityFilter(worldVisible);
    }

    // 玩家自己：镜内那一遍（lens）与 BOT 走同一套 MakeActorDrawParams，因此镜里镜外
    // 放大一致；镜圈外的那一份由 Render 在镜面合成之后按主场景比例单独贴。
    // 镜内/镜外二选一，避免同一玩家一大一小重影。
    // 自身躯体的过滤条件永远是 ownBodyVisible（自己在自己脚下，不受视锥限制）。
    const bool ownBodyDrawable = !IsSpectating() && (player_.alive || player_.deathTimer <= 2.0f);
    const bool scopedBody = player_.alive && GetRenderScopeView().transition > .001f;
    if (ownBodyDrawable && (lens || !scopedBody)) {
        const SpriteDrawParams sp = MakeActorDrawParams(player_, observer);
        renderer_->SetVisibilityFilter(ownBodyVisible, true);
        renderer_->DrawCharacter(sp);
        renderer_->SetVisibilityFilter(worldVisible, false, true);
    }

    // 枪弹视觉段：玩法层已生成的唯一数据源，UI 只绘制（契约 v1 第 6 节）
    renderer_->DrawBulletVisuals(GetBulletVisuals());

    // 投掷物
    for (const auto &g : grenades_) {
        if (!g.active)
            continue;
        renderer_->DrawThrownGrenade(static_cast<int>(g.type), g.x, g.y, g.spin);
    }

    for (const auto &decoy : decoys_)
        renderer_->DrawThrownGrenade(static_cast<int>(WeaponId::GrenadeDecoy), decoy.x, decoy.y, 0);

    // 目标物标记（包点范围）：画在角色之下、遮罩之上，保证始终可辨
    // 地面掉落物（用 _d 图标）
    for (const auto &d : drops_) {
        if (d.defuseKit) {
            renderer_->DrawDroppedKit(d.x, d.y);
            continue;
        }
        if (d.id == WeaponId::None)
            continue;
        renderer_->DrawDroppedWeapon(static_cast<int>(d.id), d.x, d.y, d.rot);
    }

    // C4 使用实体素材绘制。
    // 掉在地上的 C4：T 方始终可见；CT 只在「有人看到过」的情报期内可见
    if (bombDropped_) {
        bool visible = (player_.team == Team::T) || bombIntel_ || IsSpectating();
        if (!visible && player_.alive && renderer_) {
            visible = visibility_.IsVisibleToPlayer(player_.x, player_.y, player_.EffectiveAim(), bombDropX_,
                                                    bombDropY_) &&
                      !SegmentBlockedBySmoke(player_.x, player_.y, bombDropX_, bombDropY_);
        }
        if (visible) {
            renderer_->DrawDroppedWeapon(static_cast<int>(WeaponId::C4), bombDropX_, bombDropY_, 0);
        }
    }

    if (bombPlanted_) {
        renderer_->DrawDroppedWeapon(static_cast<int>(WeaponId::C4), bombX_, bombY_, 0);
    }

    for (const auto &fx : blasts_) {
        const float t = 1 - fx.life / std::max(.001f, fx.total);
        for (int i = 0; i < 18; ++i) {
            const float angle = i * 2.399963f;
            const float d = (3 + t * 16) * (1 + (i % 3) * .2f);
            const float x = fx.x + std::cos(angle) * d, y = fx.y + std::sin(angle) * d;
            if (!worldVisible(x, y))
                continue;
            renderer_->DrawCircleFilled(x, y, (1 - t) * (2 + i % 3), 255, 185 - i * 4, 90,
                                        static_cast<Uint8>((1 - t) * 170));
        }
    }
    renderer_->DrawObjectiveMarkers(clockSec_);
    renderer_->DrawOverlay();
    renderer_->SetVisibilityFilter({});
    for (const auto &smoke : smokes_)
        renderer_->DrawSmokeCloud(smoke.x, smoke.y, smoke.radius, smoke.age, smoke.expandTime, smoke.life,
                                  smoke.maxLife, smoke.seed);
    if (bombView) {
        renderer_->DrawVisionFog(observerX, observerY, observerAngle);
    } else if (useUnion) {
        renderer_->DrawVisionFogUnion(freeObservers, freeObserverCount);
    } else {
        renderer_->DrawVisionFog(observerX, observerY, observerAngle);
    }
}
