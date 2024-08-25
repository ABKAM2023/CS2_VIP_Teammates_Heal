#include <stdio.h>
#include "vip_teammates_heal.h"
#include <sstream>
#include "schemasystem/schemasystem.h"

HealModule g_HealModule;

IUtilsApi* g_pUtils = nullptr;
IVIPApi* g_pVIPCore = nullptr;
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
IPlayersApi* g_pPlayers = nullptr;

bool g_bCanHeal[64];
float fEffectTime = 1.2f;

char g_szWeaponBlackList[1024] = "weapon_molotov;weapon_hegrenade;";
int g_iMaxHP = 100;
int g_iMaxShotHP = 50;

bool g_bSyringeEffectEnabled = true;

PLUGIN_EXPOSE(HealModule, g_HealModule);

bool HealModule::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

    g_SMAPI->AddListener(this, this);

    {
        KeyValues* pKV = new KeyValues("VIP_Teammates_Heal");
        const char* pszPath = "addons/configs/vip_teammates_heal.ini";

        if (!pKV->LoadFromFile(g_pFullFileSystem, pszPath))
        {
            Warning("Failed to load %s\n", pszPath);
            return false;
        }

        g_bSyringeEffectEnabled = pKV->GetInt("syringe_effect", 1) != 0;
        fEffectTime = pKV->GetFloat("effect_time", 1.2f);
        const char* blacklist = pKV->GetString("weapon_blacklist", "weapon_molotov;weapon_hegrenade;");
        strncpy(g_szWeaponBlackList, blacklist, sizeof(g_szWeaponBlackList) - 1);

        delete pKV;
    }

    return true;
}

void OnStartupServer()
{
    g_pGameEntitySystem = g_pUtils->GetCGameEntitySystem();
    g_pEntitySystem = g_pUtils->GetCEntitySystem();
}

bool HealModule::Unload(char* error, size_t maxlen)
{
    delete g_pVIPCore;
    delete g_pUtils;
    delete g_pPlayers;
    return true;
}

CGameEntitySystem* GameEntitySystem()
{
    return g_pUtils->GetCGameEntitySystem();
}

CBasePlayerWeapon* GetWeaponFromController(CCSPlayerController* pController) {
    if (!pController) return nullptr;

    CHandle<CBasePlayerPawn> hPawn = pController->GetPawn()->GetHandle();
    CBasePlayerPawn* pPawn = hPawn;

    if (pPawn && pPawn->m_pWeaponServices) {
        CHandle<CBasePlayerWeapon> hWeapon = pPawn->m_pWeaponServices->m_hActiveWeapon;
        return hWeapon;
    }

    return nullptr;
}

void ApplySyringeEffect(CCSPlayerController* pVictimController)
{
    if (g_bSyringeEffectEnabled && pVictimController) {
        CCSPlayerPawn* pVictimPawn = pVictimController->GetPlayerPawn();
        if (pVictimPawn) {
            pVictimPawn->m_flHealthShotBoostExpirationTime().m_Value() = g_pUtils->GetCGlobalVars()->curtime + fEffectTime;
            g_pUtils->SetStateChanged(pVictimPawn, "CCSPlayerPawn", "m_flHealthShotBoostExpirationTime");
        }
    }
}

