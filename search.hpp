#ifndef SEARCH_H
#define SEARCH_H

#include "bot.hpp"
#include <assert.h>
#include <stdint.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <time.h>

namespace bot {

    const float exploration = std::sqrt(2);
    uint64_t new_node_count = 0;
    const uint32_t total_free_bytes = 500000000;

    template <uint32_t N>
    struct thread_state {
        uint8_t buffer_index = 0;
        uint8_t buffer[2][N];
        float distribution[203041] = {0};
        uint32_t free_index = 0;
        thread_state() {

        }
    };

    template <uint32_t N>
    uint32_t allocate_memory(thread_state<N>& thread_state, uint32_t bytes) {
        if (bytes + thread_state.free_index >= N && thread_state.buffer_index < 1) {
            thread_state.buffer_index++;
            thread_state.free_index = 0;
        }
        uint32_t index_number = thread_state.buffer_index | (thread_state.free_index << 3);
        assert(thread_state.free_index + bytes < total_free_bytes);
        thread_state.free_index += bytes;
        return index_number;
    }

    template <uint32_t N>
    void* get_buffer_by_index(thread_state<N>& thread_state, uint32_t index) {
        return &(thread_state.buffer[index & 7][index >> 3]);
    }

    uint16_t calculate_number_of_choices(player_t& player) {
        uint16_t number_of_choices;
        uint64_t unoccupied = ~find_occupied(player);
        uint8_t available = count_set_bits(unoccupied);
        if (available == 0 && player.energy < 100) {
            number_of_choices = 1;
        } else if (player.energy < 20) {
            number_of_choices = 1;
        } else if (player.energy < 30) {
            number_of_choices = available + 1;
        } else if (player.energy < 100 || (!player.iron_curtain_available)) {
            number_of_choices = 1 + (available * 3);
        } else {
            number_of_choices = (available * 4) + 1;
        } 
        return number_of_choices;
    }

    uint8_t calculate_selected_position(uint16_t normalized_choice, uint64_t unoccupied) {
        return 64 - select_ith_bit(unoccupied, normalized_choice);
    }

    uint16_t decode_move(uint16_t player_choice,
                         player_t& player,
                         uint16_t number_of_choices) {
        uint64_t unoccupied = ~find_occupied(player);
        uint8_t available = count_set_bits(unoccupied);
        uint8_t position = calculate_selected_position(player_choice, unoccupied);
        assert(position >= 0 && position < 64);
        if (player_choice == 0 && (number_of_choices != (available * 4) + 1)) {
            return 0;
        } else if (number_of_choices == available + 1) {
            return 3 | (calculate_selected_position(player_choice, unoccupied) << 3);
        } else if (number_of_choices == 1 + (available * 3)) {
            uint8_t building_num = ((player_choice - 1) / available) + 1;
            uint16_t normalized_choice = ((player_choice - 1) % available) + 1;
            return building_num |
                (calculate_selected_position(normalized_choice, unoccupied) << 3);
        } else if (number_of_choices == (available * 4) + 1) {
            if (player_choice == 0) {
                return 5;
            } else {
                uint8_t building_num = ((player_choice - 1) / available) + 1;
                building_num = ((building_num + 1) & -(building_num == 4)) | (building_num & -(building_num < 4));
                uint16_t normalized_choice = ((player_choice - 1) % available) + 1;
                return building_num |
                    (calculate_selected_position(normalized_choice, unoccupied) << 3);
            }
        } else {
            uint8_t building_num = ((player_choice - 1) / available) + 1;
            uint16_t normalized_choice = ((player_choice - 1) % available) + 1;
            return building_num |
                (calculate_selected_position(normalized_choice, unoccupied) << 3);
        }
    }

    template <uint32_t N>
    struct player_node {
        uint16_t number_of_choices = 0;
        uint32_t children = (uint32_t)-1;
        uint32_t simulations = 0;
        uint32_t wins = 0;

        player_node() {
            number_of_choices = 0;
            children = (uint32_t)-1;
            simulations = 0;
            wins = 0;
        }

