#ifndef KZ_UTIL_H
#define KZ_UTIL_H

extern int g_msg_teaminfo;

extern void fm_set_user_team(edict_t* ed, int team);
extern void fm_cs_user_spawn(edict_t* ed);
extern void fm_give_item(edict_t* ed, const char* item);

extern std::string formay_bytes(uint64_t bytes);

extern void kz_log_init(std::thread::id t);
extern void kz_log_addq(kz::queue<log_entry>* queue);
extern void kz_log_flush(uint64_t nano_delay);
extern void kz_log(kz::queue<log_entry>* queue, const char* fmt, ...);

extern uint32_t UTIL_CRC32(const void *data, size_t dataLength);
extern uint32_t UTIL_CRC32_Incremental(const void *data, size_t dataLength, uint32_t currentCrc);
extern uint32_t get_map_crc32(const char* mapname);
extern void to_base36(uint64_t value, char* dest, size_t len);
extern void replace(char* c, char token, char with);
extern size_t remove_substring(char* text, const char* what);
extern void split_net_address(const char* addr, char* ip, size_t ip_maxlen, char* port, size_t port_maxlen);
extern edict_t* find_player_by_authid(const char* authid);
extern cvar_t* register_cvar(const char* name, const char* value, int flags);

#endif
