#include "AimModules.hpp"
#include "../../AntiDbg/AntiDbg.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <vector>
#include <utility>
#include <algorithm>
#include <Windows.h>
#include <intrin.h>
#include <cstdio>
#include "../Options.hpp"
#include "../Math/Vectors/Vector3.hpp"
#include "../Math/Unity/Transform/Transform.hpp"
#include "../Memory/Memory.hpp"
#include "../EMemory.hpp"
#include "../Cheat.hpp"
#include "../SharedFrame.hpp"
#include "../RayCast/RayCast.hpp"
#include "HiResSleep.hpp"   // Cheat::HiRes::SleepMillis — precisão sub-ms sem timeBeginPeriod global

extern Memory* g_Memory;
extern int ScreenWidth;
extern int ScreenHeight;

namespace Off {
    inline uint32_t MatchEntityDict      = 0x68;
    inline uint32_t DictSize             = 0x10;
    inline uint32_t DictEntries          = 0x14;
    inline uint32_t Weapon               = 0x434;  // atualizado 2026-09-16 (era 0x3F4)
    inline uint32_t FireComponent        = 0x58;   // WeaponData
    inline uint32_t TangentTheta         = 0x10;   // CFCNBJJLEKE float em MLAEIFDLICK/GGJNCCFNAAL/NIPIBLMPCNN/BGGDKAGBAJK (dump v7a 1.132.1)
    inline uint32_t EntityIsFiring       = 0x1C8;  // atualizado 2026-09-16 (era 0x540)
    inline uint32_t HitInfoDirection     = 0x2C;
    inline uint32_t HitInfoStartPos      = 0x38;
    // HitInfoHitPoint / HitInfoDistance — offsets ainda não verificados, não usar

    // Sillent/LastAimingInfoFromWeapon — confirmado dump v7a 1.132.1 por segundo agente:
    // Player+0x788 = AEJHONDBJAD (LastAimingInfoFromWeapon, get_LastAimingInfoFromWeapon)
    // Player+0x790 = IACGJCEFEBD (LastAuxAimingInfoFromWeapon, get_LastAuxAimingInfoFromWeapon)
    // CGKJLKPMGDJ tem Vector3 RayDir @ +0x2C e Vector3 StartPos @ +0x38.
    inline uint32_t HitInfoPtr1          = 0x788;  // LastAimingInfoFromWeapon AEJHONDBJAD @ Player+0x788 (dump-confirmed)
    inline uint32_t HitInfoPtr2          = 0x790;  // LastAuxAimingInfoFromWeapon IACGJCEFEBD @ Player+0x790 (dump-confirmed)
    inline uint32_t HitInfoPtr3          = 0x9D0;  // FDMIEDDNCEC CGKJLKPMGDJ @ Player+0x9D0 (sem getter "Last" - candidato INPUT)
    inline uint32_t HitInfoPtr4          = 0x9D4;  // PKIOAMDCOPB CGKJLKPMGDJ @ Player+0x9D4 (sem getter "Last" - candidato INPUT)
    inline uint32_t HitInfoHitPoint      = 0x14;   // HPCOJHGFEEG Vector3 @ CGKJLKPMGDJ+0x14 (hit point - posição no corpo onde o tiro registra)
    // Velocity — offset não verificado; velocidade é derivada de delta de posição no SilentAim
    inline uint32_t PlayerAimRotation    = 0x440;  // atualizado 2026-09-16 (era 0x400) - AimRotation
    inline uint32_t PlayerAuxAimRotation = 0x454;  // atualizado 2026-09-17 (era 0x3B8 - causava crash) - Quaternion GKOPDDOCJMC @ Player+0x454 confirmado dump
    inline uint32_t RootMainNode         = 0x40C;
    inline uint32_t PlayerAttributes     = 0x500;  // atualizado 2026-09-16 (era 0x4BC) - LocalPlayerAttributes
    inline uint32_t EatSpeedScale        = 0x60;   // Single PNLLLKKNBOG @ PlayerAttributes+0x60 (confirmado no dump)
    inline uint32_t FireDuration         = 0x4BC;  // Single NAGGOHBGHKK @ WeaponOnHand+0x4BC (era 0x4B4=UInt32, errado)
    // AimlockInvMgr agora aponta direto ao Weapon (Player.ActiveUISightingWeapon @ 0x434)
    // Em builds antigas havia um UnkPlayerWeaponInfoClass @ 0x4A8 que continha
    // um weaponOnHand @ +0x54, mas no build v7a 1.132.1 esse offset esta em bone
    // node. Solucao: usar Weapon direto (mesmo objeto HBIBDMMOOOK). Os call-sites
    // agora leem Read(lp + AimlockInvMgr) como o weapon final, sem segundo deref.
    inline uint32_t AimlockInvMgr        = 0x434;  // atualizado 2026-09-16 (era 0x4A8) - Weapon direto
    inline uint32_t AimlockItemOnHand    = 0x0;    // OBSOLETO - manter para retrocompatibilidade do codigo; nao usar em novos writes

    // Aimbot Scope — AimAssistOnSighting exploit (parcialmente atualizado 2026-09-16 do dump v7a 1.132.1)
    inline uint32_t Weapon_IsSighting          = 0x5E4;  // NAO CONFIRMADO - HBIBDMMOOOK+0x5E4 no dump e string, nao bool. Feature pode nao funcionar corretamente.
    inline uint32_t Player_AimAssistOnSighting = 0x480;  // atualizado (era 0x43C, que era bool NextFireActionFromAuxButton) - DCIDAEHLHPI NKABFKBIMOI @ Player+0x480 (tipo casa com getter GetAimAssistOnSighting)
    inline uint32_t AimAssist_LerpTime         = 0x44;   // MANTIDO - offset dentro do AimAssist struct (nao verificado no novo dump)
    // WeaponParams (struct embutida em itemOnHand)
    inline uint32_t WeaponParamsOff         = 0x6C;  // WeaponParams
    inline uint32_t WeaponParamsFireInterval = 0x18C; // WeaponParams_FireInterval
    // Offsets verificados (tabela do usuario):
    inline uint32_t LocalPlayerAttrs         = 0x500;  // atualizado 2026-09-16 (era 0x4BC)
    inline uint32_t NoReloadFlag             = 0xC1;   // atualizado 2026-09-16 (era 0x99) - PlayerAttributes+this (byte)
    inline uint32_t ReloadNoConsumeAmmoclip  = 0xC0;   // NAO CONFIRMADO - inferido de NoReloadFlag-1 (delta +0x28)
    inline uint32_t WeaponFireInterval   = 0x88;   // WeaponOnHand + FireInterval (direto, = WeaponParamsOff+WeaponParamsFireInterval)
    inline uint32_t FullDamageDistance   = 0x48;
    inline uint32_t PrefireDelay         = 0x144;
    inline uint32_t WeaponRange          = 0x44;
    // GameVarDef — atualizados 2026-09-16 do dump v7a 1.132.1 (TypeDefIndex 11718)
    inline uint32_t GameVarTIOffset      = 0xA5BC398; // atualizado 2026-09-16 - script.json Address 173786008 COW.GameVarDef_TypeInfo (era 0xABFF734)
    inline uint32_t AccessClass          = 0x5C;    // StaticClass (mantido)
    inline uint32_t ShootTraceThresh     = 0x72C;   // atualizado (era 0x670) - float ShootTraceAdjustmentDistanceThreshold @ 0x72C
    inline uint32_t EnableShootTraceAdj  = 0x728;   // NOVO - bool EnableShootTraceAdjustment @ 0x728
    inline uint32_t RotSensMin           = 0xFD4;   // atualizado (era 0xF0C) - float RotationSensitivityMin
    inline uint32_t RotSensMax           = 0xFD8;   // atualizado (era 0xF10) - float RotationSensitivityMax
    inline uint32_t AimRotSensMin        = 0xFDC;   // atualizado (era 0xF14) - float AimRotationSensitivityMin
    inline uint32_t AimRotSensMax        = 0xFE0;   // atualizado (era 0xF18) - float AimRotationSensitivityMax

    // Bone offsets para LegitAim / BoneSwap.
    // Mesma lógica do projeto antigo (0x45C/0x460), migrados +0x44 para 1.132.1:
    //   old HEADNODE=0x458 → new 0x49C (+0x44) — mesma proporção para todos os campos.
    //   old BoneHip=0x45C        → new 0x4A0   (m_HipNode)
    //   old BoneBloodEffect=0x460 → new 0x4A4  (m_BloodEffectNode)
    // Estes campos são DISTINTOS de HEADNODE/ROOTNODE → swap não afeta GetHeadPosition/GetRootPosition.
    inline uint32_t BoneHip             = 0x4A0;   // m_HipNode — Player+0x4A0
    inline uint32_t BoneBloodEffect     = 0x4A4;   // m_BloodEffectNode — Player+0x4A4 (swap Hip↔Blood = headshot)
    inline uint32_t BoneLeftArm         = 0x4D4;   // visual "esquerda" = RightShoulder (shoulder loops)
    inline uint32_t BoneRightArm        = 0x4D0;   // visual "direita"  = LeftShoulder  (shoulder loops)

    // SpinBot anti-detect - atualizados 2026-09-16 do dump v7a 1.132.1
    inline uint32_t PlayerUserControl      = 0x344;  // atualizado (era 0x30C) - protected UserControlHandler LBPGBNKABJE @ Player+0x344
    inline uint32_t UserControlAxisData    = 0x34;   // atualizado (era 0x30) - UserControlAxisData[] m_AxisData @ UserControlHandler+0x34
    inline uint32_t UserControlMoveAxisIdx = 0x10;   // MANTIDO - Il2CppArray data start offset (arr[0])
    inline uint32_t UserControlIsTouched   = 0x37;   // CONFIRMADO - private bool m_IsTouched @ UserControlAxisData+0x37

    // GhostMode (ForceSync trick)
    inline uint32_t GhostWaitForForceSync   = 0x520;
    inline uint32_t GhostLastForceSyncTick  = 0x6B0;
    inline uint32_t GhostServerForceSync    = 0x129E;
    inline uint32_t GhostServerForceTick    = 0x12A0;

    // New features — PlayerAttributes offsets (TODOS atualizados 2026-09-16 do dump v7a 1.132.1)
    inline uint32_t RunSpeedUpScale          = 0x210;  // atualizado (era 0x1D8) - public float PlayerAttributes.RunSpeedUpScale
    inline uint32_t ForceSetRunAndDashSpeed  = 0x2B8;  // atualizado (era 0x264) - public float PlayerAttributes.ForceSetRunAndDashSpeed
    inline uint32_t DamageAdditionScale      = 0xE8;   // atualizado (era 0xB4) - public float PlayerAttributes.DamageAdditionScale
    inline uint32_t BuffWeaponDamageScale    = 0xF8;   // atualizado (era 0xC4, que era BuffWeaponScatterScale - WRONG) - public float PlayerAttributes.BuffWeaponDamageScale
    inline uint32_t ShowEnermyTargetOnMap    = 0x140;  // atualizado (era 0x108) - public bool PlayerAttributes.ShowEnermyTargetOnMap
    inline uint32_t ShowEnemyFootStep        = 0x142;  // atualizado (era 0x10A) - public bool PlayerAttributes.ShowEnemyFootStep
    inline uint32_t EnemyFootStepMaxDist     = 0x144;  // atualizado (era 0x10C) - public int PlayerAttributes.EnemyFootStepMaxDistanceDelta
    inline uint32_t EnemyFootStepMinDist     = 0x148;  // atualizado (era 0x110) - public int PlayerAttributes.EnemyFootStepMinDistanceDelta
    inline uint32_t IsSuperArmorEnable       = 0x208;  // atualizado (era 0x1D0) - public bool PlayerAttributes.IsSuperArmorEnable
    inline uint32_t InfiniteIceWall          = 0x78;   // NOVO - public bool PlayerAttributes.InfiniteIceWall
    inline uint32_t FallingSpeedUpScale      = 0x20C;  // NOVO - public float PlayerAttributes.FallingSpeedUpScale

    inline uint32_t SuperArmorSkillEffecting     = 0x64;   // atualizado (era 0x3C) - public bool PlayerAttributes.SuperArmorSkillEffecting
    inline uint32_t CanAimassistOnStrop          = 0x70;   // atualizado (era 0x48) - public bool PlayerAttributes.CanAimassistOnStrop
    inline uint32_t GrenadeThrowSpeedScaleDflt   = 0x1B8;  // atualizado (era 0x180) - public float PlayerAttributes.GrenadeThrowSpeedScaleDefault

    // IceWallMakerData (CSVBaseData) — atualizado 2026-09-16 do dump v7a 1.132.1
    inline uint32_t IceWallMakerDataOff      = 0x52C;  // atualizado (era 0x4E4) - private IceWallMakerData ICCOFLNKMCM @ Player+0x52C
    inline uint32_t IceWallLimited           = 0x10;   // MANTIDO - IceWallMakerData+0x10 (nao verificado no novo dump)
    inline uint32_t IceWallChargeMax         = 0x14;   // MANTIDO - IceWallMakerData+0x14 (nao verificado no novo dump)
    inline uint32_t IceWallChargeSpeed       = 0x18;   // MANTIDO - IceWallMakerData+0x18 (nao verificado no novo dump)
    // Backing fields do player logo apos IceWallMakerData - Player+0x530/0x531 no novo layout
    // <KNJEBPMBJJK>k__BackingField (bool @ 0x530) e <PPBHNNFPHJE>k__BackingField (bool @ 0x531)
    inline uint32_t IceWallBool1             = 0x530;  // atualizado (era 0x4E8) - bool @ Player+0x530
    inline uint32_t IceWallBool2             = 0x531;  // atualizado (era 0x4E9) - bool @ Player+0x531

    // FireData (EDDCGAGJBLO) embutida em WeaponOnHand+0x6C
    // Acesso: lp+AimlockInvMgr -> invMgr+AimlockItemOnHand -> weaponOnHand+FireDataBaseDmg
    inline uint32_t FireDataBaseDmg          = 0x6C;   // WeaponOnHand+0x6C = int32 BGKGJKDILEA (BaseDamage)