        explicit player_node(player_t& player) {
            number_of_choices = calculate_number_of_choices(player);
        }

        player_node<N>* get_children(thread_state<N>& thread_state) {
            if (children == (uint32_t)-1) {
                children = allocate_memory(thread_state, number_of_choices * sizeof(player_node));
                player_node<N>* result =
                    static_cast<player_node<N>*>(get_buffer_by_index(thread_state, children));
                for (auto node = result; node != result + number_of_choices; ++node) {
                    node->number_of_choices = 0;
                    // node->number_of_choices = 0;
                    // node->simulations = 0;
                    // node->wins = 0;
                }
                return result;
            }
            player_node<N>* result =
                static_cast<player_node<N>*>(get_buffer_by_index(thread_state, children));
            return result;
        }

    };

    template <uint32_t N>
    void update_reward(player_node<N>& node,
//                       thread_state<N>& thread_state,
                       uint8_t won) {

        // player_node<N>* selected_node = node.get_children(thread_state) + selection;
        // selected_node->wins += won;
        // selected_node->simulations++;
        node.wins += (won == 1);
        node.simulations += (won < 10);
    }

    inline float uct(uint16_t node_wins, uint16_t node_simulations, uint16_t total_simulations) {
        return ((float) node_wins / (float) node_simulations) +
            exploration * std::sqrt(std::log(total_simulations) / node_simulations);
    }

    template <uint32_t N>
    player_node<N>* construct_player_node(player_node<N>& node, player_t& player) {
        return new (&node) player_node<N>(player);
    }

    inline uint8_t count_attack_buildings(player_t& player) {
        uint64_t all_attack_buildings = 0;
        for (uint64_t* buildings = player.attack_buildings;
             buildings != player.attack_buildings + 4; buildings++) {
            all_attack_buildings |= *buildings;
        }
        return count_set_bits(all_attack_buildings);
    }

    inline uint8_t count_defence_buildings(player_t& player) {
        uint64_t all_defence_buildings = 0;
        for (uint64_t* buildings = player.defence_buildings;
             buildings != player.defence_buildings + 4; buildings++) {
            all_defence_buildings |= *buildings;
        }
        return count_set_bits(all_defence_buildings);
    }

    inline uint16_t board_score(player_t& player) {
        return 10 * (player.health > 10) +
            count_defence_buildings(player) +
            2 * count_set_bits(player.energy_buildings) + 
            3 * count_attack_buildings(player); 
    }

    uint8_t calculate_reward(player_t& self, player_t& other) {
        if ((other.health <= 0) && (board_score(self) > board_score(other)))
            return 1;
        else if (self.health > 0) return 11;
        else return 0;
    }

    template <uint32_t N>
    uint32_t select_index(player_node<N>* choices,
                          uint16_t number_of_choices,
                          uint32_t total_simulations) {

        float best = 0.;
        uint16_t best_index = 0;
        uint16_t current_index = 0;
        for (auto node = choices;
             node != choices + number_of_choices; ++node) {
            uint32_t simulations = node->simulations;
            if (simulations == 0) {
                return current_index;
            }
            float node_value = uct(node->wins, simulations, total_simulations);
            if (node_value > best) {
                best = node_value;
                best_index = current_index;
            }
            current_index++;
        }

        return best_index;
    }

