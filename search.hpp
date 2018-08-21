#ifndef SEARCH_H
#define SEARCH_H

#include "bot.hpp"
#include <assert.h>
#include <stdint.h>
#include <algorithm>
#include <random>

namespace bot {

    const float gamma = 0.05;
    const uint32_t total_free_bytes = 300000000;

    template <uint32_t N>
    struct thread_state {
        uint8_t buffer_index = 0;
        uint8_t buffer[6][N];
        float distribution[203041] = {0};
        uint32_t free_index = 0;
        thread_state() {
        }
    };

    thread_state<total_free_bytes>* global_tree_buffer = nullptr;

    template <uint32_t N>
    void* allocate_memory(thread_state<N>& thread_state, uint32_t bytes) {
        if (bytes + thread_state.free_index >= N && thread_state.buffer_index < 5) {
            thread_state.buffer_index++;
            thread_state.free_index = 0;
        }
        assert(thread_state.free_index + bytes < total_free_bytes);
        void* buffer_block = &(thread_state.buffer[thread_state.buffer_index]
                               [thread_state.free_index]);
        thread_state.free_index += bytes;
        assert(thread_state.free_index < total_free_bytes);
//        std::cout << "free memory index " << thread_state.free_index << std::endl;
        return buffer_block;
    }

    template <uint32_t N>
    void deallocate_memory(thread_state<N>& thread_state, uint32_t bytes) {
        assert(bytes < thread_state.free_index);
        thread_state.free_index -= bytes;
        std::memset(&(thread_state.buffer[thread_state.free_index]), 0, bytes);
    }

    template <uint32_t N>
    struct tree_node {
        float* cumulative_reward = nullptr;
        uint32_t number_of_choices = 0;
        tree_node* children = nullptr;
        float total_exponential_weight = 0.;

        tree_node(player_t& a,
                  player_t& b) {
            uint32_t number_of_choices_a = calculate_number_of_choices(a);
            uint32_t number_of_choices_b = calculate_number_of_choices(b);
            number_of_choices = number_of_choices_a * number_of_choices_b;
            // children = (tree_node*)allocate_memory(thread_state,
            //                                        number_of_choices * sizeof(tree_node));

        }

        tree_node* get_children(thread_state<N>& thread_state) {
            if (children == nullptr) {
                children = static_cast<tree_node*>(allocate_memory(thread_state,
                                                       number_of_choices * sizeof(tree_node)));
            }
            return children;
        }

        void initialize_total_exponential_weight() {
            float uniform_weight = std::exp(0);
            total_exponential_weight = uniform_weight * number_of_choices;
        }

        float* allocate_cdf(thread_state<N>& thread_state) {
            float* cdf = thread_state.distribution;
            return cdf;
        }


        float* get_cumulative_reward(thread_state<N>& thread_state) {
            if (cumulative_reward == nullptr) {
                cumulative_reward = (float*)allocate_memory(thread_state,
                                                 number_of_choices * sizeof(float));
            }
            return cumulative_reward;
        }

        uint16_t index_of_maximum_cumulative_reward() {
            uint16_t index_of_max = 0;
            float max_cumulative_reward = 0.;
            for (uint16_t i = 0; i < number_of_choices; i++) {
                float new_cumulative_reward = cumulative_reward[i];
                if (new_cumulative_reward > max_cumulative_reward) {
                    max_cumulative_reward = new_cumulative_reward;
                    index_of_max = i;
                }
            }
            return index_of_max;
        }

        uint8_t calculate_selected_position(uint16_t normalized_choice,
                                                   uint64_t unoccupied) {
            return 64 - select_ith_bit(unoccupied, normalized_choice);
        }

