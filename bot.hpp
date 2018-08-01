#ifndef BOT_H
#define BOT_H

#define PLAYER_LENGTH sizeof(player)

#include <stdint.h>
#include <random>
#include <atomic>

namespace bot {

    typedef uint64_t missile_positions_t;
    typedef uint64_t building_positions_t;
    typedef uint16_t energy_t;
    typedef uint16_t health_t;

    struct player {
        building_positions_t energy_buildings;
        building_positions_t attack_buildings[4];
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
        std::atomic<uint32_t> move_scores[512];
        std::atomic<bool> stop_search;
    };

    typedef search_boards search_boards_t;

    const uint64_t max_u_int_64 = 18446744073709551615ULL;

    const uint64_t leading_column_mask = (1 << 7) | (1 << 15) | (1 << 23) | (1 << 31) |
            ((uint64_t) 1 << 39) | ((uint64_t) 1 << 47) | ((uint64_t) 1 << 55) 
            | ((uint64_t) 1 << 63);

    const uint64_t enemy_hits_mask = 1 | (1 << 8) | (1 << 16) | (1 << 24) | ((uint64_t) 1 << 32) |
           ((uint64_t)1 << 40) | ((uint64_t)1 << 48) | ((uint64_t)1 << 56);

    inline void move_current_missiles(uint8_t offset, player_t& player) {
        player.enemy_half_missiles[offset] = 
            (player.player_missiles[offset] & leading_column_mask) | 
            ((player.enemy_half_missiles[offset] & max_u_int_64) >> 1);
    }

    inline void move_missiles(player_t& player) {
        move_current_missiles(0, player);
        move_current_missiles(1, player);
        move_current_missiles(2, player);
        move_current_missiles(3, player);
    }

    inline void collide_current_missiles(player_t& player, 
                                         player_t& enemy,
                                         uint8_t missiles_offset) {
        building_positions_t enemy_missiles = enemy.enemy_half_missiles[missiles_offset];
        building_positions_t intersection = enemy_missiles & player.energy_buildings;
        player.energy_buildings ^= intersection;
        enemy_missiles ^= intersection;
        for (uint8_t i = 0; i < sizeof(player.attack_buildings); i++) {
            intersection = enemy_missiles & player.attack_buildings[i];
            player.attack_buildings[i] ^= intersection;
            enemy_missiles ^= intersection;
        }
        for (uint8_t i = 0; i < sizeof(player.defence_buildings); i++) {
            intersection = player.defence_buildings[i] & enemy_missiles;
            player.defence_buildings[i] ^= intersection;
            enemy_missiles ^= intersection;
        }
    }