bool OnTakeDamage(int iVictimSlot, CTakeDamageInfo& pInfo)
{
    if (pInfo.m_bitsDamageType & DMG_FALL) {
        return true;
    }

    CCSPlayerPawn* pAttackerPawn = (CCSPlayerPawn*)pInfo.m_hAttacker.Get();
    if (!pAttackerPawn) {
        return true;
    }

    int iAttackerSlot = pAttackerPawn->m_hController()->GetEntityIndex().Get() - 1;
    if (iAttackerSlot == -1) {
        return true;
    }

    if (iVictimSlot == iAttackerSlot) {
        return true;
    }

    CCSPlayerController* pVictimController = CCSPlayerController::FromSlot(iVictimSlot);
    CCSPlayerController* pAttackerController = CCSPlayerController::FromSlot(iAttackerSlot);

    if (!pVictimController || !pAttackerController) {
        return true;
    }

    int iVictimTeam = pVictimController->m_iTeamNum();
    int iAttackerTeam = pAttackerController->m_iTeamNum();
    float iDamage = pInfo.m_flDamage;

    if (!g_bCanHeal[iAttackerSlot]) {
        return true;
    }

    if (iVictimTeam == iAttackerTeam) {
        int iHealth = pVictimController->m_iPawnHealth();
        int iMaxHealth = g_iMaxHP;

        CBasePlayerWeapon* pWeapon = GetWeaponFromController(pAttackerController);
        if (pWeapon) {
            const char* szWeaponClassname = pWeapon->GetClassname();
            if (strstr(g_szWeaponBlackList, szWeaponClassname) == nullptr) {
                if (iHealth < iMaxHealth) {
                    const char* healPercentageStr = g_pVIPCore->VIP_GetClientFeatureString(iAttackerSlot, "heal_teammates");
                    float healPercentage = atof(healPercentageStr) / 100.0f;

                    int iHealAmount = static_cast<int>(iDamage * healPercentage);
                    if (iHealAmount > g_iMaxShotHP) {
                        iHealAmount = g_iMaxShotHP;
                    }
                    iHealth += iHealAmount;
                    if (iHealth > iMaxHealth) {
                        iHealth = iMaxHealth;
                    }

                    pVictimController->m_hPlayerPawn()->m_iMaxHealth() = iMaxHealth;
                    pVictimController->m_hPlayerPawn()->m_iHealth() = iHealth;

                    ApplySyringeEffect(pVictimController);
                }
            }
        }
    }

    return true;
}

void OnClientAuthorized(int iSlot, uint64 iSteamID64)
{
    g_pUtils->NextFrame([iSlot]() {
        g_bCanHeal[iSlot] = g_pVIPCore->VIP_GetClientFeatureBool(iSlot, "heal_teammates");
        });
}

bool OnToggle(int iSlot, const char* szFeature, VIP_ToggleState eOldStatus, VIP_ToggleState& eNewStatus)
{
    if (eNewStatus == ENABLED)
        g_bCanHeal[iSlot] = true;
    else
        g_bCanHeal[iSlot] = false;
    return false;
}

void HealModule::AllPluginsLoaded()
{
    char error[64];
    int ret;

    g_pUtils = (IUtilsApi*)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
        ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
        std::string sBuffer = "meta unload " + std::to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }

    g_pPlayers = (IPlayersApi*)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        g_SMAPI->Format(error, sizeof(error), "Missing Players system plugin");
        ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
        std::string sBuffer = "meta unload " + std::to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }

    g_pVIPCore = (IVIPApi*)g_SMAPI->MetaFactory(VIP_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        g_pUtils->ErrorLog("[%s] Missing VIP system plugin", GetLogTag());
        std::string sBuffer = "meta unload " + std::to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }

    g_pVIPCore->VIP_RegisterFeature("heal_teammates", VIP_STRING, TOGGLABLE, nullptr, OnToggle);
    g_pUtils->StartupServer(g_PLID, OnStartupServer);
    g_pPlayers->HookOnClientAuthorized(g_PLID, OnClientAuthorized);
    g_pUtils->HookOnTakeDamagePre(g_PLID, OnTakeDamage);
}

const char* HealModule::GetLicense()
{
    return "Public";
}

const char* HealModule::GetVersion()
{
    return "1.0";
}

const char* HealModule::GetDate()
{
    return __DATE__;
}

const char* HealModule::GetLogTag()
{
    return "[VIP-Heal]";
}

const char* HealModule::GetAuthor()
{
    return "ABKAM";
}

const char* HealModule::GetDescription()
{
    return "VIP Module for healing teammates";
}

const char* HealModule::GetName()
{
    return "[VIP] Teammates Heal";
}

const char* HealModule::GetURL()
{
    return "https://discord.com/invite/g798xERK5Y";
}