    template <uint32_t N>
    void sm_mcts(std::mt19937& mt,
                 uint8_t& a_reward,
                 uint8_t& b_reward,
                 player_node<N>& a_node,
                 thread_state<N>& thread_state,
                 board_t& board,
                 uint16_t current_turn) {

        uint16_t a_index = select_index(a_node.get_children(thread_state),
                                        a_node.number_of_choices,
                                        a_node.simulations);

        assert(a_index < a_node.number_of_choices);

        player_node<N>& b_node = a_node.get_children(thread_state)[a_index];

        if (b_node.number_of_choices == 0) {

            construct_player_node(b_node, board.b);

            uint16_t a_move = decode_move(a_index, board.a, a_node.number_of_choices);

            uint16_t b_index = mt() % b_node.number_of_choices;
            uint16_t b_move = decode_move(b_index, board.b, b_node.number_of_choices);

            simulate(mt, board.a, board.b, a_move, b_move, current_turn);
            a_reward = calculate_reward(board.b, board.a);

            update_reward(a_node, a_reward);

            b_reward = calculate_reward(board.a, board.b);

            update_reward(b_node, b_reward);

        } else {

            uint16_t b_index = select_index(b_node.get_children(thread_state),
                                            b_node.number_of_choices,
                                            b_node.simulations);

            assert(b_index < b_node.number_of_choices);

            uint16_t a_move = decode_move(a_index, board.a, a_node.number_of_choices);
            uint16_t b_move = decode_move(b_index, board.b, b_node.number_of_choices);
            advance_state(a_move, b_move, board.a, board.b, current_turn);
            assert(a_index >= 0 && a_index < a_node.number_of_choices);
            player_node<N>& next_a_node = b_node.get_children(thread_state)[b_index];
            if (next_a_node.number_of_choices == 0) {
                construct_player_node(next_a_node, board.a);
            }
            sm_mcts(mt,
                    a_reward,
                    b_reward,
                    next_a_node,
                    thread_state,
                    board,
                    current_turn + 1);

            update_reward(a_node, a_reward);

            update_reward(b_node, b_reward);
        }
    }

    uint64_t find_attack_buildings(player_t& player) {
        uint64_t attack_buildings = 0;
        for (uint8_t i = 0; i < 4; i++) {
            attack_buildings |= player.attack_buildings[i];
        }
        return attack_buildings | player.attack_building_queue;
    }

    uint8_t find_energy_building_row(board_t& board) {
        uint64_t unoccupied = ~find_occupied(board.a);
        uint64_t b_attack_buildings = find_attack_buildings(board.b);
        for (uint8_t i = 0; i < 8; i++) {
            if ((((uint64_t)1 << (8 * i)) & unoccupied)
                && (!((b_attack_buildings >> (8 * i)) & 7))) {
                return i;
            }
        }
        return 65;
    }

    template <uint32_t N>
    void mcts_find_best_move(std::atomic<bool>& stop_search,
                             board_t initial_board,
                             player_node<N>* choices,
                             uint16_t current_turn) {
        std::mt19937 mt(time(0));
        std::uniform_real_distribution<float> uniform_distribution(0.0, 1.0);
        std::unique_ptr<thread_state<N>> memory(new thread_state<N>());
        uint32_t a_index = allocate_memory(*memory, sizeof(player_node<N>));
        player_node<N>* a_root =
            static_cast<player_node<N>*>(get_buffer_by_index(*memory, a_index));
        construct_player_node(*a_root, initial_board.a);
        uint8_t a_reward = 0.;
        uint8_t b_reward = 0.;
        bool done = true;
        while (!stop_search.compare_exchange_weak(done, done)) {
            done = true;
            board_t board_copy;
            copy_board(initial_board, board_copy);
            sm_mcts(mt, a_reward,
                    b_reward, *a_root, *memory, board_copy, current_turn);
        }
        std::memcpy(choices, a_root->get_children(*memory),
                    a_root->number_of_choices * sizeof(player_node<N>));
    }

    uint16_t read_board(board_t& board, std::string& state_path) {
        std::memset(&board, 0, sizeof(board));
        std::ifstream state_reader(state_path, std::ios::in);
        if (state_reader.is_open()) {
            json game_state_json;
            state_reader >> game_state_json;
            return read_from_state(board.a, board.b, game_state_json);
        }
        return -1;
    }

    void write_to_file(uint8_t row,
                       uint8_t col,
                       uint8_t building_num) {
        std::ofstream command_output("command.txt", std::ios::out);
        std::cout << "sim count " << sim_count << std::endl;
        if (command_output.is_open()) {
            if (building_num > 0) {
                building_num = building_num > 3 ? building_num + 1 : building_num;
                command_output << (int)col << "," << (int)row << "," <<
                    (int)(building_num - 1) <<
                    std::endl << std::flush;
            } else {
                command_output << std::endl << std::flush;
            }
            command_output.close();
        }
    }

