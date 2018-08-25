#ifndef SEARCH_H
#define SEARCH_H

#include "bot.hpp"
#include <assert.h>
#include <stdint.h>
#include <algorithm>
#include <random>
#include <time.h>

namespace bot {

    const float uniform_weight = std::exp(0);
    const float gamma = 0.05;
    const uint32_t total_free_bytes = 500000000;

    template <uint32_t N>
    struct thread_state {
        uint8_t buffer_index = 0;
        uint8_t buffer[8][N];
        float distribution[203041] = {0};
        uint32_t free_index = 0;
        thread_state() {

        }
    };

    thread_state<total_free_bytes>* global_tree_buffer = nullptr;

    template <uint32_t N>
    uint32_t allocate_memory(thread_state<N>& thread_state, uint32_t bytes) {
        if (bytes + thread_state.free_index >= N && thread_state.buffer_index < 7) {
            thread_state.buffer_index++;
            thread_state.free_index = 0;
        }
        uint32_t index_number = thread_state.buffer_index | (thread_state.free_index << 3);
        // std::cout << "allocated " <<  
        //     (thread_state.buffer_index * N + thread_state.free_index + bytes) << std::endl;
        assert(thread_state.free_index + bytes < total_free_bytes);
        thread_state.free_index += bytes;
        return index_number;
    }

    template <uint32_t N>
    void* get_buffer_by_index(thread_state<N>& thread_state, uint32_t index) {
        return &(thread_state.buffer[index & 7][index >> 3]);
    }

    template <uint32_t N>
    struct player_node {
        uint32_t cumulative_reward = (uint32_t)-1;
        uint16_t number_of_choices = 0;
        uint32_t children = (uint32_t)-1;
        float total_exponential_weight = 0.;

        explicit player_node(player_t& player) {
            number_of_choices = calculate_number_of_choices(player);
        }

        void initialize_total_exponential_weight() {
            total_exponential_weight = uniform_weight * number_of_choices;
        }

        player_node* get_children(thread_state<N>& thread_state) {
            if (children == (uint32_t)-1) {
                children = allocate_memory(thread_state, number_of_choices * sizeof(player_node));
            }
            player_node<N>* result =
                static_cast<player_node<N>*>(get_buffer_by_index(thread_state, children));
            for (auto node = result; node != result + number_of_choices; node++) {
                node->number_of_choices = 0;
            }
            return result;
        }

        float* allocate_cdf(thread_state<N>& thread_state) {
            float* cdf = thread_state.distribution;
            return cdf;
        }

        float* get_cumulative_reward(thread_state<N>& thread_state) {
            if (cumulative_reward == (uint32_t)-1) {
                cumulative_reward = allocate_memory(thread_state,
                                                    number_of_choices * sizeof(float));
            }
            return static_cast<float*>(get_buffer_by_index(thread_state, cumulative_reward));
        }

        uint16_t index_of_maximum_cumulative_reward(thread_state<N>& thread_state) {
            uint16_t index_of_max = 0;
            float max_cumulative_reward = 0.;
            float* cumulative_reward = get_cumulative_reward(thread_state);
            for (uint16_t i = 0; i < number_of_choices; i++) {
                float new_cumulative_reward = cumulative_reward[i];
                if (new_cumulative_reward > max_cumulative_reward) {
                    max_cumulative_reward = new_cumulative_reward;
                    index_of_max = i;
                }
            }
            return index_of_max;
        }

        uint8_t calculate_selected_position(uint16_t normalized_choice, uint64_t unoccupied) {
            return 64 - select_ith_bit(unoccupied, normalized_choice);
        }

        uint16_t decode_move(uint16_t player_choice, player_t& player) {
            uint64_t unoccupied = ~find_occupied(player);
            uint8_t available = count_set_bits(unoccupied);
            uint8_t position = calculate_selected_position(player_choice, unoccupied);
            assert(position >= 0 && position < 64);
            if (player_choice == 0) {
                return 0;
            } else if (number_of_choices == available + 1) {
                return 3 | (calculate_selected_position(player_choice, unoccupied) << 3);
            } else {
                uint8_t building_num = ((player_choice - 1) / available) + 1;
                uint16_t normalized_choice = ((player_choice - 1) % available) + 1;
                return building_num |
                    (calculate_selected_position(normalized_choice, unoccupied) << 3);
            }
        }

