#ifndef BOT_H
#define BOT_H

#define PLAYER_LENGTH sizeof(player)

#include <stdint.h>

namespace bot {

    typedef uint64_t missile_positions_t;
    typedef uint64_t building_positions_t;
    typedef uint16_t energy_t;
    typedef uint16_t health_t;

    struct player {
        building_positions_t energy_buildings;
        building_positions_t attack_buildings;
        building_positions_t defence_buildings[4];
        building_positions_t attack_building_queue;
        building_positions_t energy_building_queue;
        building_positions_t defence_building_queue[4];
        building_positions_t player_missiles[4];
        building_positions_t enemy_half_missiles[4];
        energy_t energy;
        health_t health;
    };

    typedef struct player player_t;

    struct board {
        player_t a;
        player_t b;
    };

    typedef struct board board_t;

    struct search_boards {
        board_t initial;
        board_t search1;
        board_t search2;
        board_t search3;
        board_t search4;
    };

    const uint64_t max_u_int_64 = 18446744073709551615ULL;

    const uint64_t leading_column_mask = (1 << 7) | (1 << 15) | (1 << 23) | (1 << 31) |
            ((uint64_t) 1 << 39) | ((uint64_t) 1 << 47) | ((uint64_t) 1 << 55) 
            | ((uint64_t) 1 << 63);

    const uint64_t enemy_hits_mask = 1 | (1 << 8) | (1 << 16) | (1 << 24) | ((uint64_t) 1 << 32) |
           ((uint64_t)1 << 40) | ((uint64_t)1 << 48) | ((uint64_t)1 << 56);

    inline void move_current_missiles(uint8_t offset, player_t player) {
        player.enemy_half_missiles[offset] = 
            (player.player_missiles[offset] & leading_column_mask) | 
            ((player.enemy_half_missiles[offset] & max_u_int_64) >> 1);
    }

    inline void move_missiles(player_t player) {
        move_current_missiles(0, player);
        move_current_missiles(1, player);
        move_current_missiles(2, player);
        move_current_missiles(3, player);
    }

    inline void collide_current_missiles(player_t player, 
                                         player_t enemy,
                                         uint8_t missiles_offset) {
        building_positions_t enemy_missiles = enemy.enemy_half_missiles[missiles_offset];
        building_positions_t intersection = enemy_missiles & player.energy_buildings;
        player.energy_buildings ^= intersection;
        enemy_missiles ^= intersection;
        intersection = enemy_missiles & player.attack_buildings;
        player.attack_buildings ^= intersection;
        enemy_missiles ^= intersection;
        for (uint8_t i = 0; i < sizeof(player.defence_buildings); i++) {
            intersection = player.defence_buildings[i] & enemy_missiles;
            player.defence_buildings[i] ^= intersection;
            enemy_missiles ^= intersection;
        }
    }

    inline void collide_missiles(player_t player, player_t enemy) {
        collide_current_missiles(player, enemy, 0);
        collide_current_missiles(player, enemy, 1);
        collide_current_missiles(player, enemy, 2);
        collide_current_missiles(player, enemy, 3);
    }

    inline uint8_t count_set_bits(uint64_t n) {
        n -= (n >> 1) & 0x5555555555555555;
        n = (n & 0x3333333333333333) + ((n >> 2) & 0x3333333333333333);
        n = (n + (n >> 4)) & 0x0F0F0F0F0F0F0F0F;
        n += n >> 8;
        n += n >> 16;
        return (n + (n >> 32)) & 0x7F;
    }

    inline uint64_t max_zero(uint64_t a) { 
        return a & (~a >> 63);
    }

    inline uint8_t count_zero_bits(uint64_t n) {
        return 64 - count_set_bits(n);
    }

    inline void harm_enemy_with_current_missiles(player_t player, 
                                                 player_t enemy,
                                                 uint8_t offset) {
        uint8_t collision_count = count_set_bits(
             enemy_hits_mask & player.enemy_half_missiles[offset]);
        enemy.health = max_zero(enemy.health - 5 * collision_count);
    }

    inline void harm_enemy(player_t player, player_t enemy) {
        harm_enemy_with_current_missiles(player, enemy, 0);
        harm_enemy_with_current_missiles(player, enemy, 1);
        harm_enemy_with_current_missiles(player, enemy, 2);
        harm_enemy_with_current_missiles(player, enemy, 3);
    }

    inline uint8_t select_ith_bit(uint64_t n, int8_t i) {
        uint64_t a, b, c, d, f = 64;
        int64_t e;
        a = n - ((n >> 1) & 0x5555555555555555);
        b = (a & 0x3333333333333333) + ((a >> 2) & 0x3333333333333333);
        c = (b + (b >> 4)) & 0x0F0F0F0F0F0F0F0F;
        d = (c + (c >> 8)) & 0x00FF00FF00FF00FF;
        e = (d >> 32) + (d >> 48);

        f -= ((e - i) & 256) >> 3;
        i -= e & ((e - i) >> 8);
        e = (d >> (f - 16)) & 0xFF;

        f -= ((e - i) & 256) >> 4;
        i -= e & ((e - i) >> 8);
        e = (c >> (f - 8)) & 0xF;
        
        f -= ((e - i) & 256) >> 5;
        i -= e & ((e - i) >> 8);
        e = (b >> (f - 4)) & 0x7;
        
        f -= ((e - i) & 256) >> 6;
        i -= e & ((e - i) >> 8);
        e = (a >> (f - 2)) & 0x3;
        
        f -= ((e - i) & 256) >> 7;
        i -= e & ((e - i) >> 8);
        e = (n >> (f - 1)) & 0x1;

        f -= ((e - i) & 256) >> 8;
        return 65 - f;
    }

    inline uint16_t select_position(building_positions_t occupied) {
        
    }

}

#endif
