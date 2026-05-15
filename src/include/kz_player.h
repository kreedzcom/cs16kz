#ifndef KZ_PLAYER_H
#define KZ_PLAYER_H

typedef struct {
    char nickname[32];
    char ipaddr[16];
    char steamid[35];
    char steamid_short[35];
    bool is_bot;
} player_t;

extern player_t g_players[33];
#endif