    // Habilidades de personagens — atualizados 2026-09-16 do dump v7a 1.132.1
    // Kelly Dash — RunSpeedUpScale (0x210) ja existe acima
    // Maxim StrongMedicine
    inline uint32_t ExtraHP = 0x7C;   // MANTIDO 0x7C - float obfuscado no dump (era declarado Int32, dump mostra float) — verificar
    inline uint32_t FireIntervalScale        = 0x1C4;  // atualizado (era 0x184) - Single @ PA+0x1C4 (CILIDJHLLJA = m_FireIntervalScale, Offsets.h autoridade)
    inline uint32_t BuffWeaponMoveSpeedScale = 0x100;  // atualizado (era 0xCC) - public float PlayerAttributes.BuffWeaponMoveSpeedScale
    inline uint32_t ShowEnermyTargetOnHud    = 0x141;  // atualizado (era 0x109) - public bool PlayerAttributes.ShowEnermyTargetOnHud
    inline uint32_t EnemyFootStepLimitNum    = 0x14C;  // atualizado (era 0x114) - public int PlayerAttributes.EnemyFootStepLimitNumDelta

    // AlokBuff — BuffECASystem AllState scale
    inline uint32_t AttrBuffDict           = 0x24;
    constexpr int      BuffAllStateKey        = 15;
    inline uint32_t BuffScaleField         = 0x8;
    inline uint32_t BuffDurationCandA      = 0x10;
    inline uint32_t BuffDurationCandB      = 0x14;
    inline uint32_t BuffDurationCandC      = 0x18;
    inline uint32_t BuffDictEntries        = 0xC;
    inline uint32_t BuffDictSize           = 0x10;
    inline uint32_t BuffDictArrayBase      = 0x10;
    inline uint32_t BuffDictElementStride  = 0x10;
    inline uint32_t BuffDictEntityOffset   = 0xC;
    // ---- Registro runtime (aba Offsets do website). ----
    const std::vector<OffEntry>& OffTable()
    {
        static const std::vector<OffEntry> t = {
            { "MatchEntityDict", &MatchEntityDict },
            { "DictSize", &DictSize },
            { "DictEntries", &DictEntries },
            { "Weapon", &Weapon },
            { "FireComponent", &FireComponent },
            { "TangentTheta", &TangentTheta },
            { "EntityIsFiring", &EntityIsFiring },
            { "HitInfoDirection", &HitInfoDirection },
            { "HitInfoStartPos", &HitInfoStartPos },
            { "HitInfoPtr1", &HitInfoPtr1 },
            { "HitInfoPtr2", &HitInfoPtr2 },
            { "HitInfoPtr3", &HitInfoPtr3 },
            { "HitInfoPtr4", &HitInfoPtr4 },
            { "HitInfoHitPoint", &HitInfoHitPoint },
            { "PlayerAimRotation", &PlayerAimRotation },
            { "PlayerAuxAimRotation", &PlayerAuxAimRotation },
            { "RootMainNode", &RootMainNode },
            { "PlayerAttributes", &PlayerAttributes },
            { "EatSpeedScale", &EatSpeedScale },
            { "FireDuration", &FireDuration },
            { "AimlockInvMgr", &AimlockInvMgr },
            { "AimlockItemOnHand", &AimlockItemOnHand },
            { "Weapon_IsSighting", &Weapon_IsSighting },
            { "Player_AimAssistOnSighting", &Player_AimAssistOnSighting },
            { "AimAssist_LerpTime", &AimAssist_LerpTime },
            { "WeaponParamsOff", &WeaponParamsOff },
            { "WeaponParamsFireInterval", &WeaponParamsFireInterval },
            { "LocalPlayerAttrs", &LocalPlayerAttrs },
            { "NoReloadFlag", &NoReloadFlag },
            { "ReloadNoConsumeAmmoclip", &ReloadNoConsumeAmmoclip },
            { "WeaponFireInterval", &WeaponFireInterval },
            { "FullDamageDistance", &FullDamageDistance },
            { "PrefireDelay", &PrefireDelay },
            { "WeaponRange", &WeaponRange },
            { "GameVarTIOffset", &GameVarTIOffset },
            { "AccessClass", &AccessClass },
            { "ShootTraceThresh", &ShootTraceThresh },
            { "EnableShootTraceAdj", &EnableShootTraceAdj },
            { "RotSensMin", &RotSensMin },
            { "RotSensMax", &RotSensMax },
            { "AimRotSensMin", &AimRotSensMin },
            { "AimRotSensMax", &AimRotSensMax },
            { "BoneHip", &BoneHip },
            { "BoneBloodEffect", &BoneBloodEffect },
            { "BoneLeftArm", &BoneLeftArm },
            { "BoneRightArm", &BoneRightArm },
            { "PlayerUserControl", &PlayerUserControl },
            { "UserControlAxisData", &UserControlAxisData },
            { "UserControlMoveAxisIdx", &UserControlMoveAxisIdx },
            { "UserControlIsTouched", &UserControlIsTouched },
            { "GhostWaitForForceSync", &GhostWaitForForceSync },
            { "GhostLastForceSyncTick", &GhostLastForceSyncTick },
            { "GhostServerForceSync", &GhostServerForceSync },
            { "GhostServerForceTick", &GhostServerForceTick },
            { "RunSpeedUpScale", &RunSpeedUpScale },
            { "ForceSetRunAndDashSpeed", &ForceSetRunAndDashSpeed },
            { "DamageAdditionScale", &DamageAdditionScale },
            { "BuffWeaponDamageScale", &BuffWeaponDamageScale },
            { "ShowEnermyTargetOnMap", &ShowEnermyTargetOnMap },
            { "ShowEnemyFootStep", &ShowEnemyFootStep },
            { "EnemyFootStepMaxDist", &EnemyFootStepMaxDist },
            { "EnemyFootStepMinDist", &EnemyFootStepMinDist },
            { "IsSuperArmorEnable", &IsSuperArmorEnable },
            { "InfiniteIceWall", &InfiniteIceWall },
            { "FallingSpeedUpScale", &FallingSpeedUpScale },
            { "SuperArmorSkillEffecting", &SuperArmorSkillEffecting },
            { "CanAimassistOnStrop", &CanAimassistOnStrop },
            { "GrenadeThrowSpeedScaleDflt", &GrenadeThrowSpeedScaleDflt },
            { "IceWallMakerDataOff", &IceWallMakerDataOff },
            { "IceWallLimited", &IceWallLimited },
            { "IceWallChargeMax", &IceWallChargeMax },
            { "IceWallChargeSpeed", &IceWallChargeSpeed },
            { "IceWallBool1", &IceWallBool1 },
            { "IceWallBool2", &IceWallBool2 },
            { "FireDataBaseDmg", &FireDataBaseDmg },
            { "ExtraHP", &ExtraHP },
            { "FireIntervalScale", &FireIntervalScale },
            { "BuffWeaponMoveSpeedScale", &BuffWeaponMoveSpeedScale },
            { "ShowEnermyTargetOnHud", &ShowEnermyTargetOnHud },
            { "EnemyFootStepLimitNum", &EnemyFootStepLimitNum },
            { "AttrBuffDict", &AttrBuffDict },
            { "BuffScaleField", &BuffScaleField },
            { "BuffDurationCandA", &BuffDurationCandA },
            { "BuffDurationCandB", &BuffDurationCandB },
            { "BuffDurationCandC", &BuffDurationCandC },
            { "BuffDictEntries", &BuffDictEntries },
            { "BuffDictSize", &BuffDictSize },
            { "BuffDictArrayBase", &BuffDictArrayBase },
            { "BuffDictElementStride", &BuffDictElementStride },
            { "BuffDictEntityOffset", &BuffDictEntityOffset },
        };
        return t;
    }
}

namespace AimModules
{
    ExploitDebugState g_ExploitDbg;

    std::atomic<bool>     bRunning{ false };
    std::atomic<uint32_t> s_SilentTarget{ 0 };  // atualizado por SilentTargetScanThread
    std::atomic<uint32_t> g_bsEntity{ 0 };
    std::atomic<uint32_t> g_bsSavedHip{ 0 };
    std::atomic<uint32_t> g_bsSavedBlood{ 0 };

    // GhostMode state (see AimModules.hpp for how Draw.cpp consumes these)
    std::atomic<bool>     g_GhostActive{ false };

    // Ghost snapshot (pos + 18 bones + validity) is protected by a mutex.
    // TickGhostMode writes it on the rising edge; Draw reads it every
    // frame. Same rationale as before — a plain Vector3 assignment is not
    // atomic, and a 19-Vector3 struct even less so.
    namespace {
        std::mutex     g_GhostMx;
        GhostSnapshot  g_GhostStorage{};

        // Bone slot tables — kept in sync with SharedFrame.cpp's
        // kMaleSlot/kFemaleSlot (same layout, same 18-bone semantic order).
        // See the enum in Draw.cpp (BONE_HEAD..BONE_RFOOT, 0..17).
        constexpr int kGhostMaleSlot[18] = {
            (0x38-0x10)/4, (0x14-0x10)/4, (0x10-0x10)/4, (0x48-0x10)/4,
            (0x18-0x10)/4, (0x1C-0x10)/4, (0x20-0x10)/4, (0x24-0x10)/4,
            (0x28-0x10)/4, (0x2C-0x10)/4, (0x30-0x10)/4, (0x34-0x10)/4,
            (0x3C-0x10)/4, (0x40-0x10)/4, (0x44-0x10)/4,
            (0x4C-0x10)/4, (0x50-0x10)/4, (0x54-0x10)/4,
        };
        constexpr int kGhostFemaleSlot[18] = {
            (0x3C-0x10)/4, (0x18-0x10)/4, (0x14-0x10)/4, (0x10-0x10)/4,
            (0x1C-0x10)/4, (0x20-0x10)/4, (0x24-0x10)/4, (0x28-0x10)/4,
            (0x2C-0x10)/4, (0x30-0x10)/4, (0x34-0x10)/4, (0x38-0x10)/4,
            (0x40-0x10)/4, (0x44-0x10)/4, (0x48-0x10)/4,
            (0x4C-0x10)/4, (0x50-0x10)/4, (0x54-0x10)/4,
        };

        // Walks the same bone chain the ESP producer uses (see FillSkeleton
        // in SharedFrame.cpp): Entity+0x760 → +0x8 → +slot → H1(+0x8) →
        // H2(+0x28) → H3(+0x14) → +0x60 = Vector3 world pos. Fills the
        // 18 slots in `out.bones` and sets `out.skelValid`.
        void CaptureLocalSkeleton(uint32_t lp, GhostSnapshot& out) {
            out.skelValid = false;
            for (int i = 0; i < 18; ++i) out.bones[i] = Vector3::Zero();

            uint32_t capsule = g_Memory->Read<uint32_t>(lp + 0x7B4);  // atualizado 2026-09-16 (era 0x760) - List<CapsuleCollider> @ Player+0x7B4
            if (!capsule) return;
            uint32_t transformPtr = g_Memory->Read<uint32_t>(capsule + 0x8);
            if (!transformPtr) return;

            uint32_t locBlock[18] = {};
            if (!g_Memory->ReadBytes(transformPtr + 0x10, locBlock, sizeof(locBlock))) return;

            // IsFemale nao tem backing field em Player (v7a 1.132.1) — sempre male slots
            const int* slots = kGhostMaleSlot;

            for (int b = 0; b < 18; ++b) {
                uint32_t Location = locBlock[slots[b]];
                if (!Location) continue;
                uint32_t H1 = g_Memory->Read<uint32_t>(Location + 0x8);
                if (!H1) continue;
                uint32_t H2 = g_Memory->Read<uint32_t>(H1 + 0x28);
                if (!H2) continue;
                uint32_t H3 = g_Memory->Read<uint32_t>(H2 + 0x14);
                if (!H3) continue;
                out.bones[b] = g_Memory->Read<Vector3>(H3 + 0x60);
            }
            out.skelValid = true;
        }
    }
    Vector3 GetGhostPos() {
        std::lock_guard<std::mutex> lk(g_GhostMx);
        return g_GhostStorage.pos;
    }
    void SetGhostPos(const Vector3& p) {
        std::lock_guard<std::mutex> lk(g_GhostMx);
        g_GhostStorage.pos = p;
    }
    GhostSnapshot GetGhostSnapshot() {
        std::lock_guard<std::mutex> lk(g_GhostMx);
        return g_GhostStorage;
    }

    std::atomic<int>      s_liveThreads{ 0 };

    struct ThreadCounterGuard {
        ThreadCounterGuard()  {
            s_liveThreads.fetch_add(1, std::memory_order_acq_rel);
            AntiDbg::HideThread();  // esconde esta thread do debugger
        }
        ~ThreadCounterGuard() { s_liveThreads.fetch_sub(1, std::memory_order_acq_rel); }
    };

    struct Quaternion { float x, y, z, w; };

    // ── Helpers ─────────────────────────────────────────────────────────────────
    //
    // TODOS os helpers de "leitura de base" (LocalPlayer, ViewMatrix, Dict,
    // FindBestTarget) leem do SharedFrame.
    //
    // Antes, cada uma das ~16 threads do cheat refazia a mesma cadeia de RPMs
    // no seu próprio ritmo. Com o SharedFrame, um único producer thread
    // (Cheat::Shared::Producer) mantém tudo cacheado em RAM local e todos
    // esses helpers viram lookups ~zero-cost. Os call-sites de cada aim
    // loop continuam idênticos — só a implementação por baixo mudou.
    //
    // Fallback: se por algum motivo o snapshot ainda não estiver pronto
    // (Producer aquecendo, jogo saiu de partida), os helpers refazem o RPM
    // legado — mantém o comportamento seguro no worst case.

    static uint32_t GetLocalPlayer(uint32_t il2cpp) {
        // Fast path: snapshot pronto → LocalPlayer sem RPM.
        if ( const auto* snap = Cheat::Shared::Producer::Get( ).Latest( ) )
            if ( snap->valid && snap->localPlayer ) return snap->localPlayer;

        // Fallback: cadeia RPM legada.
        if (!il2cpp) return 0;
        auto b = g_Memory->Read<uint32_t>(il2cpp + Cheat::g_DrawOff.BASEGF); if (!b) return 0;
        auto f = g_Memory->Read<uint32_t>(b + Cheat::g_DrawOff.GAMEFACADE);  if (!f) return 0;
        auto s = g_Memory->Read<uint32_t>(f + Cheat::g_DrawOff.STATICGF);    if (!s || s < 0x10000) return 0;
        auto c = g_Memory->Read<uint32_t>(s + Cheat::g_DrawOff.CURRENTMATCH);if (!c) return 0;
        auto m = g_Memory->Read<uint32_t>(c + Cheat::g_DrawOff.MATCHGAME);   if (!m) return 0;
        return g_Memory->Read<uint32_t>(m + Cheat::g_DrawOff.LOCALPLAYER);
    }

