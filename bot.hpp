#ifndef BOT_H
#define BOT_H

#define PLAYER_LENGTH sizeof(player)

#include <thread>
#include <algorithm>
#include <stdint.h>
#include <random>
#include <atomic>
#include <chrono>
#include <iostream>
#include <fstream>
#include "json.hpp"

namespace bot {

    std::atomic<uint64_t> sim_count(0);

    enum building_type_t : uint8_t {
        defence = 0,
        attack = 1,
        energy = 2
    };

    typedef uint64_t missile_positions_t;
    typedef uint64_t building_positions_t;
    typedef uint64_t tesla_tower_t;
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
        building_positions_t tesla_towers[2];
        energy_t energy;
        health_t health;
        uint8_t turns_protected;
    };

    typedef struct player player_t;

    using nlohmann::json;

    void read_player_energy_and_health(const json& player_state, player_t& a, player_t& b) {
        player_t& player = player_state.at("playerType").get<std::string>() == "A" ? a : b;
        player.energy = player_state.at("energy").get<energy_t>();
        player.health = player_state.at("health").get<health_t>();
    }

    inline uint8_t position_from_row_and_col(uint8_t row, uint8_t col) {
        if (col > 7) {
            col = 15 - col;
        }
        return ((row << 3) + (col & 7));
    }

    inline building_positions_t entity_from_coordinate(uint8_t row, uint8_t col) {
        return (building_positions_t)1 << position_from_row_and_col(row, col);
    }

    uint64_t* find_where_to_put_building(uint16_t current_turn,
                                         player_t& player,
                                         const json& building) {
        std::string building_type = building.at("buildingType").get<std::string>();
        int16_t construction_time_left = building.at("constructionTimeLeft").get<int16_t>();
        if (building_type == "TESLA") {
            return player.tesla_towers[0] ? &(player.tesla_towers[1]) : player.tesla_towers;
        } else if (building_type == "ATTACK") {
            if (construction_time_left > -1) {
                return &(player.attack_building_queue);
            } else {
                uint8_t weapon_cooldown_time = 
                    building.at("weaponCooldownTimeLeft").get<uint8_t>();
                return &(player.attack_buildings[(current_turn + (weapon_cooldown_time & 3)) & 3]);
            }
        } else if (building_type == "DEFENSE") {
            if (construction_time_left > -1) {
                return &(player.defence_building_queue[((construction_time_left - 3) 
                                                        + current_turn) % 3]);
            } else {
                return player.defence_buildings;
            }
        } else {
            if (construction_time_left > -1) {
                return &(player.energy_building_queue);
            } else {
                return &(player.energy_buildings);
            }
        }
    }

    inline tesla_tower_t make_tesla_tower(uint16_t construction_time_left, 
                                          uint8_t weapon_cooldown_time_left,
                                          uint8_t position) {
        return (weapon_cooldown_time_left << 24) | (position << 16) | construction_time_left;
    }

    void add_to_player_buildings(std::vector<json>& buildings, 
                                 player_t& player, 
                                 uint16_t current_turn) {
        for (auto building = buildings.begin(); building != buildings.end(); building++) {
            json j = *building;
            uint32_t row = j.at("y").get<int>();
            uint32_t col = j.at("x").get<int>();
            building_positions_t new_building = entity_from_coordinate(row, col);
            uint64_t* place_to_put_building = 
                find_where_to_put_building(current_turn, player, j);
            int16_t construction_time_left = j.at("constructionTimeLeft").get<int16_t>();
            std::string building_type = j.at("buildingType").get<std::string>();
            if (construction_time_left < 0 && building_type == "DEFENSE") {
                uint8_t health = j.at("health").get<uint8_t>();
                for (uint8_t i = (4 - (health / 5)); i < 4; i++) {
                    place_to_put_building[i] |= new_building;
                }
            } else if (building_type == "TESLA") {
                uint64_t position = position_from_row_and_col(row, col);
                uint64_t weapon_cooldown_time_left = 
                    j.at("weaponCooldownTimeLeft").get<uint64_t>();
                (*place_to_put_building) = make_tesla_tower(construction_time_left,
                                                            weapon_cooldown_time_left,
                                                            position);
            } else {
                (*place_to_put_building) |= new_building;
            }
        }
    }

    inline int16_t get_construction_time_left(tesla_tower_t tesla_tower) {
        return tesla_tower & 65535;
    }

    inline uint8_t get_tesla_tower_position(tesla_tower_t tesla_tower) {
        return (tesla_tower >> 16) & 127;
    }

    inline uint8_t get_weapon_cooldown_time_left(tesla_tower_t tesla_tower) {
        return tesla_tower >> 24;
    }

    inline uint32_t get_tesla_attack_row(building_positions_t constructed, uint8_t row) {
        return (constructed >> (row << 3)) & 255;
    }

    inline building_positions_t find_constructed(player_t& player) {
        building_positions_t constructed = player.energy_buildings;
        for (uint8_t i = 0; i < 4; i++) {
            constructed |= player.attack_buildings[i];
        }
        for (uint8_t i = 0; i < 4; i++) {
            constructed |= player.defence_buildings[i];
        }
        tesla_tower_t tesla_tower1 = player.tesla_towers[0];
        int16_t construction_time_left1 = get_construction_time_left(tesla_tower1);
        uint8_t position1 = get_tesla_tower_position(tesla_tower1);
        constructed |= (building_positions_t) ((construction_time_left1 < 0)
                                               & (tesla_tower1 > 0)) << position1;

        tesla_tower_t tesla_tower2 = player.tesla_towers[1];
        int16_t construction_time_left2 = get_construction_time_left(tesla_tower2);
        uint8_t position2 = get_tesla_tower_position(tesla_tower2);
        constructed |= (building_positions_t) ((tesla_tower2 > 0) 
                                               & (construction_time_left2 < 0)) << position2;

        return constructed;
    }

    inline building_positions_t find_occupied(player_t& player) {
        building_positions_t occupied = player.energy_buildings 
            | player.energy_building_queue
            | player.attack_building_queue;
        for (uint8_t i = 0; i < 4; i++) {
            occupied |= player.attack_buildings[i];
        }
        for (uint8_t i = 0; i < 4; i++) {
            occupied |= player.defence_buildings[i];
        }
        for (uint8_t i = 0; i < 4; i++) {
            occupied |= player.defence_building_queue[i];
        }
        tesla_tower_t tesla_tower1 = player.tesla_towers[0];
        uint8_t tesla_tower_position1 = get_tesla_tower_position(tesla_tower1);
        occupied |= (building_positions_t) (tesla_tower1 > 0) << tesla_tower_position1;
 
        tesla_tower_t tesla_tower2 = player.tesla_towers[1];
        uint8_t tesla_tower_position2 = get_tesla_tower_position(tesla_tower2);
        occupied |= (building_positions_t) (tesla_tower2 > 0) << tesla_tower_position2;

        return occupied;
    }

    inline uint8_t max_uint8(uint8_t one, uint8_t other) {
        return one ^ ((one ^ other) & -(one < other));
    }

    inline uint8_t min_uint8(uint8_t one, uint8_t other) {
        return other ^ ((one ^ other) & -(one < other));
    }

    inline void collide_tesla_shots(building_positions_t attacked_buildings, player_t& player) {
        if (attacked_buildings) {
            building_positions_t intersection = attacked_buildings & player.energy_buildings;
            player.energy_buildings ^= intersection;
            for (uint8_t i = 0; i < 4; i++) {
                intersection = attacked_buildings & player.attack_buildings[i];
                player.attack_buildings[i] ^= intersection;
            }
            for (uint8_t i = 0; i < 4; i++) {
                intersection = player.defence_buildings[i] & attacked_buildings;
                player.defence_buildings[i] ^= intersection;
            }
            uint64_t tesla_tower1 = player.tesla_towers[0];
            intersection = ((building_positions_t)(get_construction_time_left(tesla_tower1) < 0)
                            << get_tesla_tower_position(tesla_tower1)) & attacked_buildings;
            tesla_tower1 &= ((building_positions_t) -(intersection == 0));
            uint64_t tesla_tower2 = player.tesla_towers[1];
            intersection = ((building_positions_t)(get_construction_time_left(tesla_tower2) < 0)
                            << get_tesla_tower_position(tesla_tower2)) & attacked_buildings;
            tesla_tower2 &= ((building_positions_t) -(intersection == 0));
            player.tesla_towers[0] = (-(tesla_tower1 == 0) & tesla_tower2) | tesla_tower1;
            player.tesla_towers[1] = (-(tesla_tower1 > 0) & tesla_tower2);
        }
    }

    inline void harm_enemy(tesla_tower_t tesla_tower, player_t& player, player_t& enemy) {
        uint8_t col = get_tesla_tower_position(tesla_tower) & 7;
        int16_t construction_time_left = get_construction_time_left(tesla_tower);
        uint8_t weapon_cooldown_time_left = get_weapon_cooldown_time_left(tesla_tower);
        enemy.health -= (((col < 7) | (construction_time_left > -1) | (player.turns_protected > 0) |
                          (weapon_cooldown_time_left > 0) | (player.energy < 100)) - 1) & 20;
    }

    inline building_positions_t determine_attacked_buildings(player_t& player,
                                                             player_t& enemy, 
                                                             tesla_tower_t tesla_tower) {
        int16_t construction_time_left = get_construction_time_left(tesla_tower);
        uint8_t weapon_cooldown_time_left = get_weapon_cooldown_time_left(tesla_tower);
        uint8_t position = get_tesla_tower_position(tesla_tower);
        uint8_t row = position >> 3;
        uint8_t col = position & 7;
        building_positions_t constructed = find_constructed(enemy);
        uint8_t upper_coordinate = min_uint8(row, row - 1);
        uint8_t lower_coordinate = max_uint8(row, (row + 1) & 7);
        uint32_t hit_mask = (uint32_t)-1 << min_uint8(15 - col - 9, 0);
        uint64_t upper_row = (get_tesla_attack_row(constructed, upper_coordinate) & hit_mask);
        uint64_t middle_row = (get_tesla_attack_row(constructed, row) & hit_mask);
        middle_row ^= (middle_row & upper_row);
        uint64_t lower_row = (get_tesla_attack_row(constructed, lower_coordinate) & hit_mask);
        lower_row ^= (lower_row & (middle_row | upper_row));
        building_positions_t attacked_buildings = (upper_row << (upper_coordinate << 3)) 
            | (middle_row << (row << 3)) | (lower_row << (lower_coordinate << 3));
        return (((tesla_tower == 0) | (construction_time_left > -1)
                 | (weapon_cooldown_time_left > 0) | (player.energy < 100)) - 1) & attacked_buildings;
    }

    inline void set_tesla_tower_cooldown(player_t& player, uint8_t tesla_index) {
        tesla_tower_t tesla_tower = player.tesla_towers[tesla_index];
        uint8_t weapon_cooldown_time = get_weapon_cooldown_time_left(tesla_tower);
        tesla_tower ^= ((uint64_t)weapon_cooldown_time << 24);
        tesla_tower |= ((uint64_t)min_uint8(weapon_cooldown_time - 1, weapon_cooldown_time) << 24);
        player.tesla_towers[tesla_index] = tesla_tower;
    }

    inline void decrement_tesla_tower_construction_time(player_t& player, uint8_t tesla_index) {
        tesla_tower_t tesla_tower = player.tesla_towers[tesla_index];
        uint16_t construction_time_left = get_construction_time_left(tesla_tower);
        tesla_tower ^= construction_time_left;
        uint16_t new_construction_time_left = (((tesla_tower == 0) - 1) 
                                               & (construction_time_left - 1));
        tesla_tower |= new_construction_time_left;
        player.tesla_towers[tesla_index] = tesla_tower;
    }

    inline uint64_t fire_from_tesla_tower(player_t& player, player_t& enemy, uint8_t tesla_index) {
        uint64_t tesla_tower = player.tesla_towers[tesla_index];
        building_positions_t attacked_buildings = 
            determine_attacked_buildings(player, enemy, tesla_tower);
        int16_t construction_time_left = get_construction_time_left(tesla_tower);
        uint8_t weapon_cooldown_time_left = get_weapon_cooldown_time_left(tesla_tower);
        set_tesla_tower_cooldown(player, tesla_index);   

        energy_t energy = player.energy;
        uint64_t didnt_fire = ((construction_time_left > -1) | 
                          (weapon_cooldown_time_left > 0) | (energy < 100));

        harm_enemy(tesla_tower, player, enemy);

        player.energy -= (didnt_fire - 1) & 100;

        player.tesla_towers[tesla_index] |= ((uint64_t)((didnt_fire - 1) & 10) << 24);

        return attacked_buildings;
    }

    inline building_positions_t fire_from_tesla_towers(player_t& player, player_t& enemy) {
        if (player.tesla_towers[0]) {
            building_positions_t attacked_buildings = fire_from_tesla_tower(player, enemy, 0);
            attacked_buildings |= fire_from_tesla_tower(player, enemy, 1);
            return attacked_buildings & -(enemy.turns_protected == 0);
        }
        return 0;
    }

    uint64_t* get_missile_index(uint8_t col, std::string& player_type, player_t& player) {
        if (player_type == "A") {
            return col > 7 ? player.enemy_half_missiles : player.player_missiles;
        } else {
            return col > 7 ? player.player_missiles : player.enemy_half_missiles;
        }
    }

    void add_to_player_missiles(std::vector<json>& missiles,
                                player_t& a,
                                player_t& b) {
        uint8_t missiles_offset = 0;
        for (auto missile = missiles.begin(); missile != missiles.end(); 
             missile++, missiles_offset++) {
            json j = *missile;
            uint8_t row = j.at("y").get<uint8_t>();
            uint8_t col = j.at("x").get<uint8_t>();
            missile_positions_t new_missile = entity_from_coordinate(row, col);
            std::string player_type = j.at("playerType").get<std::string>();
            player_t& player = player_type == "A" ? a : b;
            uint64_t* place_to_put_missile = get_missile_index(col, player_type, player);
            place_to_put_missile[missiles_offset] |= new_missile;
        }
    }

    inline void sort_tesla_towers_by_construction_time(player_t& player) {
        uint64_t tesla_tower_1 = player.tesla_towers[0];
        uint64_t tesla_tower_2 = player.tesla_towers[1];
        if (tesla_tower_1 && tesla_tower_2) {
            if (get_construction_time_left(tesla_tower_1) > 
                get_construction_time_left(tesla_tower_2)) {

                player.tesla_towers[0] = tesla_tower_2;
                player.tesla_towers[1] = tesla_tower_1;
            }
        }
    }

    void read_buildings_and_missiles_from_map(const json& game_map,
                                              player_t& a,
                                              player_t& b,
                                              uint16_t current_turn) {
        std::vector<json> rows = game_map;
        for (auto row = rows.begin(); row != rows.end(); row++) {
            for (auto c = row->begin(); c != row->end(); c++) {
                json j = *c;
                player_t& player = j.at("cellOwner").get<std::string>() == "A" ? a : b;
                std::vector<json> buildings = j.at("buildings").get<std::vector<json>>();
                add_to_player_buildings(buildings, 
                                        player, 
                                        current_turn);
                std::vector<json> missiles = j.at("missiles").get<std::vector<json>>();
                add_to_player_missiles(missiles, a, b);
            }
        }
        sort_tesla_towers_by_construction_time(a);
        sort_tesla_towers_by_construction_time(b);
    }

    uint16_t read_from_state(player_t& a, player_t& b, const json& state) {
        json game_details = state.at("gameDetails");
        uint16_t current_turn = game_details.at("round").get<uint16_t>();
        json game_map = state.at("gameMap");
        read_buildings_and_missiles_from_map(game_map, a, b, current_turn);
        std::vector<json> players = state.at("players");
        for (auto p = players.begin(); p != players.end(); p++) {
            read_player_energy_and_health(*p, a, b);
        }
        return current_turn;
    }

    struct board {
        player_t a;
        player_t b;
    };

    typedef struct board board_t;

    struct game_state {
        board_t initial;
        board_t search1;
        board_t search2;
        board_t search3;
        board_t search4;
        std::atomic<uint32_t> move_scores[512];
        std::atomic<bool> stop_search;
    };

    typedef game_state game_state_t;

    const uint64_t max_u_int_64 = 18446744073709551615ULL;

    const uint64_t leading_column_mask = 9259542123273814144ULL;

    const uint64_t enemy_hits_mask = 72340172838076673ULL;

    const uint64_t first_zeros_mask = ~enemy_hits_mask;

    inline void move_current_missiles(uint8_t offset, 
                                      player_t& player,
                                      player_t& enemy) {
        uint64_t player_half_missiles = player.player_missiles[offset];
        player.enemy_half_missiles[offset] = 
            (player_half_missiles & leading_column_mask & -(enemy.turns_protected == 0)) | 
            ((player.enemy_half_missiles[offset] & first_zeros_mask) >> 1);
        player.player_missiles[offset] = first_zeros_mask & 
            (player_half_missiles << 1);
    }

    inline void move_missiles(player_t& a, player_t& b) {
        move_current_missiles(0, a, b);
        move_current_missiles(1, a, b);
        move_current_missiles(2, a, b);
        move_current_missiles(3, a, b);
        move_current_missiles(0, b, a);
        move_current_missiles(1, b, a);
        move_current_missiles(2, b, a);
        move_current_missiles(3, b, a);
    }

    inline void collide_current_missiles(player_t& player, 
                                         player_t& enemy,
                                         uint8_t missiles_offset) {
        building_positions_t enemy_missiles = enemy.enemy_half_missiles[missiles_offset];
        building_positions_t intersection = enemy_missiles & player.energy_buildings;
        player.energy_buildings ^= intersection;
        enemy_missiles ^= intersection;
        for (uint8_t i = 0; i < 4; i++) {
            intersection = enemy_missiles & player.attack_buildings[i];
            player.attack_buildings[i] ^= intersection;
            enemy_missiles ^= intersection;
        }
        for (uint8_t i = 0; i < 4; i++) {
            intersection = player.defence_buildings[i] & enemy_missiles;
            player.defence_buildings[i] ^= intersection;
            enemy_missiles ^= intersection;
        }
        if (player.tesla_towers[0]) {
            uint64_t tesla_tower1 = player.tesla_towers[0];
            intersection = ((building_positions_t)(get_construction_time_left(tesla_tower1) < 0)
                            << get_tesla_tower_position(tesla_tower1)) & enemy_missiles;
            tesla_tower1 &= ((building_positions_t) -(intersection == 0));
            enemy_missiles ^= intersection;
            uint64_t tesla_tower2 = player.tesla_towers[1];
            intersection = ((building_positions_t)(get_construction_time_left(tesla_tower2) < 0)
                            << get_tesla_tower_position(tesla_tower2)) & enemy_missiles;
            tesla_tower2 &= ((building_positions_t) -(intersection == 0));
            enemy_missiles ^= intersection;
            player.tesla_towers[0] = (-(tesla_tower1 == 0) & tesla_tower2) | tesla_tower1;
            player.tesla_towers[1] = (-(tesla_tower1 > 0) & tesla_tower2);
        }
        enemy.enemy_half_missiles[missiles_offset] &= enemy_missiles;
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
        return a & ((~a) >> 63);
    }

    inline uint8_t count_zero_bits(uint64_t n) {
        return 64 - count_set_bits(n);
    }

    inline void harm_enemy_with_current_missiles(player_t& player, 
                                                 player_t& enemy,
                                                 uint8_t offset) {
        uint8_t collision_count = count_set_bits(
             enemy_hits_mask & player.enemy_half_missiles[offset]);
        enemy.health = std::max(0, (int16_t) enemy.health - (5 * collision_count));
    }

    inline void harm_enemy(player_t& player, player_t& enemy) {
        harm_enemy_with_current_missiles(player, enemy, 0);
        harm_enemy_with_current_missiles(player, enemy, 1);
        harm_enemy_with_current_missiles(player, enemy, 2);
        harm_enemy_with_current_missiles(player, enemy, 3);
    }

    inline uint8_t select_ith_bit(uint64_t n, uint64_t i) {
        uint64_t a, b, c, d, f = 64;
        uint64_t e;
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

    inline void queue_attack_building(building_positions_t new_building, player_t& player) {
        player.attack_building_queue |= new_building;
    }

    inline void queue_energy_building(building_positions_t new_building, player_t& player) {
        player.energy_building_queue |= new_building;
    }

    inline void queue_defence_building(building_positions_t new_building, 
                                       player_t& player,
                                       uint16_t current_turn) {
        player.defence_building_queue[current_turn % 3] |= new_building;
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
        for (uint8_t i = 0; i < 4; i++) {
            player.defence_buildings[i] |= new_building;
        }
        player.defence_building_queue[index] = 0;
    }

    inline void fire_missiles(player_t& player, uint16_t current_turn) {
        uint8_t offset = mod4(current_turn);
        player.player_missiles[offset] |= player.attack_buildings[offset];
    }

    inline void queue_building(uint8_t position, 
                               uint8_t building_num,
                               player_t& player, 
                               uint8_t current_turn) {
        building_positions_t new_building = (building_positions_t)1 << position;
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
            break;
        case 5: {
            tesla_tower_t new_tesla_tower = make_tesla_tower(9, 0, position);
            tesla_tower_t original_tower = player.tesla_towers[0];
            player.tesla_towers[0] |= -(original_tower == 0) & new_tesla_tower;
            player.tesla_towers[1] |= -((original_tower > 0) & (player.tesla_towers[1] == 0)) 
                & new_tesla_tower;
            player.energy -= 100;
            break;
        }
        case 6:
            player.turns_protected = 6;
            player.energy -= 100;
            break;
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
            return 3 | (position << 3);
        } else if (player.energy > 29) {
            position = select_position(mt, occupied);
            uint8_t building_num = (mt() % 3) + 1;
            return building_num | (position << 3);
        } else {
            return 0;
        }
    }

    inline uint8_t get_position(uint16_t move) {
        return move >> 3;
    }

    inline uint8_t get_building_num(uint16_t move) {
        return move & 7;
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
        move_missiles(a, b);
        collide_missiles(a, b);
        collide_missiles(b, a);
    }

    inline void decrement_tesla_towers_construction_time_left(player_t& player) {
        decrement_tesla_tower_construction_time(player, 0);
        decrement_tesla_tower_construction_time(player, 1);
    }

    inline void fire_and_collide_tesla_shots(player_t& a, player_t& b) {
        if ((a.tesla_towers[0] | b.tesla_towers[0])) {
            building_positions_t attacked_b = fire_from_tesla_towers(a, b);
            building_positions_t attacked_a = fire_from_tesla_towers(b, a);
            collide_tesla_shots(attacked_b, b);
            collide_tesla_shots(attacked_a, a);
        }
    }

    inline void decrement_turns_protected(player_t& player) {
        player.turns_protected -= (player.turns_protected > 0);
    }

    inline void advance_state(uint16_t a_move, 
                              uint16_t b_move,
                              player_t& a, 
                              player_t& b,
                              uint16_t current_turn) {
        decrement_tesla_towers_construction_time_left(a);
        decrement_tesla_towers_construction_time_left(b);
        build_buildings(a, current_turn);
        build_buildings(b, current_turn);
        make_move(a_move, a, current_turn);
        make_move(b_move, b, current_turn);
        fire_missiles(a, current_turn);
        fire_missiles(b, current_turn);
        fire_and_collide_tesla_shots(a, b);
        move_and_collide_missiles(a, b);
        move_and_collide_missiles(a, b);
        increment_energy(a);
        increment_energy(b);
        decrement_turns_protected(a);
        decrement_turns_protected(b);
    }

    inline uint16_t simulate(std::mt19937& mt, player_t& a, player_t& b, uint16_t current_turn) {
        if (a.health > 0 && b.health > 0) {
            uint16_t initial_a_move = select_move(mt, a);
            uint16_t initial_b_move = select_move(mt, b);
            advance_state(initial_a_move, initial_b_move, a, b, current_turn);
            while (a.health > 0 && b.health > 0) {
                uint16_t a_move = select_move(mt, a);
                uint16_t b_move = select_move(mt, b);
                advance_state(a_move, b_move, a, b, current_turn);
            }
            return initial_a_move;
        } else {
            return 0;
        }
    }

    inline void mc_search(board_t& initial, board_t& search_board, 
                          std::atomic<uint32_t>* move_scores,
                          std::atomic<bool>& stop_search,
                          uint16_t current_turn) {
        std::mt19937 mt;
        bool done = true;
        player_t& a = search_board.a;
        player_t& b = search_board.b;
        copy_board(initial, search_board);
        while (!stop_search.compare_exchange_weak(done, done)) {
            done = true;
            uint16_t first_move = first_move = simulate(mt, a, b, current_turn);
            sim_count++;
            uint16_t index = (get_building_num(first_move) << 7) | (get_position(first_move) << 1);
            if (b.health > 0) {
                move_scores[index + 1]++;
            } else {
                move_scores[index]++;
            }
            copy_board(initial, search_board);
        }
    }

    void write_command_to_file(uint8_t row, 
                               uint8_t col,
                               uint8_t building_num) {
        std::ofstream command_output("command.txt", std::ios::out);
        std::cout << "sim count " << sim_count << std::endl;
        if (command_output.is_open()) {
            if (building_num > 0) {
                command_output << (int)col << "," << (int)row << "," << 
                    (int)(building_num - 1) << 
                    std::endl << std::flush;
            } else {
                command_output << std::endl << std::flush;
            }
            command_output.close();
        }
    }

    inline void find_best_move(game_state_t& game_state, uint16_t current_turn) {
        
        game_state.stop_search.store(false);

        std::thread search1(mc_search, std::ref(game_state.initial), 
                            std::ref(game_state.search1),
                            game_state.move_scores,
                            std::ref(game_state.stop_search),
                            current_turn);
        std::thread search2(mc_search, std::ref(game_state.initial), 
                            std::ref(game_state.search2),
                            game_state.move_scores,
                            std::ref(game_state.stop_search),
                            current_turn);
        std::thread search3(mc_search, std::ref(game_state.initial), 
                            std::ref(game_state.search3),
                            game_state.move_scores,
                            std::ref(game_state.stop_search),
                            current_turn);
        std::thread search4(mc_search, std::ref(game_state.initial), 
                            std::ref(game_state.search4),
                            game_state.move_scores,
                            std::ref(game_state.stop_search),
                            current_turn);

        std::this_thread::sleep_for(std::chrono::milliseconds(1950));
        game_state.stop_search.store(true);
        
        std::atomic<uint32_t>* move_scores = game_state.move_scores;

        uint64_t best_wins = 0;
        uint64_t best_losses = 0;
        uint8_t best_position = 0;
        uint8_t best_building_num = 0;

        for (uint16_t i = 0; i < 256; i++) {
            uint16_t index = i << 1;
            if (move_scores[index] > 0) {

                uint64_t wins = move_scores[index];
                uint64_t losses = move_scores[index + 1];
                if (best_wins == 0 || (wins * best_losses > best_wins * losses)) {
                    best_wins = wins;
                    best_losses = losses;
                    best_building_num = index >> 7;
                    best_position = (index & 127) >> 1;
                }
            }
        }
        uint8_t row = best_position >> 3;
        uint8_t col = best_position & 7;

        // std::cout << "best row " << (int) best_position << std::endl;
        // std::cout << "best col " << (int) best
        write_command_to_file(row, col, best_building_num);

        search1.join();
        search2.join();
        search3.join();
        search4.join();
    }

    uint16_t read_state(game_state_t& game_state, std::string& state_path) {
        std::memset(&(game_state.initial), 0, sizeof(board));
        std::memset(&(game_state.search1), 0, sizeof(board));
        std::memset(&(game_state.search2), 0, sizeof(board));
        std::memset(&(game_state.search3), 0, sizeof(board));
        std::memset(&(game_state.search4), 0, sizeof(board));
        for (int i = 0; i < 512; i++) {
            game_state.move_scores[i] = 0;
        }
        std::ifstream state_reader(state_path, std::ios::in);
        if (state_reader.is_open()) {
            json game_state_json;
            state_reader >> game_state_json;
            return read_from_state(game_state.initial.a, game_state.initial.b, game_state_json);
        }
        return -1;
    }
    
    void move_and_write_to_file() {
        game_state_t game_state;
        std::string state_path("state.json");
        uint16_t current_turn = read_state(game_state, state_path);
        if (current_turn != (uint16_t) -1) {
            find_best_move(game_state, current_turn);
        }
    }


}

#endif