        uint16_t calculate_number_of_choices(player_t& player) {
            uint16_t number_of_choices;
            uint64_t unoccupied = ~find_occupied(player);
            uint8_t available = count_set_bits(unoccupied);
            if (player.energy < 20) {
                number_of_choices = 1;
            } else if (player.energy < 30) {
                number_of_choices = available + 1;
            } else if (player.energy < 100 || (!player.iron_curtain_available
                                               && !can_build_tesla_tower(player))) {
                number_of_choices = 1 + (available * 3);
            } else if (!player.iron_curtain_available) {
                number_of_choices = (available * 4) + 1;
            } else {
                number_of_choices = (available * 5) + 1;
            }
            return number_of_choices;
        }

    };

    template <uint32_t N>
    struct tree_node {
        uint32_t cumulative_reward = 0;
        uint32_t number_of_choices = 0;
        uint32_t children = 0;
        float total_exponential_weight = 0.;

        tree_node(player_t& a,
                  player_t& b) {
            uint32_t number_of_choices_a = calculate_number_of_choices(a);
            uint32_t number_of_choices_b = calculate_number_of_choices(b);
            number_of_choices = number_of_choices_a * number_of_choices_b;
        }

        tree_node* get_children(thread_state<N>& thread_state) {
            if (children == 0) {
                children = allocate_memory(thread_state, number_of_choices * sizeof(tree_node));
                // for (tree_node<N>* child = children; 
                //      child != children + number_of_choices;
                //      child++) assert(child->total_exponential_weight == 0.);
            }
            return static_cast<tree_node<N>*>(get_buffer_by_index(thread_state, children));
        }

        void initialize_total_exponential_weight() {
            total_exponential_weight = uniform_weight * number_of_choices;
        }

        float* allocate_cdf(thread_state<N>& thread_state) {
            float* cdf = thread_state.distribution;
            return cdf;
        }


        float* get_cumulative_reward(thread_state<N>& thread_state) {
            if (cumulative_reward == 0) {
                cumulative_reward = allocate_memory(thread_state,
                                                    number_of_choices * sizeof(float));
            }
            return static_cast<float*>(get_buffer_by_index(thread_state, cumulative_reward));
        }

        uint16_t index_of_maximum_cumulative_reward(thread_state<N>& thread_state) {
            uint16_t index_of_max = 0;
            float max_cumulative_reward = 0.;
            float* cumulative_reward = get_cumulative_reward(thread_state);
            for (uint16_t i = 0; i < number_of_choices; i++) {
                float new_cumulative_reward = cumulative_reward[i];
                if (new_cumulative_reward > max_cumulative_reward) {
                    max_cumulative_reward = new_cumulative_reward;
                    index_of_max = i;
                }
            }
            return index_of_max;
        }

        uint8_t calculate_selected_position(uint16_t normalized_choice, uint64_t unoccupied) {
            return 64 - select_ith_bit(unoccupied, normalized_choice);
        }

        uint16_t decode_move(uint16_t player_choice, player_t& player) {
            uint64_t unoccupied = ~find_occupied(player);
            uint8_t available = count_set_bits(unoccupied);
            uint32_t number_of_player_choices = calculate_number_of_choices(player);
            uint8_t position = calculate_selected_position(player_choice, unoccupied);
            assert(position >= 0 && position < 64);
            if (player_choice == 0) {
                return 0;
            } else if (number_of_player_choices == available + 1) {
                return 3 | (calculate_selected_position(player_choice, unoccupied) << 3);
            } else {
                uint8_t building_num = ((player_choice - 1) / available) + 1;
                uint16_t normalized_choice = ((player_choice - 1) % available) + 1;
                return building_num |
                    (calculate_selected_position(normalized_choice, unoccupied) << 3);
            }
        }

        uint16_t calculate_number_of_choices(player_t& player) {
            uint16_t number_of_choices;
            uint64_t unoccupied = ~find_occupied(player);
            uint8_t available = count_set_bits(unoccupied);
            if (player.energy < 20) {
                number_of_choices = 1;
            } else if (player.energy < 30) {
                number_of_choices = available + 1;
            } else if (player.energy < 100 || (!player.iron_curtain_available
                                               && !can_build_tesla_tower(player))) {
                number_of_choices = 1 + (available * 3);
            } else if (!player.iron_curtain_available) {
                number_of_choices = (available * 4) + 1;
            } else {
                number_of_choices = (available * 5) + 1;
            }
            return number_of_choices;
        }

    };