    struct DictInfo {
        uint32_t CurrentMatchGame=0, entitiesBase=0, entityCount=0, dictStride=0, dictEntOff=0;
        bool valid=false;
    };
    static DictInfo GetDict(uint32_t il2cpp) {
        // Fast path.
        if ( const auto* snap = Cheat::Shared::Producer::Get( ).Latest( ) ) {
            if ( snap->valid && snap->dictionary && snap->entitiesBase ) {
                DictInfo d;
                d.CurrentMatchGame = snap->currentMatchGame;
                d.entitiesBase     = snap->entitiesBase;
                d.entityCount      = snap->entityCount;
                d.dictStride       = Cheat::g_DrawOff.DICTSTRIDE;
                d.dictEntOff       = Cheat::g_DrawOff.DICTENTOFF;
                d.valid            = true;
                return d;
            }
        }

        // Fallback.
        DictInfo d;
        auto b=g_Memory->Read<uint32_t>(il2cpp+Cheat::g_DrawOff.BASEGF); if(!b) return d;
        auto f=g_Memory->Read<uint32_t>(b+Cheat::g_DrawOff.GAMEFACADE);  if(!f) return d;
        auto s=g_Memory->Read<uint32_t>(f+Cheat::g_DrawOff.STATICGF);    if(!s||s<0x10000) return d;
        d.CurrentMatchGame=g_Memory->Read<uint32_t>(s+Cheat::g_DrawOff.CURRENTMATCH); if(!d.CurrentMatchGame) return d;
        auto dict=g_Memory->Read<uint32_t>(d.CurrentMatchGame+Cheat::g_DrawOff.DICTIONARY); if(!dict) return d;
        d.entityCount  = g_Memory->Read<uint32_t>(dict+Cheat::g_DrawOff.ENTITYCOUNT);
        d.entitiesBase = g_Memory->Read<uint32_t>(dict+Cheat::g_DrawOff.DICTVALUES)+Cheat::g_DrawOff.DICTBASE;
        d.dictStride   = Cheat::g_DrawOff.DICTSTRIDE;
        d.dictEntOff   = Cheat::g_DrawOff.DICTENTOFF;
        d.valid = true;
        return d;
    }

    static Matrix4x4 GetViewMatrix(uint32_t lp) {
        // Fast path.
        if ( const auto* snap = Cheat::Shared::Producer::Get( ).Latest( ) )
            if ( snap->hasView && snap->localPlayer == lp )
                return snap->viewMatrix;

        // Fallback.
        Matrix4x4 m{};
        auto fc=g_Memory->Read<uint32_t>(lp+Cheat::g_DrawOff.FOLLOWCAM);   if(!fc) return m;
        auto cam=g_Memory->Read<uint32_t>(fc+Cheat::g_DrawOff.CAMERA);     if(!cam) return m;
        auto pr=g_Memory->Read<uint32_t>(cam+Cheat::g_DrawOff.POSTRENDER); if(!pr) return m;
        return g_Memory->Read<Matrix4x4>(pr+Cheat::g_DrawOff.VIEWMATRIX);
    }

    static uint32_t FindBestTarget(uint32_t localPlayer, const DictInfo& d, Vector3 localPos,
                                    Matrix4x4 viewMatrix, float fovPx)
    {
        // Fast path: usa a lista pré-scaneada + screen coords do snapshot.
        // Zero RPMs — mesmo com 5 threads chamando FindBestTarget em ritmos
        // diferentes, todas leem do MESMO scan que o producer fez uma vez.
        if ( const auto* snap = Cheat::Shared::Producer::Get( ).Latest( ) )
            if ( snap->valid && snap->hasView )
                return Cheat::Shared::Producer::FindBestTarget( snap , fovPx );

        // Fallback: scan legado com RPM por entidade.
        uint32_t best = 0;
        float bestSD  = 9999.f;
        for (int i = 0; i < (int)d.entityCount && i < 512; i++) {
            auto entity = g_Memory->Read<uint32_t>(d.entitiesBase + i * d.dictStride + d.dictEntOff);
            if (!entity) continue;
            if (entity == localPlayer) continue;
            auto avatarMgr = g_Memory->Read<uint32_t>(entity + Cheat::g_DrawOff.AVATARMGR);
            if (!avatarMgr) continue;
            auto avatar = g_Memory->Read<uint32_t>(avatarMgr + Cheat::g_DrawOff.AVATAR);
            if (!avatar) continue;
            auto avatarData = g_Memory->Read<uint32_t>(avatar + Cheat::g_DrawOff.AVATARDATA);
            if (!avatarData) continue;
            bool isVisible     = g_Memory->Read<bool>(avatar + Cheat::g_DrawOff.ISVISIBLE);
            int  transformType = g_Memory->Read<int>(entity + Cheat::g_DrawOff.TRANSFORMTYPE);
            if (!isVisible && transformType == 0) continue;
            // TeamCheck gate — só filtra teammate quando a checkbox está ON.
            // Default OFF = aim mira em qualquer player (inclui teammate),
            // consistente com o filtro do ESP em SharedFrame.cpp.
            if ( g_Options.Visuals.ESP.Players.TeamCheck ) {
                bool isTeam = g_Memory->Read<bool>(avatarData + Cheat::g_DrawOff.ISTEAM);
                if (isTeam) continue;
            }
            auto ipri   = g_Memory->Read<uint32_t>(entity + Cheat::g_DrawOff.IPRIDATAPOOL);
            if (!ipri) continue;
            auto mdatas = g_Memory->Read<uint32_t>(ipri + Cheat::g_DrawOff.MDATAS);
            if (!mdatas) continue;
            auto hrd    = g_Memory->Read<uint32_t>(mdatas + Cheat::g_DrawOff.HEALTHREP);
            if (!hrd) continue;
            int hp = g_Memory->Read<int>(hrd + Cheat::g_DrawOff.REPDATA);
            if (hp <= 0) continue;
            Vector3 headPos = Transform::GetHeadPosition(entity);
            if (headPos == Vector3::Zero()) continue;
            Vector3 screen = W2S::World2Screen(viewMatrix, headPos);
            if (screen.Z <= 0.f) continue;
            float dx = screen.X - ScreenWidth  * 0.5f;
            float dy = screen.Y - ScreenHeight * 0.5f;
            float sd = sqrtf(dx * dx + dy * dy);
            if (fovPx > 0.f && sd > fovPx) continue;
            if (sd < bestSD) { bestSD = sd; best = entity; }
        }
        return best;
    }

    static Quaternion LookAt(Vector3 targetPos, Vector3 from) {
        float dx  = targetPos.X - from.X;
        float dy  = targetPos.Y - from.Y;
        float dz  = targetPos.Z - from.Z;
        float yaw = atan2f(dx, dz);
        float pit = -atan2f(dy, sqrtf(dx * dx + dz * dz));
        float cy = cosf(yaw * 0.5f), sy = sinf(yaw * 0.5f);
        float cp = cosf(pit * 0.5f), sp = sinf(pit * 0.5f);
        return { cy * sp, sy * cp, -sy * sp, cy * cp };
    }

    static uint32_t GetGameVar(uint32_t il2cpp) {
        uint32_t ti=g_Memory->Read<uint32_t>(il2cpp+Off::GameVarTIOffset); if(!ti) return 0;
        return g_Memory->Read<uint32_t>(ti+Off::AccessClass);
    }

    // ═══════════════════════════════════════════════════════════════════════════

