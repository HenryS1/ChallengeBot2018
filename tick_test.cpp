#include "bot.hpp"
#include <fstream>
#include <vector>
#include <iostream>
#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <algorithm>

namespace tick_test {

    using namespace boost::filesystem;

    uint32_t read_command(std::string& filepath) {
        std::ifstream command_file(filepath, std::ios::in);
        if (command_file.is_open()) {
            std::string line;
            getline(command_file, line);
            if (line == "No Command") {
                return 0;
            }
            std::cout << "Line " << line << std::endl;
            std::vector<std::string> strs;
            boost::split(strs, line, boost::is_any_of(","));
            std::vector<int> result(strs.size());
            std::transform(strs.begin(), strs.end(), result.begin(),
                           [](std::string& s) { return std::stoi(s); });
            if (result.size() == 3) {
                uint16_t col = result[0];
                uint16_t row = result[1];
                uint16_t building_num = result[2];
                return ((col + (row * 8)) << 3) | (1 + building_num);
            } else {
                return 0;
            }
        } else {
            return 0;
        }
    }

    std::vector<std::string> list_directory(const path& directory) {
        std::vector<std::string> result;
        if (!exists(directory)) return result;
        directory_iterator end;
        for (directory_iterator it(directory); it != end; it++) {
            result.push_back(it->path().string());
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    bool contains(std::string& container, std::string contained) {
        return container.find(contained) != std::string::npos;
    }

    void check_player_entries_are_equal(const path& round_dir,
                                        bot::player_t& player_check,
                                        bot::player_t& player) {
        assert(player_check.energy_buildings == player.energy_buildings);
        assert(player_check.attack_building_queue == player.attack_building_queue);
        std::cout << player_check.energy_building_queue << " " << 
            player.energy_building_queue << std::endl;
        assert(player_check.energy_building_queue == player.energy_building_queue);
        std::cout << "PLAYER HEALTH " << player_check.health << " " << player.health << std::endl;
        assert(player_check.health == player.health);
        std::cout << player_check.health << " " << player.health << std::endl;
        for (uint8_t i = 0; i < 4; i++) {
            assert(player_check.attack_buildings[i] == player.attack_buildings[i]);
            std::cout << "defence_buildings[" << (int)i << "] "
                      << player_check.defence_buildings[i] << " "
                << player.defence_buildings[i] << std::endl;
            assert(player_check.defence_buildings[i] == player.defence_buildings[i]);
            assert(player_check.defence_building_queue[i] == player.defence_building_queue[i]);
        }
    }

    void check_player_missiles_are_equal(const path& round_dir,
                                         bot::player_t& player_check,
                                         bot::player_t& player) {
        uint64_t player_missiles_result = 0;
        uint64_t enemy_half_missiles_result = 0;
        for (uint8_t i = 0; i < 4; i++) {
            player_missiles_result ^= player_check.player_missiles[i];
            player_missiles_result ^= player.player_missiles[i];
            enemy_half_missiles_result ^= player_check.enemy_half_missiles[i];
            enemy_half_missiles_result ^= player.enemy_half_missiles[i];
            std::cout << "ENEMY HALF MISSILES CHECK " << player_check.enemy_half_missiles[i] 
                << " " << player.enemy_half_missiles[i] << std::endl;
        }    
        assert(player_missiles_result == 0);
        assert(enemy_half_missiles_result == 0);
    }

    void read_round_and_tick(const path& round_dir, bot::player_t& a, bot::player_t& b) {
        std::vector<std::string> dir_contents = list_directory(round_dir);
        if (dir_contents.size() == 2) {
            std::vector<std::string> a_files = list_directory(dir_contents[0]);
            std::vector<std::string> b_files = list_directory(dir_contents[1]);
            auto state_it = std::find_if(a_files.begin(), a_files.end(), 
                                   [=](std::string& s) { 
                                       return contains(s, std::string("JsonMap")); });
            auto a_command_it = std::find_if(a_files.begin(), a_files.end(), 
                                   [=](std::string& s) { 
                                       return contains(s, std::string("PlayerCommand")); });
            auto b_command_it = std::find_if(b_files.begin(), b_files.end(), 
                                   [=](std::string& s) { 
                                       return contains(s, std::string("PlayerCommand")); });
            if (state_it != a_files.end() && 
                a_command_it != a_files.end() && 
                b_command_it != b_files.end()) {
                std::string state_path = *state_it;
                std::string a_command_path = *a_command_it;
                std::string b_command_path = *b_command_it;
                bot::game_state_t game_state;
                uint16_t current_turn = bot::read_state(game_state, state_path);
                uint32_t a_command = read_command(a_command_path);
                uint32_t b_command = read_command(b_command_path);
                std::cout << "current turn " << current_turn << std::endl;
                check_player_entries_are_equal(round_dir, game_state.initial.a, a);
                check_player_entries_are_equal(round_dir, game_state.initial.b, b);
                std::cout << "check missiles a" << std::endl;
                check_player_missiles_are_equal(round_dir, game_state.initial.a, a);
                std::cout << "check missiles b" << std::endl;
                check_player_missiles_are_equal(round_dir, game_state.initial.b, b);
                bot::advance_state(a_command, b_command, a, b, current_turn);
            }
        }
    }

    void process_game(const path& game_dir) {
        std::cout << "directory " << game_dir << std::endl;
        std::vector<std::string> round_dirs = list_directory(game_dir);
        if (round_dirs.size() > 0) {
            bot::board_t board;
            std::memset(&board, 0, sizeof(bot::board_t));
            board.a.health = 100;
            board.b.health = 100;
            board.a.energy = 20;
            board.b.energy = 20;
            
            for (auto it = round_dirs.begin(); it != round_dirs.end(); it++) {
                read_round_and_tick(*it, board.a, board.b);
            }
        }
    }

}

int main(int argc, char** argv) {
    // std::vector<std::string> files = tick_test::list_directory("/Users/henrysteere/");
    // for (auto it = files.begin(); it != files.end(); it++) {
    //     std::cout << "FILENAME: " << (*it) << std::endl;
    // }
    // std::string filename("command.txt");
    // std::cout << "MOVE " << tick_test::read_command(filename) << std::endl;
    if (argc == 2) {
        std::string game_dir(argv[1]);
        tick_test::process_game(game_dir);
    } else {
        std::cout << "Provide game directory" << std::endl;
    }
    return 0;
}