    inline void collide_missiles(player_t& player, player_t& enemy) {
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

    inline void harm_enemy_with_current_missiles(player_t& player, 
                                                 player_t& enemy,
                                                 uint8_t offset) {
        uint8_t collision_count = count_set_bits(
             enemy_hits_mask & player.enemy_half_missiles[offset]);
        enemy.health = max_zero(enemy.health - 5 * collision_count);
    }

    inline void harm_enemy(player_t& player, player_t& enemy) {
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

    inline uint16_t select_position(std::mt19937& mt, 
                                    building_positions_t occupied) {
        uint8_t zero_bits = count_zero_bits(occupied);
        uint8_t selected_position = mt() % zero_bits;
        return 64 - select_ith_bit(~occupied, 1 + selected_position);
    }

    inline uint64_t find_occupied(player_t& player) {
        building_positions_t occupied = player.energy_buildings 
            | player.energy_building_queue
            | player.attack_building_queue;
        for (uint8_t i = 0; i < sizeof(player.attack_buildings); i++) {
            occupied |= player.attack_buildings[i];
        }
        for (uint8_t i = 0; i < sizeof(player.defence_buildings); i++) {
            occupied |= player.defence_buildings[i];
        }
        for (uint8_t i = 0; i < sizeof(player.defence_building_queue); i++) {
            occupied |= player.defence_building_queue[i];
        }
        return occupied;
    }

    inline void queue_attack_building(building_positions_t new_building, player_t& player) {
        player.attack_building_queue |= new_building;
    }

    inline void queue_energy_building(building_positions_t new_building, player_t& player) {
        player.energy_building_queue |= new_building;
    }

    inline void queue_defence_building(building_positions_t new_building, 
                                       player_t& player,
                                       uint16_t current_turn) {
        player.defence_buildings[current_turn % 3] |= new_building;
    }

    inline uint8_t mod4(uint16_t n) {
        return n & 3;
    }

    inline void build_attack_building(player_t& player, uint16_t current_turn) {
        player.attack_buildings[mod4(current_turn)] |= player.attack_building_queue;
        player.attack_building_queue = 0;
    }

    inline void build_energy_building(player_t& player) {
        player.energy_buildings |= player.energy_building_queue;
        player.energy_building_queue = 0;
    }

    inline void build_defence_building(player_t& player, uint8_t current_turn) {
        uint8_t index = current_turn % 3;
        building_positions_t new_building = player.defence_building_queue[index];
        for (uint8_t i = 0; i < sizeof(player.defence_buildings); i++) {
            player.defence_buildings[i] |= new_building;
        }
        player.defence_building_queue[index] = 0;
    }

    inline void fire_missiles(player_t& player, uint16_t current_turn) {
        player.player_missiles[mod4(current_turn)] |= player.attack_buildings[mod4(current_turn)];
    }

    inline void queue_building(uint8_t position, 
                               uint8_t building_num,
                               player_t& player, 
                               uint8_t current_turn) {
        building_positions_t new_building = 1 << position;
        switch (building_num) {
        case 1:
            queue_defence_building(new_building, player, current_turn);
            player.energy -= 30;
            break;
        case 2:
            queue_attack_building(new_building, player);
            player.energy -= 30;
            break;
        case 3:
            queue_energy_building(new_building, player);
            player.energy -= 20;
        }
    }

    inline uint16_t select_move(std::mt19937& mt,
                                player_t& player) {
        uint64_t occupied = find_occupied(player);
        uint16_t position = 0;
        if (occupied == max_u_int_64) {
            return 0;
        } else if (player.energy > 19 && player.energy < 30) {
            position = select_position(mt, occupied);
            return 3 + (position << 2);
        } else if (player.energy > 29) {
            position = select_position(mt, occupied);
            uint8_t building_num = (mt() % 3) + 1;
            return building_num | (position << 2);
        } else {
            return 0;
        }
    }

    inline uint8_t get_position(uint16_t move) {
        return move >> 2;
    }

    inline uint8_t get_building_num(uint16_t move) {
        return move & 3;
    }

    inline void make_move(uint16_t move, player_t& player, uint16_t current_turn) {
        if (move > 0) queue_building(get_position(move), get_building_num(move), 
                                     player, current_turn);
    }

    inline void copy_board(board_t& src, board_t& dest) {
        std::memcpy(&dest, &src, sizeof(board));
    }

    inline void increment_energy(player_t& player) {
        uint8_t energy_tower_count = count_set_bits(player.energy_buildings);
        player.energy += (energy_tower_count * 3) + 5;
    }

    inline void build_buildings(player_t& player, uint16_t current_turn) {
        build_attack_building(player, current_turn);
        build_defence_building(player, current_turn);
        build_energy_building(player);
    }

    inline void move_and_collide_missiles(player_t& a, player_t& b) {
        harm_enemy(a, b);
        harm_enemy(b, a);
        move_missiles(a);
        move_missiles(b);
        collide_missiles(a, b);
        collide_missiles(b, a);
    }

    inline void advance_state(uint16_t a_move, 
                              uint16_t b_move,
                              player_t& a, 
                              player_t& b,
                              uint16_t current_turn) {
        build_buildings(a, current_turn);
        build_buildings(b, current_turn);
        make_move(a_move, a, current_turn);
        make_move(b_move, b, current_turn);
        fire_missiles(a, current_turn);
        fire_missiles(b, current_turn);
        move_and_collide_missiles(a, b);
        move_and_collide_missiles(a, b);
        increment_energy(a);
        increment_energy(b);
    }

    inline uint16_t simulate(std::mt19937& mt, player_t& a, player_t& b, uint16_t current_turn) {
        if (a.health > 0 && b.health > 0) {
            uint16_t a_move = select_move(mt, a);
            uint16_t b_move = select_move(mt, b);
            advance_state(a_move, b_move, a, b, current_turn);
            while (a.health > 0 && b.health > 0) {
                a_move = select_move(mt, a);
                b_move = select_move(mt, b);
                advance_state(a_move, b_move, a, b, current_turn);
            }
            return a_move;
        } else {
            return 0;
        }
    }

    inline void mc_search(board_t& initial, board_t& search_board, 
                          std::atomic<uint32_t>* move_scores,
                          std::atomic<bool>& stop_search,
                          uint16_t current_turn) {
        std::mt19937 mt;
        uint16_t first_move = 0;
        uint16_t index = 0;
        bool done = true;
        player_t& a = search_board.a;
        player_t& b = search_board.b;
        copy_board(initial, search_board);
        while (!stop_search.compare_exchange_weak(done, done)) {
            first_move = simulate(mt, a, b, current_turn);
            index = (get_building_num(first_move) << 7) | (get_position(first_move) << 1);
            if (b.health > 0) {
                move_scores[index + 1]++;
            } else {
                move_scores[index]++;
            }
            copy_board(initial, search_board);
        }
    }

//    inline void find_best_move(search_boards_t& sear

}

#endif