    void NoRecoilLoop(uint32_t il2cpp)
    {
        // DGHJEPHMGPO (ptr em weapon+0x58) — escreve 0.0f em dois offsets:
        //   +0x0C = IHCKHAFILED float em MBJCPLOAJJA / bool em outros (abordagem da referência)
        //   +0x10 = CFCNBJJLEKE float em MLAEIFDLICK/GGJNCCFNAAL/NIPIBLMPCNN/BGGDKAGBAJK
        // Cobre todos os subclasses. Referência não usa LBNIHCJFPGI.
        float    orig0 = -1.f, orig1 = -1.f;
        uint32_t lastFc = 0;

        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.NoRecoil) {
                if (lastFc) {
                    if (orig0 >= 0.f) g_Memory->Write<float>(lastFc + 0x0C, orig0);
                    if (orig1 >= 0.f) g_Memory->Write<float>(lastFc + Off::TangentTheta, orig1);
                    orig0 = orig1 = -1.f; lastFc = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t weapon = g_Memory->Read<uint32_t>(lp + Off::Weapon);
                if (weapon) {
                    uint32_t fc = g_Memory->Read<uint32_t>(weapon + Off::FireComponent);
                    if (fc) {
                        if (orig0 < 0.f || fc != lastFc) {
                            orig0 = g_Memory->Read<float>(fc + 0x0C);
                            orig1 = g_Memory->Read<float>(fc + Off::TangentTheta);
                            lastFc = fc;
                        }
                        g_Memory->Write<float>(fc + 0x0C, 0.f);
                        g_Memory->Write<float>(fc + Off::TangentTheta, 0.f);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void AimBotMemoryLoop(uint32_t il2cpp)
    {
        // Porta fiel do AimBotLoop do WithoutCoffeBRDMA (RageAimbotModule.cs).
        //
        //   prevQuat  — último quaternion escrito por NÓS (rastreado internamente,
        //               NÃO relido do jogo). Slerp sempre parte daí.
        //   prevQOk   — false = próxima write deve ser snap direto (sem slerp).
        //               Resetado ao trocar/perder alvo, toggle OFF ou bind solto.
        //   lastTick  — tick da última write (DWORD, ms). Usado pro dt da slerp
        //               exponencial: t = 1 − exp(−dt × rate), rate = 100/smooth.
        //   prevTarget — addr do alvo anterior para detectar troca de alvo.
        //
        // Diferença vs abordagem de ler "cur" do jogo: o C# nunca lê o campo
        // de volta — slerpa de prevQuat (o que ELE escreveu) para targetQ.
        // Isso garante trajetória suave e previsível sem depender do valor
        // atual do jogo (que pode estar desatualizado entre ticks de sync).
        uint32_t prevTarget = 0;
        Quaternion prevQuat = { 0.f, 0.f, 0.f, 1.f }; // identity
        bool     prevQOk    = false;
        DWORD    lastTick   = 0;

        // Helper: slerp quaternion idêntico ao Quaternion.Slerp do C#
        auto QuatSlerp = [](const Quaternion& a, Quaternion b, float t) -> Quaternion {
            float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
            if (dot < 0.0f) { b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w; dot=-dot; }
            Quaternion r;
            if (dot > 0.9995f) {
                r.x = a.x + t*(b.x-a.x); r.y = a.y + t*(b.y-a.y);
                r.z = a.z + t*(b.z-a.z); r.w = a.w + t*(b.w-a.w);
            } else {
                float th0 = acosf(dot);
                float th  = th0 * t;
                float s1  = sinf(th) / sinf(th0);
                float s0  = cosf(th) - dot * s1;
                r.x = s0*a.x + s1*b.x; r.y = s0*a.y + s1*b.y;
                r.z = s0*a.z + s1*b.z; r.w = s0*a.w + s1*b.w;
            }
            float n = sqrtf(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
            if (n > 0.0f) { r.x/=n; r.y/=n; r.z/=n; r.w/=n; }
            return r;
        };

        while (bRunning) {
            EMemory::BeginFrameRead();
            try {
                if (!g_Options.AimDMA.AimBotMemory) {
                    prevTarget = 0; prevQOk = false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue;
                }

                int bind = g_Options.AimDMA.AimBotMemoryBind;
                if (bind > 0 && !(GetAsyncKeyState(bind) & 0x8000)) {
                    prevTarget = 0; prevQOk = false;
                    Cheat::HiRes::SleepMillis(5); continue;
                }

                uint32_t lp = GetLocalPlayer(il2cpp);
                if (!lp) { Cheat::HiRes::SleepMillis(5); continue; }

                auto localTf = g_Memory->Read<uint32_t>(lp + Cheat::g_DrawOff.TRANSFORM);
                if (!localTf) { Cheat::HiRes::SleepMillis(5); continue; }
                Vector3 localPos = Transform::get_position_Injected(localTf);
                if (localPos == Vector3::Zero()) { Cheat::HiRes::SleepMillis(5); continue; }

                Matrix4x4 vm = GetViewMatrix(lp);
                if (vm.m[0][0] == 0.f) { Cheat::HiRes::SleepMillis(5); continue; }

                DictInfo d = GetDict(il2cpp);
                if (!d.valid) { Cheat::HiRes::SleepMillis(5); continue; }

                uint32_t best = FindBestTarget(lp, d, localPos, vm, g_Options.AimDMA.AimBotMemoryFov);

                if (best) {
                    // Troca de alvo → força snap (mesmo comportamento do C#:
                    // prevQOk = false quando bestEnt != prevTarget).
                    if (best != prevTarget) {
                        prevQOk    = false;
                        prevTarget = best;
                    }

                    Vector3 head = Transform::GetHeadPosition(best);
                    if (head == Vector3::Zero()) {
                        Cheat::HiRes::SleepMillis(2);
                        continue;
                    }
                    head.Y += 0.1f;
                    Quaternion targetQ = LookAt(head, localPos);
                    float n = targetQ.x*targetQ.x + targetQ.y*targetQ.y +
                              targetQ.z*targetQ.z + targetQ.w*targetQ.w;
                    if (n > 0.5f && n < 1.5f) {
                        int smooth = g_Options.AimDMA.AimBotSmooth;

                        Quaternion writeQ;
                        if (smooth <= 0 || !prevQOk) {
                            // Snap direto: sem smooth ou primeira iter do alvo.
                            writeQ = targetQ;
                        } else {
                            // Slerp exponencial — porta exata do C#:
                            //   dt   = tempo desde o último write (s)
                            //   rate = 100 / smooth  (quanto mais suave, menor o rate)
                            //   t    = 1 − exp(−dt × rate)   [0..1]
                            DWORD now = GetTickCount();
                            float dt  = (lastTick != 0) ? (float)(now - lastTick) * 0.001f : 0.001f;
                            if (dt <= 0.f) dt = 0.001f;
                            float rate = 100.0f / (float)smooth;
                            float t    = 1.0f - expf(-dt * rate);
                            if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
                            writeQ = QuatSlerp(prevQuat, targetQ, t);
                        }

                        // Escreve em PlayerAimRotation (0x440) e PlayerAuxAimRotation (0x454),
                        // ambos Quaternion confirmados pelo dump v7a 1.132.1.
                        g_Memory->Write<Quaternion>(lp + Off::PlayerAimRotation,    writeQ);
                        g_Memory->Write<Quaternion>(lp + Off::PlayerAuxAimRotation, writeQ);

                        prevQuat = writeQ;  // rastreia internamente (não relê do jogo)
                        prevQOk  = true;
                        lastTick = GetTickCount();
                    }
                } else {
                    prevTarget = 0; prevQOk = false;
                }
            } catch (...) {}
            Cheat::HiRes::SleepMillis(8);
        }
    }

    void SilentAimLoop( uint32_t il2cpp ) {
        // HIGHEST (não TIME_CRITICAL). HIGHEST ainda dá preferência sobre a
        // maioria das threads user-mode mas não preempta as threads TIME_CRITICAL
        // do jogo/anti-cheat, evitando dead-lock em scheduler saturado. Silent
        // de referência usa HIGHEST e mantém 100% hit rate — provou ser suficiente.
        SetThreadPriority( GetCurrentThread( ), THREAD_PRIORITY_HIGHEST );
        UINT32 rngState = (UINT32)__rdtsc();
        if ( !rngState ) rngState = 0xDEADBEEF;
        auto RngNext = [&]() -> UINT32 {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            return rngState;
        };
        auto RngFloat = [&]() -> float {
            return (float)( RngNext() & 0x00FFFFFF ) / (float)0x01000000;
        };

        constexpr float SMOOTH_MIN     = 0.0015f;
        constexpr float SMOOTH_MAX     = 0.0035f;
        // Centro da hitbox da cabeça (~7cm acima do head bone)
        constexpr float HEAD_Y_OFFSET  = 0.07f;
        // Lead total = DMA latency (~5ms) + tick do jogo (~16ms) + server (~45ms)
        constexpr float PREDICT_LEAD_S = 0.065f;
        constexpr float MAX_VEL        = 25.0f;   // m/s — cobre jump/slide sem descartar movimento válido

        auto validHi = []( uint32_t h ) { return h >= 0x10000000u && h <= 0xEF000000u; };

        // ── Estado persistente (sem dead gap entre outer-loop iters) ──────────
        uint32_t cLp     = 0;
        uint32_t cHi1 = 0, cHi2 = 0, cHi3 = 0, cHi4 = 0;
        uint32_t cTarget = 0;
        Vector3  cAimPos = {};
        Vector3  cAimAlt = {};
        bool     posValid = false;

        // Counter global de writes — usado pelo modo mix (target=2) pra alternar
        // entre HEAD e HIP conforme SilentChestRate:SilentHeadRate. Persiste
        // entre bursts pra que o rate seja consistente ao longo do tempo.
        uint32_t writeCounter = 0;

        // Cache do EntityIsFiring — refresh 5ms (200Hz, muito acima de qualquer
        // trigger pull humano). Evita RPM por iter do outer loop.
        bool  cIsFiring = false;
        DWORD tFire     = 0;

        // Predição: sempre calcula velocidade a partir da posição RAW (bug anterior
        // salvava a posição já com o lead aplicado, causando drift na velocidade)
        Vector3 prevRawHead  = {};
        DWORD   prevHeadTick = 0;
        Vector3 smoothVel    = {};

        DWORD tLp     = 0;
        DWORD tHi     = 0;
        DWORD tPos    = 0;
        Vector3 cPlayerPos = {};
        DWORD   tPlayerPos = 0;

        while ( bRunning ) {
            EMemory::BeginFrameRead(); // invalida cache thread-local — leituras de cHi/sp ficam frescas
            try {
                if ( !g_Options.AimDMA.SilentAim ) {
                    cLp = 0; cHi1 = 0; cHi2 = 0; cHi3 = 0; cHi4 = 0; cTarget = 0;
                    cAimPos = Vector3{}; cAimAlt = Vector3{};
                    prevRawHead = Vector3{}; smoothVel = Vector3{};
                    prevHeadTick = 0; posValid = false;
                    cIsFiring = false;
                    Cheat::HiRes::SleepMillis( 3 );
                    continue;
                }

                {
                    int bind = g_Options.AimDMA.SilentAimBind;
                    if ( bind != 0 && ( ( GetAsyncKeyState( bind ) & 0x8000 ) == 0 ) ) {
                        cLp = 0; cHi1 = 0; cHi2 = 0; cHi3 = 0; cHi4 = 0; cTarget = 0;
                        cAimPos = Vector3{}; prevRawHead = Vector3{}; smoothVel = Vector3{};
                        prevHeadTick = 0; posValid = false;
                        Cheat::HiRes::SleepMillis( 3 );
                        continue;
                    }
                }

                DWORD now = GetTickCount();

                // [A] LP — a cada 50ms; target vem do SilentTargetScanThread (sem stall de FindBestTarget)
                if ( !cLp || now - tLp >= 50 ) {
                    cLp = GetLocalPlayer( il2cpp );
                    tLp = now;
                }
                if ( !cLp ) { Cheat::HiRes::SleepMillis( 1 ); continue; }

                // [A2] Posição do player — refresh 50ms
                if ( now - tPlayerPos >= 50 ) {
                    uint32_t ltf = g_Memory->Read<uint32_t>( cLp + Cheat::g_DrawOff.TRANSFORM );
                    if ( ltf ) {
                        Vector3 p = Transform::get_position_Injected( ltf );
                        if ( p.X != 0.f || p.Y != 0.f || p.Z != 0.f ) { cPlayerPos = p; tPlayerPos = now; }
                    }
                }

                // ── Fire gate: se o usuário NÃO configurou bind (bind=0), gate
                // por EntityIsFiring do LP. Só escreve enquanto o player está
                // realmente atirando. Corta a footprint de "silent escrevendo
                // 66k writes/s idle" que anti-cheats podem detectar via
                // heuristic. Se bind>0, o gate do bind já cuida (bloco acima).
                //
                // Refresh a 5ms (200Hz) — bem acima da taxa de trigger pull
                // humano (~5 Hz max), mas 20× menos RPMs que ler todo iter.
                // EntityIsFiring esta em weapon+0x1C8 (dentro de LBNIHCJFPGI), nao em lp.
                if ( g_Options.AimDMA.SilentAimBind == 0 && g_Options.AimDMA.SilentFireGate ) {
                    if ( now - tFire >= 5 ) {
                        uint32_t wFire = g_Memory->Read<uint32_t>( cLp + Off::Weapon );
                        cIsFiring = wFire ? g_Memory->Read<bool>( wFire + Off::EntityIsFiring ) : false;
                        tFire = now;
                    }
                    if ( !cIsFiring ) { Cheat::HiRes::SleepMillis( 2 ); continue; }
                }

                // Atualiza cTarget a partir do scan thread dedicado (sem custo aqui)
                {
                    uint32_t newT = s_SilentTarget.load( std::memory_order_relaxed );
                    if ( newT != cTarget ) {
                        prevRawHead = Vector3{}; prevHeadTick = 0; smoothVel = Vector3{};
                        posValid = false; cTarget = newT;
                    }
                }

                if ( !cTarget ) { Cheat::HiRes::SleepMillis( 1 ); continue; }

                // [B] HitInfo ptrs
                if ( ( !validHi( cHi1 ) && !validHi( cHi2 ) ) || now - tHi >= 5 ) {
                    cHi1 = g_Memory->Read<uint32_t>( cLp + Off::HitInfoPtr1 );
                    cHi2 = g_Memory->Read<uint32_t>( cLp + Off::HitInfoPtr2 );
                    cHi3 = g_Memory->Read<uint32_t>( cLp + Off::HitInfoPtr3 );
                    cHi4 = g_Memory->Read<uint32_t>( cLp + Off::HitInfoPtr4 );
                    tHi  = now;
                }

                if ( !validHi( cHi1 ) && !validHi( cHi2 ) && !validHi( cHi3 ) && !validHi( cHi4 ) ) { Cheat::HiRes::SleepMillis( 1 ); continue; }

                // [C] AimPos (ponto de mira do alvo) — a cada 1ms (1000Hz).
                // Era 4ms/250Hz — a 25m/s (max sprint/slide/bhop), alvo movia
                // até 10cm entre refreshes = fora da hitbox lateral da cabeça.
                // Agora move só 2.5cm max = sempre dentro. Custo extra: ~10-25
                // RPMs adicionais / ms, todos cached (sub-microssegundo cada).
                // sp NÃO é lido aqui: é lido fresco dentro de writeHi a cada iteração.
                if ( !posValid || now - tPos >= 1 ) {
                    // SilentAimTarget:
                    //   0 = Head   (só cabeça)
                    //   1 = Chest  (só torso)
                    //   2 = Mix    (alterna HEAD e HIP conforme SilentChestRate:HeadRate)
                    int tgtMode = g_Options.AimDMA.SilentAimTarget;

                    // rawHp = mira primária (Head pra modo 0/2, Chest pra modo 1).
                    // Se falhar, posValid=false e pula frame.
                    Vector3 rawHp = ( tgtMode == 1 )
                                    ? Transform::GetChestPosition( cTarget )
                                    : Transform::GetHeadPosition( cTarget );

                    if ( !( rawHp.X == 0.f && rawHp.Y == 0.f && rawHp.Z == 0.f ) ) {

                        // Velocidade calculada sobre posição RAW (sem lead acumulado)
                        if ( prevHeadTick != 0 ) {
                            DWORD el = now - prevHeadTick;
                            if ( el > 0 && el < 300 ) {
                                float dt = el * 0.001f;
                                Vector3 rv = {
                                    ( rawHp.X - prevRawHead.X ) / dt,
                                    ( rawHp.Y - prevRawHead.Y ) / dt,
                                    ( rawHp.Z - prevRawHead.Z ) / dt
                                };
                                float vMag = sqrtf( rv.X*rv.X + rv.Y*rv.Y + rv.Z*rv.Z );
                                if ( vMag <= MAX_VEL ) {
                                    // alpha 0.5: resposta mais rápida a mudanças de direção do alvo
                                    smoothVel.X = smoothVel.X * 0.5f + rv.X * 0.5f;
                                    smoothVel.Y = smoothVel.Y * 0.5f + rv.Y * 0.5f;
                                    smoothVel.Z = smoothVel.Z * 0.5f + rv.Z * 0.5f;
                                }
                            }
                        }
                        prevRawHead  = rawHp;   // salva RAW antes do lead
                        prevHeadTick = now;

                        // HEAD_Y_OFFSET só é aplicado quando o target é a cabeça.
                        // Modo 1 (chest) usa a posição direta do torso, sem bias.
                        float yBias = ( tgtMode == 1 ) ? 0.0f : HEAD_Y_OFFSET;
                        Vector3 hp = rawHp;
                        hp.X += smoothVel.X * PREDICT_LEAD_S;
                        hp.Y += smoothVel.Y * PREDICT_LEAD_S + yBias;
                        hp.Z += smoothVel.Z * PREDICT_LEAD_S;
                        cAimPos = hp;

                        // ── Modo 2 (Mix): também calcula a posição HIP predita
                        // pra a alternância de writes. cAimAlt aplica a mesma
                        // velocidade suavizada da cabeça (aproximação válida —
                        // o corpo inteiro se move com a mesma velocidade linear
                        // exceto por animação). Sem yBias (root já é o centro
                        // do quadril).
                        if ( tgtMode == 2 ) {
                            Vector3 rawHip = Transform::GetRootPosition( cTarget );
                            if ( !( rawHip.X == 0.f && rawHip.Y == 0.f && rawHip.Z == 0.f ) ) {
                                cAimAlt = {
                                    rawHip.X + smoothVel.X * PREDICT_LEAD_S,
                                    rawHip.Y + smoothVel.Y * PREDICT_LEAD_S,
                                    rawHip.Z + smoothVel.Z * PREDICT_LEAD_S
                                };
                            } else {
                                // Sem hip válido — cai no HEAD pra todos os writes.
                                cAimAlt = cAimPos;
                            }
                        }

                        posValid = true;
                    } else {
                        posValid = false;
                    }
                    tPos = now;
                }

                if ( !posValid ) { Cheat::HiRes::SleepMillis( 1 ); continue; }

                // ── FOV check antes do burst ────────────────────────────────
                // Silent de referência: se o target sai do FOV mid-burst, para
                // de escrever (evita direções aleatórias vazando pro server).
                // Só ativa quando SilentFov > 0 (>0 = tem cap). Usa a view
                // matrix cached do snap.
                bool fovOk = true;
                if ( g_Options.AimDMA.SilentFov > 0.f ) {
                    if ( const auto* snap = Cheat::Shared::Producer::Get( ).Latest( ) ) {
                        if ( snap->valid && snap->hasView ) {
                            Vector3 scr = W2S::World2Screen( snap->viewMatrix , cAimPos );
                            if ( scr.Z <= 0.f ) {
                                fovOk = false;   // alvo atrás da câmera
                            } else {
                                float dx = scr.X - snap->screenW * 0.5f;
                                float dy = scr.Y - snap->screenH * 0.5f;
                                if ( sqrtf( dx * dx + dy * dy ) > g_Options.AimDMA.SilentFov )
                                    fovOk = false;
                            }
                        }
                    }
                }
                if ( !fovOk ) { Cheat::HiRes::SleepMillis( 1 ); continue; }

                // [D] Escreve direção — roda em CADA iteração do while, sem dead gap.
                // aimPointer escolhe HEAD (cAimPos) ou HIP (cAimAlt) conforme
                // o modo Silent:
                //   Target=0 → sempre HEAD
                //   Target=1 → sempre CHEST (cAimPos já é chest)
                //   Target=2 → alterna HIP/HEAD conforme rate global counter
                static Vector3 s_lastSp = {};

                auto writeHi = [&]( uint32_t hi , const Vector3& aimP ) {
                    if ( !validHi( hi ) ) return;

                    Vector3 sp = g_Memory->Read<Vector3>( hi + Off::HitInfoStartPos );
                    if ( sp.X != 0.f || sp.Y != 0.f || sp.Z != 0.f ) {
                        s_lastSp = sp;
                    } else if ( s_lastSp.X != 0.f || s_lastSp.Y != 0.f || s_lastSp.Z != 0.f ) {
                        sp = s_lastSp;
                    } else if ( cPlayerPos.X != 0.f || cPlayerPos.Y != 0.f || cPlayerPos.Z != 0.f ) {
                        sp = cPlayerPos;
                    } else {
                        return;
                    }

                    Vector3 dir = { aimP.X - sp.X, aimP.Y - sp.Y, aimP.Z - sp.Z };
                    if ( dir.X == 0.f && dir.Y == 0.f && dir.Z == 0.f ) return;

                    float len = sqrtf( dir.X*dir.X + dir.Y*dir.Y + dir.Z*dir.Z );
                    if ( len < 0.001f ) return;
                    dir.X /= len; dir.Y /= len; dir.Z /= len;

                    const float jitter = ( RngFloat() * ( SMOOTH_MAX - SMOOTH_MIN ) ) + SMOOTH_MIN;
                    dir.X += ( RngFloat() * jitter ) - ( jitter * 0.5f );
                    dir.Y += ( RngFloat() * jitter ) - ( jitter * 0.5f );
                    dir.Z += ( RngFloat() * jitter ) - ( jitter * 0.5f );

                    g_Memory->Write<Vector3>( hi + Off::HitInfoDirection, dir );
                };

                // Pré-calcula rates do modo mix (target=2). No modo 0/1, aimPtr
                // é fixo (cAimPos). Ternário no lugar de std::max — windows.h
                // define `max` como macro e quebra o parsing.
                const int mixMode  = g_Options.AimDMA.SilentAimTarget == 2 ? 1 : 0;
                const int rawChest = g_Options.AimDMA.SilentChestRate;
                const int rawHead  = g_Options.AimDMA.SilentHeadRate;
                const int chestR   = mixMode ? ( rawChest < 0 ? 0 : rawChest ) : 0;
                const int headR    = mixMode ? ( rawHead  < 0 ? 0 : rawHead  ) : 1;
                const int totalR   = ( chestR + headR ) > 0 ? ( chestR + headR ) : 1;

                // Coalesce burst de 12 writes (6 iters × 2 hi) em UMA BatchWrite.
                // RAII garante End em qualquer exception intermediária.
                {
                    EMemory::WriteBatchGuard _wbg;
                    for ( int b = 0; b < 6; b++ ) {
                        // Escolhe aimPtr conforme rate global counter (persiste
                        // entre bursts — mix time-distributed, não per-burst).
                        const Vector3* aimPtr = &cAimPos;
                        if ( mixMode ) {
                            int phase = (int)( writeCounter % (uint32_t)totalR );
                            aimPtr = ( phase < chestR ) ? &cAimAlt : &cAimPos;
                        }
                        writeHi( cHi1 , *aimPtr );
                        writeHi( cHi2 , *aimPtr );
                        writeHi( cHi3 , *aimPtr );
                        writeHi( cHi4 , *aimPtr );
                        ++writeCounter;
                    }
                }


            } catch ( ... ) {}
            // Sem sleep — loop contínuo de escrita sem dead gap
        }
    }

    // Thread dedicada para FindBestTarget — roda separada do loop de escrita para
    // não bloquear as escritas durante a varredura de 100 entidades (~600ms de DMA)
    static void SilentTargetScanThread( uint32_t il2cpp ) {
        SetThreadPriority( GetCurrentThread( ), THREAD_PRIORITY_ABOVE_NORMAL );
        while ( bRunning ) {
            EMemory::BeginFrameRead();
            if ( !g_Options.AimDMA.SilentAim ) {
                s_SilentTarget.store( 0, std::memory_order_relaxed );
                Sleep( 50 );
                continue;
            }

            {
                int bind = g_Options.AimDMA.SilentAimBind;
                if ( bind != 0 && ( ( GetAsyncKeyState( bind ) & 0x8000 ) == 0 ) ) {
                    s_SilentTarget.store( 0, std::memory_order_relaxed );
                    Sleep( 10 );
                    continue;
                }
            }

            try {
                uint32_t lp = GetLocalPlayer( il2cpp );
                if ( !lp ) { Sleep( 10 ); continue; }

                auto ltf = g_Memory->Read<uint32_t>( lp + Cheat::g_DrawOff.TRANSFORM );
                Vector3   lPos = ltf ? Transform::get_position_Injected( ltf ) : Vector3{};
                Matrix4x4 vm   = GetViewMatrix( lp );
                DictInfo  d    = GetDict( il2cpp );

                if ( d.valid && vm.m[0][0] != 0.f ) {
                    uint32_t best = FindBestTarget( lp, d, lPos, vm, g_Options.AimDMA.SilentFov );

                    if ( best && g_Options.AimDMA.SilentVisibleCheck ) {
                        RayCast::Initialize();

                        Vector3 eye{};
                        uint32_t fc = g_Memory->Read<uint32_t>( lp + Cheat::g_DrawOff.FOLLOWCAM );
                        uint32_t cam = fc ? g_Memory->Read<uint32_t>( fc + Cheat::g_DrawOff.CAMERA ) : 0;
                        uint32_t pr  = cam ? g_Memory->Read<uint32_t>( cam + Cheat::g_DrawOff.POSTRENDER ) : 0;
                        if ( pr ) eye = W2S::GetCameraWorldPos( pr );

                        if ( eye == Vector3::Zero() ) eye = Transform::GetHeadPosition( lp );
                        if ( eye == Vector3::Zero() ) eye = lPos + ( Vector3::Up() * 1.5f );

                        Vector3 targetHead = Transform::GetHeadPosition( best );
                        if ( targetHead == Vector3::Zero() || !RayCast::IsVisible( eye, targetHead ) )
                            best = 0;
                    }

                    s_SilentTarget.store( best, std::memory_order_relaxed );
                } else {
                    s_SilentTarget.store( 0, std::memory_order_relaxed );
                }
            } catch ( ... ) {}
            Sleep( 20 );
        }
        s_SilentTarget.store( 0, std::memory_order_relaxed );
    }

    void SpinBotLoop(uint32_t il2cpp)
    {
        constexpr uint32_t TMATRIX_SIZE = 0x30;
        constexpr uint32_t ROT_OFFSET   = 0x10;
        constexpr float    TWO_PI       = 6.28318530f;

        float currentAngle = 0.f;

        while (bRunning) {
            try {
                if (!g_Options.AimDMA.SpinBot) {
                    currentAngle = 0.f;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }

                // Bind: 0 = sempre ativo, >0 = precisa segurar a tecla
                {
                    int bind = g_Options.AimDMA.SpinBotBind;
                    if ( bind != 0 && !( GetAsyncKeyState( bind ) & 0x8000 ) ) {
                        currentAngle = 0.f;
                        Cheat::HiRes::SleepMillis( 5 );
                        continue;
                    }
                }

                uint32_t lp = GetLocalPlayer(il2cpp);
                if (!lp) { Cheat::HiRes::SleepMillis(5); continue; }

                // Anti-detect: não gira quando está andando sem atirar
                {
                    bool isShooting = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                    uint32_t uc = g_Memory->Read<uint32_t>(lp + Off::PlayerUserControl);
                    if (uc) {
                        uint32_t axisArr = g_Memory->Read<uint32_t>(uc + Off::UserControlAxisData);
                        if (axisArr) {
                            uint32_t moveAxis = g_Memory->Read<uint32_t>(axisArr + Off::UserControlMoveAxisIdx);
                            if (moveAxis) {
                                bool isTouched = g_Memory->Read<bool>(moveAxis + Off::UserControlIsTouched);
                                if (isTouched && !isShooting) {
                                    Cheat::HiRes::SleepMillis(5);
                                    continue;
                                }
                            }
                        }
                    }
                }

                // Velocidade: 1 = 1 rev/s, 100 = 100 rev/s. Step é escalado
                // pelo intervalo do sleep de cauda (SPIN_STEP_HZ), então o
                // significado do slider SpinBotSpeed continua idêntico
                // independentemente da taxa de iteração.
                constexpr float SPIN_STEP_HZ = 250.0f; // deve casar com o Sleep(4) do fim do loop
                float speed = (float)g_Options.AimDMA.SpinBotSpeed;
                currentAngle += speed * (TWO_PI / SPIN_STEP_HZ);
                while (currentAngle >= TWO_PI) currentAngle -= TWO_PI;
                while (currentAngle <  0.0f)   currentAngle += TWO_PI;

                Quaternion yawQuat;
                yawQuat.x = 0.f;
                yawQuat.y = sinf(currentAngle * 0.5f);
                yawQuat.z = 0.f;
                yawQuat.w = cosf(currentAngle * 0.5f);

                uint32_t rootNode = g_Memory->Read<uint32_t>(lp + Cheat::g_DrawOff.ROOTNODE);
                if (!rootNode) { Cheat::HiRes::SleepMillis(4); continue; }
                uint32_t rootTF = g_Memory->Read<uint32_t>(rootNode + Cheat::g_DrawOff.ITRANSFORM);
                if (!rootTF) { Cheat::HiRes::SleepMillis(4); continue; }
                uint32_t tAccess = g_Memory->Read<uint32_t>(rootTF + 0x8);
                if (!tAccess) { Cheat::HiRes::SleepMillis(4); continue; }
                int tIndex = g_Memory->Read<int>(tAccess + 0x24);
                if (tIndex < 0) { Cheat::HiRes::SleepMillis(4); continue; }
                uint32_t basePtr = g_Memory->Read<uint32_t>(tAccess + 0x20);
                if (!basePtr) { Cheat::HiRes::SleepMillis(4); continue; }
                uint32_t matrixList    = g_Memory->Read<uint32_t>(basePtr + Cheat::g_DrawOff.MATRIXLIST);
                uint32_t matrixIndices = g_Memory->Read<uint32_t>(basePtr + Cheat::g_DrawOff.MATRIXIDX);
                if (!matrixList || !matrixIndices) { Cheat::HiRes::SleepMillis(4); continue; }

                // Coalesce: até 61 writes de Quaternion (root + 60 parents)
                // viram UMA BatchWrite ao driver. RAII garante End em exception.
                {
                    EMemory::WriteBatchGuard _wbg;
                    g_Memory->Write<Quaternion>(matrixList + TMATRIX_SIZE * tIndex + ROT_OFFSET, yawQuat);

                    // Propaga aos parents — faz o spin virar server-side
                    int parentIndex = g_Memory->Read<int>(matrixIndices + sizeof(int) * tIndex);
                    int safety = 0;
                    while (parentIndex >= 0 && safety++ < 60) {
                        g_Memory->Write<Quaternion>(matrixList + TMATRIX_SIZE * parentIndex + ROT_OFFSET, yawQuat);
                        parentIndex = g_Memory->Read<int>(matrixIndices + sizeof(int) * parentIndex);
                    }
                }
            }
            catch (...) {}
            // 250Hz — cobre o tick do jogo (60fps) com 4× de folga; step do
            // ângulo é escalado por SPIN_STEP_HZ acima pra manter revoluções/s
            // idênticas ao que o slider indica. 4× mais barato em CPU que 1ms.
            Cheat::HiRes::SleepMillis(4);
        }
    }

    void AtributarArmaLoop(uint32_t il2cpp) {
        // Lógica do projeto antigo: 1 write em m_FireIntervalScale (PA+0x1C4).
        // O jogo re-computa o campo ao trocar de arma — não precisa restaurar.
        while (bRunning) {
            if (g_Options.AimDMA.AtributarArmaSpeed == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t pa = g_Memory->Read<uint32_t>(lp + Off::PlayerAttributes);
                if (pa) {
                    float iv = 1.0f;
                    switch (g_Options.AimDMA.AtributarArmaSpeed) {
                    case 1: iv = 0.90f; break;
                    case 2: iv = 0.85f; break;
                    case 3: iv = 0.80f; break;
                    case 4: iv = 0.75f; break;
                    }
                    g_Memory->Write<float>(pa + Off::FireIntervalScale, iv);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // Aimbot Legit (BoneSwap) — ativado pela checkbox "Enable Aim".
    // NAO mexe na camera do player. Troca m_HipNode (ent+0x460) <-> m_BloodEffectNode (ent+0x464)
    // no INIMIGO, fazendo o servidor classificar tiros no torso como headshot.
    // Porta do AimBotSafe/BoneSwap do projeto HideConsole.
    // ── Helper: GetBoneTargetPosition (copia exata do HideConsole) ────────────
    static Vector3 GetBoneTargetPosition(uint32_t ent, int bone)
    {
        Vector3 head = Transform::GetHeadPosition(ent);
        if (head == Vector3::Zero()) return head;

        if (bone == 0) { head.Y += 0.1f;  return head; } // Cabeca
        if (bone == 1) { head.Y -= 0.15f; return head; } // Pescoco
        // bone == 2: Corpo — midpoint entre cabeca e root
        Vector3 root = Transform::GetRootPosition(ent);
        if (root == Vector3::Zero()) { head.Y += 0.1f; return head; }
        return { (head.X + root.X) * 0.5f,
                 (head.Y + root.Y) * 0.5f + 0.2f,
                 (head.Z + root.Z) * 0.5f };
    }

    // Aimbot Legit / Safe Mode — copia EXATA do AimBotMemoryLoop do HideConsole
    // quando AimBotSafe + AimBotSafeAlways estao ativos.
    // BoneSwap: troca Player+0x4A0 (m_HipNode) <-> Player+0x4A4 (m_BloodEffectNode) no alvo (headshot servidor).
    // Mira automatica sem bind no corpo do alvo (bone 2 = midpoint cabeca/root).
    void LegitAimLoop(uint32_t il2cpp) {
        uint32_t bsAddrA = 0, bsAddrB  = 0;
        uint32_t bsSavedA = 0, bsSavedB = 0;
        uint32_t bsLastTarget = 0;
        bool     bsApplied   = false;

        auto BS_Restore = [&]() {
            if (bsApplied && bsAddrA && bsAddrB) {
                // Coalesce 2 writes → 1 BatchWrite (RAII garante End mesmo em exception)
                EMemory::WriteBatchGuard _wbg;
                g_Memory->Write<uint32_t>(bsAddrA, bsSavedA);
                g_Memory->Write<uint32_t>(bsAddrB, bsSavedB);
            }
            bsApplied = false; bsAddrA = 0; bsAddrB = 0; bsLastTarget = 0;
            g_bsEntity.store(0, std::memory_order_relaxed);
        };

        // Bind/delay state
        bool  keyWasDown    = false;
        DWORD keyDownStart  = 0;
        bool  delayPassed   = false;

        while (bRunning) {
            EMemory::BeginFrameRead();
            try {
                if (!g_Options.AimDMA.LegitAimEnable) {
                    if (bsApplied) BS_Restore();
                    keyWasDown = false; delayPassed = false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }

                // Ombros ativos: cede para evitar double-write em BoneHip (0x4A0)
                if (g_Options.AimDMA.LegitAimLeftShoulder || g_Options.AimDMA.LegitAimRightShoulder) {
                    if (bsApplied) BS_Restore();
                    keyWasDown = false; delayPassed = false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }

                // ── Bind: 0 = sempre ativo, >0 = precisa segurar a tecla ─────
                int key = g_Options.AimDMA.LegitAimKey;
                bool keyDown = (key == 0) || ((GetAsyncKeyState(key) & 0x8000) != 0);

                if (!keyDown) {
                    if (bsApplied) BS_Restore();
                    keyWasDown = false; delayPassed = false;
                    Cheat::HiRes::SleepMillis(5);
                    continue;
                }

                // ── Delay: ao apertar bind, espera N ms antes do swap ────────
                if (!keyWasDown) {
                    keyWasDown   = true;
                    keyDownStart = GetTickCount();
                    delayPassed  = (g_Options.AimDMA.LegitAimDelay <= 0);
                }
                if (!delayPassed) {
                    if (GetTickCount() - keyDownStart < (DWORD)g_Options.AimDMA.LegitAimDelay) {
                        Cheat::HiRes::SleepMillis(1);
                        continue;
                    }
                    delayPassed = true;
                }

                uint32_t lp = GetLocalPlayer(il2cpp);
                if (!lp) { Cheat::HiRes::SleepMillis(5); continue; }

                auto localTf = g_Memory->Read<uint32_t>(lp + Cheat::g_DrawOff.TRANSFORM);
                if (!localTf) continue;
                Vector3 localPos = Transform::get_position_Injected(localTf);
                if (localPos == Vector3::Zero()) continue;

                Matrix4x4 vm = GetViewMatrix(lp);
                if (vm.m[0][0] == 0.f) continue;

                DictInfo d = GetDict(il2cpp);
                if (!d.valid) continue;

                uint32_t best = FindBestTarget(lp, d, localPos, vm, 0.f);

                if (!best) {
                    if (bsApplied) BS_Restore();
                } else {
                    if (bsApplied && bsLastTarget != best) BS_Restore();

                    if (!bsApplied) {
                        // Swap m_HipNode (0x4A0) <-> m_BloodEffectNode (0x4A4) — tiro no corpo vira headshot servidor
                        uint32_t valA = g_Memory->Read<uint32_t>(best + Off::BoneHip);
                        uint32_t valB = g_Memory->Read<uint32_t>(best + Off::BoneBloodEffect);
                        if (valA && valB) {
                            bsAddrA      = best + Off::BoneHip;
                            bsAddrB      = best + Off::BoneBloodEffect;
                            bsSavedA     = valA;
                            bsSavedB     = valB;
                            bsApplied    = true;
                            bsLastTarget = best;
                            g_bsSavedHip.store(valA, std::memory_order_relaxed);
                            g_bsSavedBlood.store(valB, std::memory_order_relaxed);
                            g_bsEntity.store(best, std::memory_order_relaxed);
                        }
                    }
                    if (bsApplied) {
                        // Coalesce 2 writes → 1 BatchWrite (RAII, exception-safe)
                        EMemory::WriteBatchGuard _wbg;
                        g_Memory->Write<uint32_t>(bsAddrA, bsSavedB);
                        g_Memory->Write<uint32_t>(bsAddrB, bsSavedA);
                    }
                }
            } catch (...) {}
            // 250Hz — BoneSwap escreve 2 uint32 no alvo. A 1000Hz era 4× mais
            // caro em RPMs/WPMs + FindBestTarget sem ganho: o tick do jogo é
            // 60fps e o server só processa hits nesse cadence. 250Hz cobre
            // 100+ tiros/s de forma imperceptível pro humano.
            Cheat::HiRes::SleepMillis(4);
        }

        if (bsApplied) BS_Restore();
    }

    // BoneSwap HipNode <-> BoneLeftArm no alvo (ombro esquerdo)
    void LegitAimLeftShoulderLoop(uint32_t il2cpp) {
        uint32_t bsAddrA = 0, bsAddrB  = 0;
        uint32_t bsSavedA = 0, bsSavedB = 0;
        uint32_t bsLastTarget = 0;
        bool     bsApplied   = false;

        auto BS_Restore = [&]() {
            if (bsApplied && bsAddrA && bsAddrB) {
                // Coalesce 2 writes → 1 BatchWrite (RAII, exception-safe)
                EMemory::WriteBatchGuard _wbg;
                g_Memory->Write<uint32_t>(bsAddrA, bsSavedA);
                g_Memory->Write<uint32_t>(bsAddrB, bsSavedB);
            }
            bsApplied = false; bsAddrA = 0; bsAddrB = 0; bsLastTarget = 0;
        };

        bool  keyWasDown   = false;
        DWORD keyDownStart = 0;
        bool  delayPassed  = false;

        while (bRunning) {
            EMemory::BeginFrameRead();
            try {
                if (!g_Options.AimDMA.LegitAimLeftShoulder) {
                    if (bsApplied) BS_Restore();
                    keyWasDown = false; delayPassed = false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }

                int key = g_Options.AimDMA.LegitAimKey;
                bool keyDown = (key == 0) || ((GetAsyncKeyState(key) & 0x8000) != 0);

                if (!keyDown) {
                    if (bsApplied) BS_Restore();
                    keyWasDown = false; delayPassed = false;
                    Cheat::HiRes::SleepMillis(5);
                    continue;
                }

                if (!keyWasDown) {
                    keyWasDown   = true;
                    keyDownStart = GetTickCount();
                    delayPassed  = (g_Options.AimDMA.LegitAimDelay <= 0);
                }
                if (!delayPassed) {
                    if (GetTickCount() - keyDownStart < (DWORD)g_Options.AimDMA.LegitAimDelay) {
                        Cheat::HiRes::SleepMillis(1);
                        continue;
                    }
                    delayPassed = true;
                }

                uint32_t lp = GetLocalPlayer(il2cpp);
                if (!lp) { Cheat::HiRes::SleepMillis(5); continue; }

                auto localTf = g_Memory->Read<uint32_t>(lp + Cheat::g_DrawOff.TRANSFORM);
                if (!localTf) continue;
                Vector3 localPos = Transform::get_position_Injected(localTf);
                if (localPos == Vector3::Zero()) continue;

                Matrix4x4 vm = GetViewMatrix(lp);
                if (vm.m[0][0] == 0.f) continue;

                DictInfo d = GetDict(il2cpp);
                if (!d.valid) continue;

                uint32_t best = FindBestTarget(lp, d, localPos, vm, 0.f);

                if (!best) {
                    if (bsApplied) BS_Restore();
                } else {
                    if (bsApplied && bsLastTarget != best) BS_Restore();

                    if (!bsApplied) {
                        uint32_t valA = g_Memory->Read<uint32_t>(best + Off::BoneHip);
                        uint32_t valB = g_Memory->Read<uint32_t>(best + Off::BoneLeftArm);
                        if (valA && valB) {
                            bsAddrA      = best + Off::BoneHip;
                            bsAddrB      = best + Off::BoneLeftArm;
                            bsSavedA     = valA;
                            bsSavedB     = valB;
                            bsApplied    = true;
                            bsLastTarget = best;
                        }
                    }
                    if (bsApplied) {
                        // Coalesce 2 writes → 1 BatchWrite (RAII, exception-safe)
                        EMemory::WriteBatchGuard _wbg;
                        g_Memory->Write<uint32_t>(bsAddrA, bsSavedB);
                        g_Memory->Write<uint32_t>(bsAddrB, bsSavedA);
                    }
                }
            } catch (...) {}
            // 250Hz — BoneSwap escreve 2 uint32 no alvo. A 1000Hz era 4× mais
            // caro em RPMs/WPMs + FindBestTarget sem ganho: o tick do jogo é
            // 60fps e o server só processa hits nesse cadence. 250Hz cobre
            // 100+ tiros/s de forma imperceptível pro humano.
            Cheat::HiRes::SleepMillis(4);
        }

        if (bsApplied) BS_Restore();
    }

    // BoneSwap HipNode <-> BoneRightArm no alvo (ombro direito)
    void LegitAimRightShoulderLoop(uint32_t il2cpp) {
        uint32_t bsAddrA = 0, bsAddrB  = 0;
        uint32_t bsSavedA = 0, bsSavedB = 0;
        uint32_t bsLastTarget = 0;
        bool     bsApplied   = false;

        auto BS_Restore = [&]() {
            if (bsApplied && bsAddrA && bsAddrB) {
                // Coalesce 2 writes → 1 BatchWrite (RAII, exception-safe)
                EMemory::WriteBatchGuard _wbg;
                g_Memory->Write<uint32_t>(bsAddrA, bsSavedA);
                g_Memory->Write<uint32_t>(bsAddrB, bsSavedB);
            }
            bsApplied = false; bsAddrA = 0; bsAddrB = 0; bsLastTarget = 0;
        };

        bool  keyWasDown   = false;
        DWORD keyDownStart = 0;
        bool  delayPassed  = false;

        while (bRunning) {
            EMemory::BeginFrameRead();
            try {
                if (!g_Options.AimDMA.LegitAimRightShoulder) {
                    if (bsApplied) BS_Restore();
                    keyWasDown = false; delayPassed = false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }

                int key = g_Options.AimDMA.LegitAimKey;
                bool keyDown = (key == 0) || ((GetAsyncKeyState(key) & 0x8000) != 0);

                if (!keyDown) {
                    if (bsApplied) BS_Restore();
                    keyWasDown = false; delayPassed = false;
                    Cheat::HiRes::SleepMillis(5);
                    continue;
                }

                if (!keyWasDown) {
                    keyWasDown   = true;
                    keyDownStart = GetTickCount();
                    delayPassed  = (g_Options.AimDMA.LegitAimDelay <= 0);
                }
                if (!delayPassed) {
                    if (GetTickCount() - keyDownStart < (DWORD)g_Options.AimDMA.LegitAimDelay) {
                        Cheat::HiRes::SleepMillis(1);
                        continue;
                    }
                    delayPassed = true;
                }

                uint32_t lp = GetLocalPlayer(il2cpp);
                if (!lp) { Cheat::HiRes::SleepMillis(5); continue; }

                auto localTf = g_Memory->Read<uint32_t>(lp + Cheat::g_DrawOff.TRANSFORM);
                if (!localTf) continue;
                Vector3 localPos = Transform::get_position_Injected(localTf);
                if (localPos == Vector3::Zero()) continue;

                Matrix4x4 vm = GetViewMatrix(lp);
                if (vm.m[0][0] == 0.f) continue;

                DictInfo d = GetDict(il2cpp);
                if (!d.valid) continue;

                uint32_t best = FindBestTarget(lp, d, localPos, vm, 0.f);

                if (!best) {
                    if (bsApplied) BS_Restore();
                } else {
                    if (bsApplied && bsLastTarget != best) BS_Restore();

                    if (!bsApplied) {
                        uint32_t valA = g_Memory->Read<uint32_t>(best + Off::BoneHip);
                        uint32_t valB = g_Memory->Read<uint32_t>(best + Off::BoneRightArm);
                        if (valA && valB) {
                            bsAddrA      = best + Off::BoneHip;
                            bsAddrB      = best + Off::BoneRightArm;
                            bsSavedA     = valA;
                            bsSavedB     = valB;
                            bsApplied    = true;
                            bsLastTarget = best;
                        }
                    }
                    if (bsApplied) {
                        // Coalesce 2 writes → 1 BatchWrite (RAII, exception-safe)
                        EMemory::WriteBatchGuard _wbg;
                        g_Memory->Write<uint32_t>(bsAddrA, bsSavedB);
                        g_Memory->Write<uint32_t>(bsAddrB, bsSavedA);
                    }
                }
            } catch (...) {}
            // 250Hz — BoneSwap escreve 2 uint32 no alvo. A 1000Hz era 4× mais
            // caro em RPMs/WPMs + FindBestTarget sem ganho: o tick do jogo é
            // 60fps e o server só processa hits nesse cadence. 250Hz cobre
            // 100+ tiros/s de forma imperceptível pro humano.
            Cheat::HiRes::SleepMillis(4);
        }

        if (bsApplied) BS_Restore();
    }

    void AimlockLoop(uint32_t il2cpp) {
        // Escreve -3.0f em weapon+0x4BC (<HFHNMGCBPIK>k__BackingField no dump 1.132.1).
        // NAGGOHBGHKK (campo original do exploit) nao existe no dump 1.132.1 — offset pode
        // estar errada ou mecanismo mudou. AimbotScopeLoop (LerpTime) e alternativa ativa.
        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.Aimlock) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }

            // Bind: 0 = sempre ativo, >0 = precisa segurar a tecla
            {
                int bind = g_Options.AimDMA.AimlockBind;
                if ( bind != 0 && !( GetAsyncKeyState( bind ) & 0x8000 ) ) {
                    Cheat::HiRes::SleepMillis( 5 );
                    continue;
                }
            }

            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t inv = g_Memory->Read<uint32_t>(lp + Off::AimlockInvMgr);
                if (inv) {
                    uint32_t ioh = inv /*Off::AimlockItemOnHand=0 apos refactor 2026-09-16 - inv ja e o weapon*/;
                    if (ioh) g_Memory->Write<float>(ioh + Off::FireDuration, -3.0f);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // -------------------------------------------------------------------------
    // FastMedkitLoop — reduz EatSpeedScale para 0.5f (mínimo do jogo = ~2s
    // de animação, equivalente a Maxim StrongMedicine Lv4).
    //
    // Por que 0.5f e não 0.75f:
    //   EatSpeedScale é um multiplicador de TEMPO (1.0 = normal, <1.0 = mais
    //   rápido). O servidor sincroniza o campo periodicamente via pacotes COW;
    //   com 0.75f o ganho (25% mais rápido) é pequeno demais para vencer a
    //   competição com o sync do servidor (~100ms). Com 0.5f (2× mais rápido)
    //   o efeito é suficientemente forte para ser perceptível mesmo quando o
    //   servidor reseta o campo entre nossas escritas.
    //
    // Por que sem read-compare e sleep 10ms:
    //   Polling a 50ms com guard de leitura deixava janelas de ~50ms onde o
    //   servidor conseguia restaurar 1.0f antes da próxima iteração. A 10ms
    //   escrevemos ~10× mais rápido que o sync (~100ms), garantindo que o
    //   jogo lê 0.5f na esmagadora maioria dos frames durante o uso do kit.
    // -------------------------------------------------------------------------
    void FastMedkitLoop(uint32_t il2cpp)
    {
        float originalScale = 1.0f;
        bool  hasOriginal   = false;
        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.FastMedkit) {
                if (hasOriginal) {
                    uint32_t lp = GetLocalPlayer(il2cpp);
                    if (lp) {
                        uint32_t at = g_Memory->Read<uint32_t>(lp + Off::PlayerAttributes);
                        if (at) g_Memory->Write<float>(at + Off::EatSpeedScale, originalScale);
                    }
                    hasOriginal = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t at = g_Memory->Read<uint32_t>(lp + Off::PlayerAttributes);
                if (at) {
                    // Salva original apenas na primeira ativação
                    if (!hasOriginal) {
                        originalScale = g_Memory->Read<float>(at + Off::EatSpeedScale);
                        hasOriginal   = true;
                    }
                    // Escreve direto, sem read-compare — elimina a latência de
                    // 1 RPM extra por tick e garante que o valor está "fresco"
                    // quando o jogo lê para calcular a duração da animação.
                    g_Memory->Write<float>(at + Off::EatSpeedScale, 0.5f);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void MoreDamageLoop(uint32_t il2cpp)
    {
        float origFDD = 0.0f; bool hasOrig = false;
        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.MoreDamage) {
                if (hasOrig) {
                    uint32_t lp = GetLocalPlayer(il2cpp);
                    if (lp) {
                        uint32_t inv = g_Memory->Read<uint32_t>(lp + Off::AimlockInvMgr);
                        uint32_t ioh = inv ? inv /*Off::AimlockItemOnHand=0 apos refactor 2026-09-16 - inv ja e o weapon*/ : 0;
                        if (ioh) g_Memory->Write<float>(ioh + Off::WeaponParamsOff + Off::FullDamageDistance, origFDD);
                    }
                    hasOrig = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; // idle: 4× menos wake-ups
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t inv = g_Memory->Read<uint32_t>(lp + Off::AimlockInvMgr);
                uint32_t ioh = inv ? inv /*Off::AimlockItemOnHand=0 apos refactor 2026-09-16 - inv ja e o weapon*/ : 0;
                if (ioh) {
                    uint32_t wp = ioh + Off::WeaponParamsOff;
                    float cur = g_Memory->Read<float>(wp + Off::FullDamageDistance);
                    if (!hasOrig) { origFDD = cur; hasOrig = true; }
                    if (cur != 400.0f) g_Memory->Write<float>(wp + Off::FullDamageDistance, 400.0f);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void FireDelayLoop(uint32_t il2cpp)
    {
        float origPD = 0.0f; bool hasOrig = false;
        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.FireDelay) {
                if (hasOrig) {
                    uint32_t lp = GetLocalPlayer(il2cpp);
                    if (lp) {
                        uint32_t inv = g_Memory->Read<uint32_t>(lp + Off::AimlockInvMgr);
                        uint32_t ioh = inv ? inv /*Off::AimlockItemOnHand=0 apos refactor 2026-09-16 - inv ja e o weapon*/ : 0;
                        if (ioh) g_Memory->Write<float>(ioh + Off::WeaponParamsOff + Off::PrefireDelay, origPD);
                    }
                    hasOrig = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; // idle: 4× menos wake-ups
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t inv = g_Memory->Read<uint32_t>(lp + Off::AimlockInvMgr);
                uint32_t ioh = inv ? inv /*Off::AimlockItemOnHand=0 apos refactor 2026-09-16 - inv ja e o weapon*/ : 0;
                if (ioh) {
                    uint32_t wp = ioh + Off::WeaponParamsOff;
                    float cur = g_Memory->Read<float>(wp + Off::PrefireDelay);
                    if (!hasOrig) { origPD = cur; hasOrig = true; }
                    if (cur != 0.0f) g_Memory->Write<float>(wp + Off::PrefireDelay, 0.0f);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void SocoLongeLoop(uint32_t il2cpp)
    {
        // Salva e restaura o range original por arma (nao usa sentinel hardcoded
        // 1.35f — o range varia por arma e o sentinel prevenia o write para a
        // maioria delas). Acesso INLINE: ioh + WeaponParamsOff + WeaponRange
        // (igual MoreDamageLoop que funciona).
        float origRange = 0.f; bool hasOrig = false;
        uint32_t lastIoh = 0;
        while (bRunning) {
            EMemory::BeginFrameRead();
            uint32_t lp = GetLocalPlayer(il2cpp);
            uint32_t ioh = lp ? g_Memory->Read<uint32_t>(lp + Off::AimlockInvMgr) : 0;

            if (!g_Options.AimDMA.SocoLonge) {
                if (hasOrig && ioh) {
                    g_Memory->Write<float>(ioh + Off::WeaponParamsOff + Off::WeaponRange, origRange);
                    hasOrig = false; lastIoh = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue;
            }
            if (ioh) {
                if (!hasOrig || ioh != lastIoh) {
                    origRange = g_Memory->Read<float>(ioh + Off::WeaponParamsOff + Off::WeaponRange);
                    hasOrig = true; lastIoh = ioh;
                }
                g_Memory->Write<float>(ioh + Off::WeaponParamsOff + Off::WeaponRange, 3.1f);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void NoReloadLoop(uint32_t il2cpp)
    {
        uint8_t origVal = 0; bool hasOrig = false;
        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.NoReload) {
                if (hasOrig) {
                    uint32_t lp = GetLocalPlayer(il2cpp);
                    if (lp) {
                        uint32_t at = g_Memory->Read<uint32_t>(lp + Off::LocalPlayerAttrs);
                        if (at) g_Memory->Write<uint8_t>(at + Off::NoReloadFlag, origVal);
                    }
                    hasOrig = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; // idle: 4× menos wake-ups
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t at = g_Memory->Read<uint32_t>(lp + Off::LocalPlayerAttrs);
                if (at) {
                    if (!hasOrig) { origVal = g_Memory->Read<uint8_t>(at + Off::NoReloadFlag); hasOrig = true; }
                    if (g_Memory->Read<uint8_t>(at + Off::NoReloadFlag) != 1)
                        g_Memory->Write<uint8_t>(at + Off::NoReloadFlag, (uint8_t)1);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void PrecisionLoop(uint32_t il2cpp)
    {
        // Valores aplicados quando Precision está ativa.
        constexpr float kPrecRotMin = 15.0625f;
        constexpr float kPrecRotMax = 3.40939e-05f;
        constexpr float kPrecAimMin = 15.125f;
        constexpr float kPrecAimMax = 3184.0f;

        // Valores padrão do jogo — hardcoded, idêntico ao C# DMA RestorePrecision.
        // Não usa snapshot: o C# nunca leu os originais, sempre restaura estes valores.
        constexpr float kDefRotMin  = 15.0f;
        constexpr float kDefRotMax  = 35.0f;
        constexpr float kDefAimMin  = 10.0f;
        constexpr float kDefAimMax  = 20.0f;

        bool     lastPrecision = false; // estado anterior (edge detection)
        uint32_t cachedGV      = 0;     // ponteiro cacheado — resolve uma vez

        while (bRunning) {
            EMemory::BeginFrameRead();
            bool now  = g_Options.AimDMA.Precision;
            bool edge = (now != lastPrecision);

            // Fast path: desativado e sem mudança de estado — nada a fazer.
            if (!now && !edge) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            // Resolve o GameVarDef uma vez e reutiliza (mesmo padrão _cachedGV do C#).
            if (!cachedGV) cachedGV = GetGameVar(il2cpp);
            if (!cachedGV) {
                lastPrecision = now;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (now) {
                // Aplica precision (escreve só se necessário).
                if (g_Memory->Read<float>(cachedGV + Off::RotSensMin)    != kPrecRotMin) g_Memory->Write<float>(cachedGV + Off::RotSensMin,    kPrecRotMin);
                if (g_Memory->Read<float>(cachedGV + Off::RotSensMax)    != kPrecRotMax) g_Memory->Write<float>(cachedGV + Off::RotSensMax,    kPrecRotMax);
                if (g_Memory->Read<float>(cachedGV + Off::AimRotSensMin) != kPrecAimMin) g_Memory->Write<float>(cachedGV + Off::AimRotSensMin, kPrecAimMin);
                if (g_Memory->Read<float>(cachedGV + Off::AimRotSensMax) != kPrecAimMax) g_Memory->Write<float>(cachedGV + Off::AimRotSensMax, kPrecAimMax);
            } else {
                // Restaura valores padrão hardcoded — sem condição de leitura antes,
                // igual ao C# RestorePrecision que escreve incondicionalmente.
                g_Memory->Write<float>(cachedGV + Off::RotSensMin,    kDefRotMin);
                g_Memory->Write<float>(cachedGV + Off::RotSensMax,    kDefRotMax);
                g_Memory->Write<float>(cachedGV + Off::AimRotSensMin, kDefAimMin);
                g_Memory->Write<float>(cachedGV + Off::AimRotSensMax, kDefAimMax);
            }

            lastPrecision = now;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Cleanup on exit: se precision estava ativa quando bRunning caiu,
        // faz o restore final (cobre "fechei o cheat com Precision ativa").
        if (lastPrecision) {
            if (!cachedGV) cachedGV = GetGameVar(il2cpp);
            if (cachedGV) {
                g_Memory->Write<float>(cachedGV + Off::RotSensMin,    kDefRotMin);
                g_Memory->Write<float>(cachedGV + Off::RotSensMax,    kDefRotMax);
                g_Memory->Write<float>(cachedGV + Off::AimRotSensMin, kDefAimMin);
                g_Memory->Write<float>(cachedGV + Off::AimRotSensMax, kDefAimMax);
            }
        }
    }

    void BugarPixelLoop(uint32_t il2cpp)
    {
        bool lastBP=false;
        while (bRunning) {
            EMemory::BeginFrameRead();
            bool wB=g_Options.AimDMA.BugarPixel;
            if(!wB&&!lastBP){ std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
            uint32_t gv=GetGameVar(il2cpp);
            if(gv){
                if(wB){ if(g_Memory->Read<float>(gv+Off::ShootTraceThresh)!=0.0f) g_Memory->Write<float>(gv+Off::ShootTraceThresh,0.0f); }
                else if(lastBP){ g_Memory->Write<float>(gv+Off::ShootTraceThresh,1.5f); }
            }
            lastBP=wB;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // TickGhostMode — Ghost toggle driver, called EVERY render frame from
    // Draw.cpp. Replaces the old GhostModeLoop background thread. See the
    // doc-comment on the declaration in AimModules.hpp for why this had
    // to move to the render thread.
    //
    // Mirrors ZmInternal (Downloads/ZmInternal/EspLines/Main/Draw/Draw.cpp)
    // 1:1: only m_WaitForForceSync is written (no ServerForceSync, no
    // LastForceSyncTick, no ServerForceTick), and the write is a single
    // edge-triggered toggle — never repeated in a hot loop.
    void TickGhostMode(uint32_t lp) {
        // Function-static so state survives between frames. There is only
        // one Draw thread, so a plain static is safe (no atomicity needed).
        static bool wasActive = false;

        // Master switch off → release everything and reset.
        if (!g_Options.AimDMA.GhostMode) {
            if (wasActive) {
                if (lp) g_Memory->Write<bool>(lp + Off::GhostWaitForForceSync, false);
                wasActive = false;
                g_GhostActive.store(false);
            }
            return;
        }

        if (!lp) return;

        const int  bind     = g_Options.AimDMA.GhostModeBind;
        const bool bindHeld = (bind == 0) || ((GetAsyncKeyState(bind) & 0x8000) != 0);

        if (bindHeld && !wasActive) {
            // Edge: not held → held. Capture the anchor from the CURRENT
            // transform AND the local player's 18 bones (world coords).
            // Draw will W2S the bones every frame from the current view
            // matrix, so the "ghost skeleton" stays anchored at the
            // capture location regardless of where the player walks off
            // to next. Reading everything inline here (same frame as the
            // toggle) matches ZmInternal exactly and eliminates the
            // stale-position window the background thread used to have.
            GhostSnapshot fresh{};
            uint32_t tf = g_Memory->Read<uint32_t>(lp + Cheat::g_DrawOff.TRANSFORM);
            if (tf) fresh.pos = Transform::get_position_Injected(tf);
            CaptureLocalSkeleton(lp, fresh);
            {
                std::lock_guard<std::mutex> lk(g_GhostMx);
                g_GhostStorage = fresh;
            }
            g_Memory->Write<bool>(lp + Off::GhostWaitForForceSync, true);
            g_GhostActive.store(true);
            wasActive = true;
        } else if (!bindHeld && wasActive) {
            // Edge: held → released.
            g_Memory->Write<bool>(lp + Off::GhostWaitForForceSync, false);
            wasActive = false;
            g_GhostActive.store(false);
        }
    }

    void AlokBuffLoop(uint32_t il2cpp) {
        static const float kMultScales[4] = { 1.00f, 1.15f, 1.30f, 1.50f };
        // 7200s (2h) — grande o suficiente para qualquer partida, seguro para o cleanup do jogo
        constexpr float kSafeDuration = 7200.0f;
        bool wasOn = false;
        float savedDurA = 0.0f, savedDurB = 0.0f, savedDurC = 0.0f;
        bool durSaved = false;

        while (bRunning) {
            EMemory::BeginFrameRead();
            try {
                uint32_t lp = GetLocalPlayer(il2cpp);
                if (!lp) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

                uint32_t attrs = g_Memory->Read<uint32_t>(lp + Off::PlayerAttributes);
                if (!attrs)   { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

                uint32_t dictRef = g_Memory->Read<uint32_t>(attrs + Off::AttrBuffDict);
                if (!dictRef) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

                uint32_t entries = g_Memory->Read<uint32_t>(dictRef + Off::BuffDictEntries);
                int      count   = g_Memory->Read<int     >(dictRef + Off::BuffDictSize);
                if (!entries || count <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

                uint32_t statePtr = 0;
                int maxIter = count > 64 ? 64 : count;
                for (int i = 0; i < maxIter; ++i) {
                    uint32_t entryBase = entries + Off::BuffDictArrayBase +
                                         (uint32_t)(i * Off::BuffDictElementStride);
                    int key = g_Memory->Read<int>(entryBase);
                    if (key != Off::BuffAllStateKey) continue;
                    statePtr = g_Memory->Read<uint32_t>(entryBase + Off::BuffDictEntityOffset);
                    break;
                }

                if (!statePtr) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                if (g_Options.AimDMA.AlokBuff) {
                    // Dropdown de 3 itens (Alok 1/2/3) — idx 0..2. O tier
                    // "Natural" (kMultScales[0] = 1.00, sem boost) foi
                    // absorvido: Alok 1 agora é o piso e sempre aplica pelo
                    // menos o boost de 1.15. Idx é convertido para o índice
                    // de kMultScales somando +1 (Alok 1 → 1, Alok 3 → 3).
                    int lvl = g_Options.AimDMA.AlokBuffLevel;
                    if (lvl < 0) lvl = 0;
                    if (lvl > 2) lvl = 2;
                    float scale = kMultScales[lvl + 1];
                    g_Memory->Write<float>(statePtr + Off::BuffScaleField, scale);

                    if (g_Options.AimDMA.AlokBuffInfinite) {
                        if (!durSaved) {
                            savedDurA = g_Memory->Read<float>(statePtr + Off::BuffDurationCandA);
                            savedDurB = g_Memory->Read<float>(statePtr + Off::BuffDurationCandB);
                            savedDurC = g_Memory->Read<float>(statePtr + Off::BuffDurationCandC);
                            durSaved = true;
                        }
                        g_Memory->Write<float>(statePtr + Off::BuffDurationCandA, kSafeDuration);
                        g_Memory->Write<float>(statePtr + Off::BuffDurationCandB, kSafeDuration);
                        g_Memory->Write<float>(statePtr + Off::BuffDurationCandC, kSafeDuration);
                    }
                    wasOn = true;
                } else if (wasOn) {
                    g_Memory->Write<float>(statePtr + Off::BuffScaleField, 1.0f);
                    if (durSaved) {
                        g_Memory->Write<float>(statePtr + Off::BuffDurationCandA, savedDurA);
                        g_Memory->Write<float>(statePtr + Off::BuffDurationCandB, savedDurB);
                        g_Memory->Write<float>(statePtr + Off::BuffDurationCandC, savedDurC);
                        durSaved = false;
                    }
                    wasOn = false;
                }
            } catch (...) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // -------------------------------------------------------------------------
    // SpeedHackLoop — escreve RunSpeedUpScale e ForceSetRunAndDashSpeed
    // em PlayerAttributes. Restaura ao desativar.
    // -------------------------------------------------------------------------
    void SpeedHackLoop(uint32_t il2cpp)
    {
        float origSpeed = 1.0f, origDash = 1.0f;
        bool  hasOrig = false;
        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.SpeedHack) {
                if (hasOrig) {
                    uint32_t lp = GetLocalPlayer(il2cpp);
                    if (lp) {
                        uint32_t at = g_Memory->Read<uint32_t>(lp + Off::PlayerAttributes);
                        if (at) {
                            g_Memory->Write<float>(at + Off::RunSpeedUpScale,         origSpeed);
                            g_Memory->Write<float>(at + Off::ForceSetRunAndDashSpeed, origDash);
                        }
                    }
                    hasOrig = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            g_ExploitDbg.localPlayer.store(lp);
            if (lp) {
                uint32_t at = g_Memory->Read<uint32_t>(lp + Off::PlayerAttributes);
                g_ExploitDbg.playerAttribs.store(at);
                if (at) {
                    float scale = g_Options.AimDMA.SpeedScale;
                    if (!hasOrig) {
                        origSpeed = g_Memory->Read<float>(at + Off::RunSpeedUpScale);
                        origDash  = g_Memory->Read<float>(at + Off::ForceSetRunAndDashSpeed);
                        hasOrig = true;
                    }
                    // debug: lê o que está na memória antes de escrever
                    g_ExploitDbg.speedScaleRead  = g_Memory->Read<float>(at + Off::RunSpeedUpScale);
                    g_ExploitDbg.dashScaleRead   = g_Memory->Read<float>(at + Off::ForceSetRunAndDashSpeed);
                    g_ExploitDbg.speedScaleWrite = scale;
                    g_Memory->Write<float>(at + Off::RunSpeedUpScale,         scale);
                    g_Memory->Write<float>(at + Off::ForceSetRunAndDashSpeed, scale);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // -------------------------------------------------------------------------
    // MaximSkillLoop — Maxim StrongMedicine (EatSpeedScale 0x60)
    // EatSpeedScale e um multiplicador de TEMPO: 1.0=normal, <1.0=mais rapido.
    // Usamos EatSpeedScale = 1.0 / MaximSpeed  (1.5x => 0.666, 2.0x => 0.5)
    // -------------------------------------------------------------------------
    void MaximSkillLoop(uint32_t il2cpp)
    {
        float origEat = 1.0f;
        bool  hasOrig = false;
        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.MaximSkill) {
                if (hasOrig) {
                    uint32_t lp = GetLocalPlayer(il2cpp);
                    if (lp) {
                        uint32_t at = g_Memory->Read<uint32_t>(lp + Off::PlayerAttributes);
                        if (at) g_Memory->Write<float>(at + Off::EatSpeedScale, origEat);
                    }
                    hasOrig = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t at = g_Memory->Read<uint32_t>(lp + Off::PlayerAttributes);
                if (at) {
                    if (!hasOrig) {
                        origEat = g_Memory->Read<float>(at + Off::EatSpeedScale);
                        hasOrig = true;
                    }
                    // EatSpeedScale = 0.5 => kit em 2 segundos (limite minimo do jogo)
                    float cur = g_Memory->Read<float>(at + Off::EatSpeedScale);
                    if (fabsf(cur - 0.5f) > 0.001f)
                        g_Memory->Write<float>(at + Off::EatSpeedScale, 0.5f);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Cores ANSI
    #define DBG_RESET  "\033[0m"
    #define DBG_RED    "\033[91m"
    #define DBG_GREEN  "\033[92m"
    #define DBG_YELLOW "\033[93m"
    #define DBG_CYAN   "\033[96m"
    #define DBG_GRAY   "\033[90m"
    #define DBG_WHITE  "\033[97m"

    static const char* ok_or_fail(bool v) { return v ? DBG_GREEN "OK"    DBG_RESET : DBG_RED "FAIL"  DBG_RESET; }
    static const char* bool_str  (bool v) { return v ? DBG_GREEN "TRUE"  DBG_RESET : DBG_RED "false" DBG_RESET; }

    // -------------------------------------------------------------------------
    // DebugConsoleThread — saída compacta, 1 linha por feature, atualiza 1×/s.
    // -------------------------------------------------------------------------
    void DebugConsoleThread()
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

        while (bRunning)
        {
            auto& A  = g_Options.AimDMA;
            auto& D  = g_ExploitDbg;
            uint32_t lp = D.localPlayer.load();
            uint32_t at = D.playerAttribs.load();

            system("cls");

            // Cabeçalho mínimo
            printf(DBG_YELLOW "=== EXPLOIT DEBUG ===" DBG_RESET
                   "  LP=0x%08X  AT=0x%08X\n", lp, at);

            if (!lp || !at) {
                printf(DBG_RED "  [!] Sem ponteiros — entre em partida!\n" DBG_RESET);
            } else {
                // Speed
                if (A.SpeedHack)
                    printf("  Speed  mem=%.2f/%.2f %s  dash=%.2f %s\n",
                        D.speedScaleRead, D.speedScaleWrite,
                        ok_or_fail(D.speedScaleRead == D.speedScaleWrite),
                        D.dashScaleRead,
                        ok_or_fail(D.dashScaleRead  == D.speedScaleWrite));

                // Nenhum exploit ativo
                bool anyOn = A.SpeedHack || A.InfiniteAmmo;
                if (!anyOn)
                    printf(DBG_GRAY "  [idle] Nenhum exploit ativo.\n" DBG_RESET);
            }

            fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    // -------------------------------------------------------------------------
    // InfiniteAmmoLoop — Munição Infinita via PlayerAttributes
    //
    // Dois campos em PlayerAttributes controlam o consumo de munição:
    //   • ReloadNoConsumeAmmoclip (PA+0x98 bool) — recarga sem gastar estoque
    //     do inventário (EBuffBehavior_WeaponReloadNoConsumeAmmoclip = 97).
    //   • ShootNoReload / NoReloadFlag (PA+0x99 bool) — disparar nunca diminui
    //     o clipe; o jogador jamais precisa recarregar.
    //
    // Setar AMBOS garante munição verdadeiramente infinita:
    //   1. O clipe da arma fica sempre cheio (0x99).
    //   2. O inventário de munição nunca diminui ao recarregar (0x98).
    //
    // Ao desativar: 0x98 é sempre restaurado; 0x99 só é restaurado se NoReload
    // também estiver desligado (evita conflito com o loop NoReload que já
    // gerencia esse campo independentemente).
    // -------------------------------------------------------------------------
    void InfiniteAmmoLoop(uint32_t il2cpp)
    {
        uint8_t origReload  = 0; // valor original de PA+0x98
        uint8_t origNoRld   = 0; // valor original de PA+0x99
        bool    hasOrig     = false;

        while (bRunning) {
            if (!g_Options.AimDMA.InfiniteAmmo) {
                if (hasOrig) {
                    uint32_t lp = GetLocalPlayer(il2cpp);
                    if (lp) {
                        uint32_t at = g_Memory->Read<uint32_t>(lp + Off::LocalPlayerAttrs);
                        if (at) {
                            // Restaura ReloadNoConsumeAmmoclip (exclusivo deste loop)
                            g_Memory->Write<uint8_t>(at + Off::ReloadNoConsumeAmmoclip, origReload);
                            // Restaura ShootNoReload somente se NoReload também estiver off
                            if (!g_Options.AimDMA.NoReload)
                                g_Memory->Write<uint8_t>(at + Off::NoReloadFlag, origNoRld);
                        }
                    }
                    hasOrig = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t at = g_Memory->Read<uint32_t>(lp + Off::LocalPlayerAttrs);
                if (at) {
                    // Captura os valores originais uma única vez
                    if (!hasOrig) {
                        origReload = g_Memory->Read<uint8_t>(at + Off::ReloadNoConsumeAmmoclip);
                        origNoRld  = g_Memory->Read<uint8_t>(at + Off::NoReloadFlag);
                        hasOrig    = true;
                    }
                    // Escreve apenas quando o campo difere do desejado (reduz RPMs)
                    if (g_Memory->Read<uint8_t>(at + Off::ReloadNoConsumeAmmoclip) != 1)
                        g_Memory->Write<uint8_t>(at + Off::ReloadNoConsumeAmmoclip, (uint8_t)1);
                    if (g_Memory->Read<uint8_t>(at + Off::NoReloadFlag) != 1)
                        g_Memory->Write<uint8_t>(at + Off::NoReloadFlag, (uint8_t)1);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Cleanup ao sair com InfiniteAmmo ativo: restaura para não deixar
        // o campo travado caso o cheat seja descarregado durante o jogo.
        if (hasOrig) {
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (lp) {
                uint32_t at = g_Memory->Read<uint32_t>(lp + Off::LocalPlayerAttrs);
                if (at) {
                    g_Memory->Write<uint8_t>(at + Off::ReloadNoConsumeAmmoclip, origReload);
                    if (!g_Options.AimDMA.NoReload)
                        g_Memory->Write<uint8_t>(at + Off::NoReloadFlag, origNoRld);
                }
            }
        }
    }

    // =========================================================================
    // NOVOS LOOPS — v3 discovery (comparacao dump legivel vs dump ofuscado)
    // =========================================================================

    // -------------------------------------------------------------------------
    // AimbotScopeLoop — AimAssistOnSighting LerpTime exploit
    //
    // Quando o player abre scope (IsSighting=true), o jogo aplica aim assist
    // via uma curva f(LerpTime): forca maxima em t=0, decai com o tempo.
    // Forcar LerpTime=0 todo tick prende o assist na forca maxima permanente
    // — o crosshair "gruda" no alvo mais proximo dentro do cone de assist.
    //
    // Chain: lp+0x434 (Weapon direto) — IsSighting nao exposta como campo no dump
    //        lp+0x480 (AimAssistOnSighting) -> +0x44 (LerpTime <- 0.0f)
    //
    // Anti-detect: zero writes de rotacao/HitInfo. Comportamento identico ao
    // aim assist nativo do jogo. Custo: 1 read bool + 1 read ptr + 1 write float.
    // -------------------------------------------------------------------------
    void AimbotScopeLoop(uint32_t il2cpp)
    {
        auto isValid = [](uint32_t p) { return p >= 0x00100000u && p <= 0xEF000000u; };
        while (bRunning) {
            EMemory::BeginFrameRead();
            if (!g_Options.AimDMA.AimbotScope) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                continue;
            }
            uint32_t lp = GetLocalPlayer(il2cpp);
            if (!lp) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

            // Resolve item na mao (mesma chain do RapidFire/MoreDamage)
            uint32_t invMgr = g_Memory->Read<uint32_t>(lp + Off::AimlockInvMgr);
            if (!isValid(invMgr)) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
            uint32_t ioh = invMgr /*Off::AimlockItemOnHand=0 apos refactor 2026-09-16 - invMgr ja e o weapon*/;
            if (!isValid(ioh)) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }

            // IsSighting e propriedade calculada no Player (nao campo do weapon).
            // Sem backing field exposto no dump sem .SO. Exploit ativo sempre que
            // checkbox ligada (aim assist snap instantaneo em qualquer situacao).
            // Le ptr do aim assist e trava o timer em 0
            uint32_t aimAssist = g_Memory->Read<uint32_t>(lp + Off::Player_AimAssistOnSighting);
            if (!isValid(aimAssist)) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }

            float curTime = g_Memory->Read<float>(aimAssist + Off::AimAssist_LerpTime);
            if (fabsf(curTime) > 0.001f)
                g_Memory->Write<float>(aimAssist + Off::AimAssist_LerpTime, 0.0f);

            std::this_thread::sleep_for(std::chrono::milliseconds(20)); // 50Hz
        }
    }

    void StartAll(uint32_t il2cpp)
    {
        if (bRunning) return;
        bRunning = true;
        std::thread([il2cpp](){ ThreadCounterGuard g; NoRecoilLoop(il2cpp);     }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; AimBotMemoryLoop(il2cpp);       }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; SilentTargetScanThread(il2cpp); }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; SilentAimLoop(il2cpp);          }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; SpinBotLoop(il2cpp);            }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; AtributarArmaLoop(il2cpp);      }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; AimlockLoop(il2cpp);            }).detach();
        // GhostMode no longer runs on a background thread — driven from
        // Draw via AimModules::TickGhostMode() so key edges are caught at
        // render rate instead of a 50ms poll cycle.
        std::thread([il2cpp](){ ThreadCounterGuard g; AlokBuffLoop(il2cpp);           }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; FastMedkitLoop(il2cpp);         }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; NoReloadLoop(il2cpp);           }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; InfiniteAmmoLoop(il2cpp);      }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; PrecisionLoop(il2cpp);          }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; BugarPixelLoop(il2cpp);         }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; MoreDamageLoop(il2cpp);         }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; FireDelayLoop(il2cpp);          }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; SocoLongeLoop(il2cpp);          }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; LegitAimLoop(il2cpp);              }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; LegitAimLeftShoulderLoop(il2cpp);  }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; LegitAimRightShoulderLoop(il2cpp); }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; SpeedHackLoop(il2cpp);             }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; MaximSkillLoop(il2cpp);           }).detach();
        std::thread([il2cpp](){ ThreadCounterGuard g; AimbotScopeLoop(il2cpp);          }).detach();
        // DebugConsoleThread removida — system("cls") em loop spawnava cmd.exe
        // a cada 100 ms, causando janelas CMD piscando na tela do usuário.
    }

    void StopAll()
    {
        bRunning = false;
    }

    bool WaitAllStopped(unsigned int timeoutMs)
    {
        const DWORD deadline = GetTickCount() + timeoutMs;
        while (s_liveThreads.load(std::memory_order_acquire) > 0) {
            if ((LONG)(GetTickCount() - deadline) >= 0)
                return s_liveThreads.load(std::memory_order_acquire) == 0;
            Sleep(5);
        }
        return true;
    }
}
