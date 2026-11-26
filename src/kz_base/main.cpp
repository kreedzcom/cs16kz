#include "amxxmodule.h"

#include "pdata.h"
#include "kz_api.h"
#include "kz_cmd.h"
#include "kz_base.h"
#include "kz_mpbhop.h"

edict_t* g_pEdicts = nullptr;

/***************************************************************************************************************/
/***************************************************************************************************************/
int FN_AMXX_CHECKGAME(const char* game)
{
    return (FStrEq(game, "cstrike") ? AMXX_GAME_OK : AMXX_GAME_BAD);
}
void FN_AMXX_ATTACH()
{
    g_pEdicts = (*g_engfuncs.pfnPEntityOfEntIndex)(0);

    MF_RegisterFunction(reinterpret_cast<void*>(kz_base_reset_checkpoints), "kz_base_reset_checkpoints");

    kz_register_client_command("checkpoint", kz_cmd_checkpoint, false);
    kz_register_client_command("cp",         kz_cmd_checkpoint, false);

    kz_register_client_command("gocheck",    kz_cmd_gocheck,    false);
    kz_register_client_command("gc",         kz_cmd_gocheck,    false);

    kz_register_client_command("teleport",   kz_cmd_gocheck,    false);
    kz_register_client_command("tp",         kz_cmd_gocheck,    false);

}
void FN_AMXX_PLUGINSLOADED()
{
    kz_api_init();
}
void FN_ClientCommand(edict_t* pEntity)
{
    if (kz_cmd_dispatch(pEntity))
    {
        RETURN_META(MRES_SUPERCEDE);
    }
    RETURN_META(MRES_IGNORED);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void FN_DispatchKeyValue(edict_t* pentKeyvalue, KeyValueData* pkvd)
{
    if (FClassnameIs(pentKeyvalue, "worldspawn"))
    {
        g_pEdicts = pentKeyvalue;
    }
    RETURN_META(MRES_IGNORED);
}
int FN_DispatchSpawn(edict_t* pent)
{
    if (FClassnameIs(pent, "worldspawn"))
    {
        g_pEdicts = (*g_engfuncs.pfnPEntityOfEntIndex)(0);
    }
    RETURN_META_VALUE(MRES_IGNORED, FALSE);
}
void FN_ServerActivate_Post(edict_t* pEdictList, int edictCount, int clientMax)
{
    for (int id = 1; id <= clientMax; ++id)
    {
        kz_base_reset_checkpoints(id);
    }
    kz_mpbhop_init();
    RETURN_META(MRES_IGNORED);
}
void FN_ServerDeactivate(void)
{
    kz_mpbhop_uninit();
    RETURN_META(MRES_IGNORED);
}
void FN_ServerDeactivate_Post(void)
{
    g_pEdicts = nullptr;
    RETURN_META(MRES_IGNORED);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void FN_ClientPutInServer_Post(edict_t* pEntity)
{
    kz_base_reset_checkpoints(indexOfEdict(pEntity));
    kz_mpbhop_ClientPutInServer(pEntity);
    RETURN_META(MRES_IGNORED);
}
void FN_ClientDisconnect(edict_t* pEntity)
{
    kz_base_reset_checkpoints(indexOfEdict(pEntity));
    kz_mpbhop_ClientDisconnected(pEntity);
    RETURN_META(MRES_IGNORED);
}
/***************************************************************************************************************/
/***************************************************************************************************************/
void FN_DispatchTouch(edict_t* pentTouched, edict_t* pentOther)
{
    if (kz_mpbhop_DispatchTouch(pentTouched, pentOther))
    {
        RETURN_META(MRES_SUPERCEDE);
    }
    RETURN_META(MRES_IGNORED);
}
void FN_PlayerPreThink(edict_t* pEntity)
{
    kz_mpbhop_PlayerPreThink(pEntity);
    RETURN_META(MRES_IGNORED);
}
