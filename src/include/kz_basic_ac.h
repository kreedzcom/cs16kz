#ifndef KZ_BASIC_AC_H
#define KZ_BASIC_AC_H

extern void kz_ac_frame(void);
extern void kz_ac_cmd(int id, const usercmd_t* ucmd);
extern void kz_ac_postthink(int id, edict_t* pEdict);
#endif