    template <uint32_t N>
    void combine_choices(player_node<N>* aggregate,
                         player_node<N>* thread_rewards,
                         uint16_t number_of_choices,
                         uint32_t& total_simulations) {
        player_node<N> *aggregate_it, *thread_it;
        for (aggregate_it = aggregate, thread_it = thread_rewards;
             aggregate_it != aggregate + number_of_choices; (aggregate_it++, thread_it++)) {
            aggregate_it->wins += thread_it->wins;
            aggregate_it->simulations += thread_it->simulations;
            total_simulations += thread_it->simulations;
        }
    }

    template <uint32_t N>
    void find_best_move_and_write_to_file()  {
        board_t board;
        std::string state_path("state.json");
        uint16_t current_turn = read_board(board, state_path);
        uint16_t number_of_choices = calculate_number_of_choices(board.a);
        std::unique_ptr<player_node<N>[]>
            aggregate_choices(new player_node<N>[number_of_choices]);
        for (auto it = &(aggregate_choices[0]);
             it != &(aggregate_choices[number_of_choices]); it++) {
            new (it) player_node<N>();
        }
        if (current_turn < 13) {
            if (board.a.energy < 20) {
                write_to_file(0, 0, 0);
                return;
            }
            uint8_t energy_building_row = find_energy_building_row(board);
            if (energy_building_row < 64) {
                write_to_file(energy_building_row, 0, 3);
                return;
            }
        }
        std::atomic<bool> stop_search(false);
        if (current_turn != (uint16_t) -1) {
            player_node<N>* choices1 =
                new player_node<N>[number_of_choices];
            player_node<N>* choices2 =
                new player_node<N>[number_of_choices];
            player_node<N>* choices3 =
                new player_node<N>[number_of_choices];
            player_node<N>* choices4 =
                new player_node<N>[number_of_choices];

            std::thread thr1(mcts_find_best_move<N>,
                             std::ref(stop_search),
                             board,
                             choices1,
                             current_turn);

            std::thread thr2(mcts_find_best_move<N>,
                             std::ref(stop_search),
                             board,
                             choices2,
                             current_turn);

            std::thread thr3(mcts_find_best_move<N>,
                             std::ref(stop_search),
                             board,
                             choices3,
                             current_turn);

            std::thread thr4(mcts_find_best_move<N>,
                             std::ref(stop_search),
                             board,
                             choices4,
                             current_turn);

            std::this_thread::sleep_for(std::chrono::milliseconds(1900));
            stop_search.store(true);
            thr1.join();
            thr2.join();
            thr3.join();
            thr4.join();


            uint32_t total_simulations = 0;

            combine_choices(&(aggregate_choices[0]),
                            choices1,
                            number_of_choices,
                            total_simulations);
            combine_choices(&(aggregate_choices[0]),
                            choices2,
                            number_of_choices,
                            total_simulations);
            combine_choices(&(aggregate_choices[0]),
                            choices3,
                            number_of_choices,
                            total_simulations);
            combine_choices(&(aggregate_choices[0]),
                            choices4,
                            number_of_choices,
                            total_simulations);

            uint16_t number_of_choices = calculate_number_of_choices(board.a);

            uint16_t index_of_max_reward =
                select_index(&(aggregate_choices[0]), number_of_choices, total_simulations);
            delete[] choices1;
            delete[] choices2;
            delete[] choices3;
            delete[] choices4;
            uint16_t move = decode_move(index_of_max_reward,
                                        board.a, number_of_choices);

            uint8_t position = move >> 3;
            assert(position >= 0 && position < 64);
            uint8_t building_num = move & 7;
            uint8_t row = position >> 3;
            uint8_t col = position & 7;
            write_to_file(row, col, building_num);
        }
    }

}

#endif
