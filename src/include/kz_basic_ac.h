#ifndef KZ_BASIC_AC_H
#define KZ_BASIC_AC_H

extern void kz_ac_frame(void);
extern void kz_ac_connect(int id, edict_t* pEdict);
extern void kz_ac_cmd(int id, const usercmd_t* ucmd);
extern void kz_ac_postthink(int id, edict_t* pEdict);
extern void kz_ac_querycvar_result(const edict_t* pEdict, int requestId, const char* cvar, const char* value);
#endif