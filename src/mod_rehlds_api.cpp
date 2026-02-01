#include "amxxmodule.h"
#include "mod_rehlds_api.h"

IRehldsApi*          RehldsApi;
const RehldsFuncs_t* RehldsFuncs;
IRehldsServerData*   RehldsData;
IRehldsHookchains*   RehldsHookchains;
IRehldsServerStatic* RehldsSvs;

bool RehldsApi_Init()
{
	if (!IS_DEDICATED_SERVER())
	{
		return false;
	}

#if defined(PLATFORM_WINDOWS)
	const auto library = "swds";
#elif defined(PLATFORM_POSIX)
	const auto library = "engine_i486";
#endif

	if (!GET_IFACE<IRehldsApi>(library, RehldsApi, VREHLDS_HLDS_API_VERSION) || !RehldsApi)
	{
		return false;
	}

	const auto majorVersion = RehldsApi->GetMajorVersion();
	const auto minorVersion = RehldsApi->GetMinorVersion();

	if (majorVersion != REHLDS_API_VERSION_MAJOR)
	{
		MF_Log("ReHLDS: Api major version mismatch; expected %d, real %d", REHLDS_API_VERSION_MAJOR, majorVersion);
		return false;
	}
	if (minorVersion < REHLDS_API_VERSION_MINOR)
	{
		MF_Log("ReHLDS: Api minor version mismatch; expected at least %d, real %d", REHLDS_API_VERSION_MINOR, minorVersion);
		return false;
	}

	RehldsFuncs      = RehldsApi->GetFuncs();
	RehldsData       = RehldsApi->GetServerData();
	RehldsHookchains = RehldsApi->GetHookchains();
	RehldsSvs        = RehldsApi->GetServerStatic();

	return true;
}