    template <uint32_t N>
    void update_cumulative_reward(player_node<N>& player_node,
                                  thread_state<N>& thread_state,
                                  float reward,
                                  float selection_probability,
                                  uint16_t selection) {

        float cumulative_reward_update_value = reward / selection_probability;
        float* cumulative_reward = player_node.get_cumulative_reward(thread_state);
        cumulative_reward[selection] += cumulative_reward_update_value;
        assert(selection < player_node.number_of_choices);
    }

    void construct_cdf(float* cdf, uint32_t number_of_choices) {
        for (uint32_t i = 1; i < number_of_choices; i++) {
            cdf[i] += cdf[i - 1];
        }
    }

    uint32_t sample(std::mt19937& mt,
                    std::uniform_real_distribution<float>& uniform_distribution,
                    uint32_t number_of_choices,
                    float* cdf) {
        float selection = uniform_distribution(mt) * cdf[number_of_choices - 1];
        uint16_t bottom = 0, top = number_of_choices - 1, mid = number_of_choices / 2;
        bool less_condition = (mid > 0) && (selection < cdf[mid - 1]);
        bool greater_condition = (selection > cdf[mid]);
//        std::cout << "gets here " << mid << std::endl;
//        std::cout << "number of choices " << number_of_choices << std::endl;
        while (greater_condition || less_condition) {
            if (greater_condition) {
//               std::cout << "greater" << std::endl;
                bottom = mid;
                uint32_t new_mid = (top + bottom) / 2;
                mid = (new_mid & -(mid < new_mid)) | ((mid + 1) & -(mid == new_mid));
            } else if (less_condition) {
//               std::cout << "less" << std::endl;
                top = mid;
                mid = (top + bottom) / 2;
            } else {
                return mid;
            }
//            std::cout << "mid is " << mid << std::endl;
            greater_condition = selection > cdf[mid];
            less_condition = (mid > 0) && (selection < cdf[mid - 1]);
        }
        return mid;
    }

    template <uint32_t N>
    uint32_t select_index(std::mt19937& mt,
                          std::uniform_real_distribution<float>& uniform_distribution,
                          thread_state<N>& thread_state,
                          float& selection_probability,
                          player_node<N>& player_node) {

        float* cdf = player_node.allocate_cdf(thread_state);
        float gamma_over_k = gamma / player_node.number_of_choices;
        float* cumulative_reward = player_node.get_cumulative_reward(thread_state);
        for (uint32_t i = 0; i < player_node.number_of_choices; i++) {
            //assert(cumulative_reward[i] >= 0);
            assert(gamma_over_k > 0);
            assert(1 - gamma > 0);
            cdf[i] = std::exp(gamma_over_k * cumulative_reward[i]);
            cdf[i] = (1 - gamma) * cdf[i] + gamma_over_k;
            // if (!(cdf[i] > 0)) {
            //     std::cout << "gamma over k " << gamma_over_k << std::endl;
            //     std::cout << "total cumulative_reward "
            //               << player_node.total_exponential_weight << std::endl;
            //     std::cout << "what the " << cdf[i] << std::endl;
            // }
            assert(cdf[i] > 0);
        }
        construct_cdf(cdf, player_node.number_of_choices);
        uint32_t selection = sample(mt, uniform_distribution, 
                                    player_node.number_of_choices, cdf);
        selection_probability = selection == 0 ? cdf[0] : 
            cdf[selection] - cdf[selection - 1];
        assert(selection_probability > 0);
        return selection;
    }

    template <uint32_t N>
    tree_node<N>* construct_node(tree_node<N>& node, board_t& board) {
        return new (&node) tree_node<N>(board.a, board.b);
    }

    template <uint32_t N>
    player_node<N>* construct_player_node(player_node<N>& node, player_t& player) {
        return new (&node) player_node<N>(player);
    }

    float calculate_reward(player_t& me, player_t& other) {
        if (me.health == 0) return -1.;
        else if (other.health == 0) return 1.;
        else return 0.;
    }

    uint64_t new_node_count = 0;