        uint16_t decode_move(uint16_t player_choice,
                             player_t& player) {
            uint64_t unoccupied = ~find_occupied(player);
            uint8_t available = count_set_bits(unoccupied);
            uint32_t number_of_player_choices = calculate_number_of_choices(player);
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
    void update_cumulative_reward(tree_node<N>& tree_node,
                                  thread_state<N>& thread_state,
                                  float reward,
                                  float selection_probability,
                                  uint16_t selection) {

        float cumulative_reward_update_value = reward / selection_probability;
        float* cumulative_reward = tree_node.get_cumulative_reward(thread_state);
        cumulative_reward[selection] += cumulative_reward_update_value;
        assert(selection < tree_node.number_of_choices);
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
        while (greater_condition || less_condition) {
            if (greater_condition) {
                bottom = mid;
                uint32_t new_mid = (top + bottom) / 2;
                mid = (new_mid & -(mid < new_mid)) | ((mid + 1) & -(mid == new_mid));
            } else if (less_condition) {
                top = mid;
                mid = (top + bottom) / 2;
            } else {
                return mid;
            }
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
                          tree_node<N>& tree_node) {

        float* cdf = tree_node.allocate_cdf(thread_state);
        float gamma_over_k = gamma / tree_node.number_of_choices;
        float* cumulative_reward = tree_node.get_cumulative_reward(thread_state);
        for (uint32_t i = 0; i < tree_node.number_of_choices; i++) {
            assert(cumulative_reward[i] >= 0);
            assert(gamma_over_k > 0);
            assert(1 - gamma > 0);
            cdf[i] = std::exp(gamma_over_k * cumulative_reward[i]);
            cdf[i] = (1 - gamma) * cdf[i] + gamma_over_k;
            if (!(cdf[i] > 0)) {
                std::cout << "gamma over k " << gamma_over_k << std::endl;
                std::cout << "total cumulative_reward " << tree_node.total_exponential_weight << std::endl;
                std::cout << "what the " << cdf[i] << std::endl;
            }
            assert(cdf[i] > 0);
        }
        construct_cdf(cdf, tree_node.number_of_choices);
        uint32_t selection = sample(mt, uniform_distribution, 
                                    tree_node.number_of_choices, cdf);
        selection_probability = selection == 0 ? cdf[0] : 
            cdf[selection] - cdf[selection - 1];
        assert(selection_probability > 0);
        return selection;
    }

    template <uint32_t N>
    tree_node<N>* construct_node(tree_node<N>& node, board_t& board) {
        return new (&node) tree_node<N>(board.a, board.b);
    }

    float calculate_reward(player_t& a) {
        if (a.health == 0) return -1.;
        else return 1.;
    }

    uint64_t new_node_count = 0;

    template <uint32_t N>
    void sm_mcts(std::mt19937& mt,
                 std::uniform_real_distribution<float>& uniform_distribution,
                 float& reward,
                 tree_node<N>& node,
                 thread_state<N>& thread_state,
                 board_t& board,
                 uint16_t current_turn) {
        if (node.number_of_choices == 0) {
//            std::cout << "new node" << std::endl;
            new_node_count++;
//            std::cout << "new node count " << new_node_count << std::endl;
            construct_node(node, board);
            uint32_t index = mt() % node.number_of_choices;
            uint16_t number_of_choices_a = node.calculate_number_of_choices(board.a);
            uint16_t a_move = node.decode_move(index % number_of_choices_a, board.a);
            uint16_t b_move = node.decode_move(index / number_of_choices_a, board.b);
            simulate(mt, board.a, board.b, a_move, b_move, current_turn);
            reward = calculate_reward(board.a);
            float uniform_density = 1. / node.number_of_choices;
            update_cumulative_reward(node, thread_state, reward, uniform_density, index);
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
            float selection_probability;
            uint32_t index = select_index(mt, uniform_distribution,
                                          thread_state, selection_probability, node);
            // std::cout << "old node 1" << std::endl;
            uint16_t number_of_choices_a = node.calculate_number_of_choices(board.a);
            uint16_t a_move = node.decode_move(index % number_of_choices_a, board.a);
            // std::cout << "old node 2" << std::endl;
            uint16_t b_move = node.decode_move(index / number_of_choices_a, board.b);
            // std::cout << "old node 3" << std::endl;
            advance_state(a_move, b_move, board.a, board.b, current_turn);
            // std::cout << "old node 4" << std::endl;
            // std::cout << "chose index " << index << " " << node->number_of_choices << std::endl;
            assert(index >= 0 && index < node.number_of_choices);
            sm_mcts(mt,
                    uniform_distribution,
                    reward,
                    node.get_children(thread_state)[index],
                    thread_state,
                    board,
                    current_turn + 1);
            // std::cout << "old node 5" << std::endl;
            assert(selection_probability > 0);
            update_cumulative_reward(node, thread_state, reward, selection_probability, index);
            // std::cout << "end of old node" << std::endl;
        }
    }

    template <uint32_t N>
    uint16_t mcts_find_best_move(board_t& initial_board, uint16_t current_turn) {
        std::mt19937 mt;
        std::uniform_real_distribution<float> uniform_distribution(0.0, 1.0);
        std::unique_ptr<thread_state<N>> memory(new thread_state<N>());
        tree_node<N>* root =
            static_cast<tree_node<N>*>(allocate_memory(*memory, sizeof(tree_node<N>)));
        construct_node(*root, initial_board);
        uint32_t iterations = 0;
        float reward = 0.;
        while (iterations < 40000) {
            board_t board_copy;
            copy_board(initial_board, board_copy);
            sm_mcts(mt, uniform_distribution, reward, *root, *memory, board_copy, current_turn);
            iterations++;
        }
        uint16_t index_of_max_cumulative_reward = root->index_of_maximum_cumulative_reward();
        uint16_t number_of_choices_a = root->calculate_number_of_choices(initial_board.a);
        return root->decode_move(index_of_max_cumulative_reward 
                                 % number_of_choices_a, initial_board.a);
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
            uint8_t building_num = move & 7;
            uint8_t row = position >> 3;
            uint8_t col = position & 8;
            write_to_file(row, col, building_num);
        }
    }

}


#endif