    template <uint32_t N>
    void sm_mcts(std::mt19937& mt,
                 std::uniform_real_distribution<float>& uniform_distribution,
                 float& a_reward,
                 float& b_reward,
                 player_node<N>& a_node,
                 player_node<N>& b_node,
                 thread_state<N>& thread_state,
                 board_t& board,
                 uint16_t current_turn) {
        if (a_node.number_of_choices == 0) {
//            std::cout << "new node" << std::endl;
            new_node_count++;
//            std::cout << "new node count " << new_node_count << std::endl;
            construct_player_node(a_node, board.a);
            construct_player_node(b_node, board.b);
            uint16_t a_index = mt() % a_node.number_of_choices;
            uint16_t a_move = a_node.decode_move(a_index, board.a);

            uint16_t b_index = mt() % b_node.number_of_choices;
            uint16_t b_move = b_node.decode_move(b_index, board.b);

            simulate(mt, board.a, board.b, a_move, b_move, current_turn);
            a_reward = calculate_reward(board.a, board.b);
            float a_uniform_density = 1. / a_node.number_of_choices;
            update_cumulative_reward(a_node, thread_state, a_reward, a_uniform_density, a_index);
            
            float b_uniform_density = 1. / b_node.number_of_choices;
            update_cumulative_reward(b_node, thread_state, b_reward, b_uniform_density, b_index);
//            std::cout << "end of new node " << result << std::endl;
        } else {
            // std::cout << "starting" << std::endl;
            // std::cout << "old node " << node << std::endl;
            // if (!node.already_visited) {
            //     for (uint32_t i = 0; i < node.number_of_choices; i++) {
            //         assert(node.get_children(thread_state)[i].number_of_choices == 0);
            //     }
            // }
            // node.already_visited = true;
            // std::cout << "done" << std::endl;
            // std::cout << "old node " << node << std::endl;
            float a_selection_probability;
            float b_selection_probability;
            uint16_t a_index = select_index(mt, uniform_distribution,
                                          thread_state, a_selection_probability, a_node);

            uint16_t b_index = select_index(mt, uniform_distribution,
                                            thread_state, b_selection_probability, b_node);

            uint16_t a_move = a_node.decode_move(a_index, board.a);
            // std::cout << "old node 2" << std::endl;
            uint16_t b_move = b_node.decode_move(b_index, board.b);
            // std::cout << "old node 3" << std::endl;
            advance_state(a_move, b_move, board.a, board.b, current_turn);
            // std::cout << "old node 4" << std::endl;
            // std::cout << "chose index " << index << " " << node->number_of_choices << std::endl;
            assert(a_index >= 0 && a_index < a_node.number_of_choices);
            sm_mcts(mt,
                    uniform_distribution,
                    a_reward,
                    b_reward,
                    a_node.get_children(thread_state)[a_index],
                    b_node.get_children(thread_state)[b_index],
                    thread_state,
                    board,
                    current_turn + 1);
            // std::cout << "old node 5" << std::endl;
            assert(a_selection_probability > 0);
            update_cumulative_reward(a_node, thread_state, a_reward,
                                     a_selection_probability, a_index);
            update_cumulative_reward(b_node, thread_state, b_reward,
                                     b_selection_probability, b_index);
            // std::cout << "end of old node" << std::endl;
        }
    }

    template <uint32_t N>
    uint16_t mcts_find_best_move(board_t& initial_board, uint16_t current_turn) {
        std::mt19937 mt(time(0));
        std::uniform_real_distribution<float> uniform_distribution(0.0, 1.0);
        std::unique_ptr<thread_state<N>> memory(new thread_state<N>());
        uint32_t index = allocate_memory(*memory, sizeof(tree_node<N>));
        player_node<N>* a_root = static_cast<player_node<N>*>(get_buffer_by_index(*memory, index));
        player_node<N>* b_root = static_cast<player_node<N>*>(get_buffer_by_index(*memory, index));
        construct_player_node(*a_root, initial_board.a);
        construct_player_node(*b_root, initial_board.b);
        uint32_t iterations = 0;
        float a_reward = 0.;
        float b_reward = 0.;
        while (iterations < 30000) {
            board_t board_copy;
            copy_board(initial_board, board_copy);
            sm_mcts(mt, uniform_distribution, a_reward, 
                    b_reward, *a_root, *b_root, *memory, board_copy, current_turn);
            iterations++;
        }
        uint16_t index_of_max_cumulative_reward = 
            a_root->index_of_maximum_cumulative_reward(*memory);
        return a_root->decode_move(index_of_max_cumulative_reward, initial_board.a);
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

    void find_best_move_and_write_to_file()  {
        board_t board;
        std::string state_path("state.json");
        uint16_t current_turn = read_board(board, state_path);
        if (current_turn != (uint16_t) -1) {
            uint16_t move = mcts_find_best_move<total_free_bytes>(board, current_turn);
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
