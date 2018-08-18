#ifndef SEARCH_H
#define SEARCH_H

#include "bot.hpp"
#include <assert.h>
#include <stdint.h>
#include <algorithm>
#include <random>

namespace bot {
    
    const float gamma = 0.05;
    const uint32_t total_free_bytes = 100000000;

    struct free_memory {
        uint8_t buffer[total_free_bytes];
        uint32_t free_index = 0;
    };

    typedef struct free_memory free_memory_t;

    void* allocate_memory(free_memory_t& free_memory, uint16_t bytes) {
        void* buffer_block = &(free_memory.buffer[free_memory.free_index]);
        free_memory.free_index += bytes;
        assert(free_memory.free_index < total_free_bytes);
        return buffer_block;
    }

    struct tree_node {
        uint16_t number_of_choices_a;
        uint16_t number_of_choices_b;
        uint32_t number_of_choices;
        uint8_t available_a;
        uint8_t available_b;
        uint64_t unoccupied_a;
        uint64_t unoccupied_b;
        uint8_t choice_mapping[12] = {0};
        tree_node** children;
        float* pdf;
        float* cdf;
        float* positive_regret;
        float total_positive_regret = 0.0;
        float total_mass = 0.0;

        tree_node(player_t& a,
                  player_t& b,
                  free_memory_t& free_memory) {
            available_a = count_set_bits(unoccupied_a);
            available_b = count_set_bits(unoccupied_b);
            set_move_mapping_and_choices(a, number_of_choices_a, available_a);
            set_move_mapping_and_choices(b, number_of_choices_b, available_b);
            this->number_of_choices = number_of_choices_a * number_of_choices_b;
            float uniform_density = 1. / number_of_choices;
            children = (tree_node**)allocate_memory(free_memory,
                                                    number_of_choices * sizeof(tree_node*));
            pdf = (float*)allocate_memory(free_memory, number_of_choices * sizeof(float));
            cdf = (float*)allocate_memory(free_memory, number_of_choices * sizeof(float));
            std::fill(children, children + number_of_choices, nullptr);
            std::fill(pdf, pdf + number_of_choices, uniform_density);
            std::memset(cdf, 0, number_of_choices * sizeof(float));
            std::memset(positive_regret, 0, number_of_choices * sizeof(float));
        }

        inline uint16_t index_of_maximum_regret() {
            uint16_t index_of_max = 0;
            float max_regret = 0.;
            for (uint16_t i = 0; i < number_of_choices; i++) {
                float new_regret = positive_regret[i];
                if (new_regret > max_regret) {
                    max_regret = new_regret;
                    index_of_max = i;
                }
            }
            return index_of_max;
        }

        inline uint8_t calculate_selected_position(uint16_t normalized_choice,
                                                   uint64_t unoccupied) {
            return 64 - select_ith_bit(unoccupied, normalized_choice);
        }

        inline uint16_t decode_move(uint16_t player_choice, 
                                    uint8_t available,
                                    uint64_t unoccupied,
                                    uint16_t number_of_player_choices) {

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

        inline uint16_t decode_a_move(uint16_t player_choice) {
            return decode_move(player_choice, available_a, unoccupied_a, number_of_choices_a);
        }
        
        inline uint16_t decode_b_move(uint16_t player_choice) {
            return decode_move(player_choice, available_b, unoccupied_b, number_of_choices_b);
        }

        inline void set_move_mapping_and_choices(player_t& player, 
                                                 uint16_t& number_of_choices,
                                                 uint8_t available) {
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
        }

    };

    typedef struct tree_node tree_node_t;

    inline void update_regret(tree_node_t& tree_node, float reward, uint16_t selection) {
        tree_node.total_positive_regret -= 
            std::max(0., (double)(tree_node.total_positive_regret - (768 * reward)));
        float regret_update_value = reward / tree_node.pdf[selection];
        tree_node.total_positive_regret += regret_update_value;
        tree_node.positive_regret[selection] += regret_update_value;
    }

    inline void construct_cdf(tree_node_t& tree_node) {
        tree_node.cdf[0] = tree_node.pdf[0];
        for (uint32_t i = 1; i < tree_node.number_of_choices; i++) {
            tree_node.cdf[i] = tree_node.pdf[i] + tree_node.cdf[i - 1];
        }
        tree_node.total_mass = tree_node.cdf[tree_node.number_of_choices - 1];
    }

    inline uint32_t sample(std::mt19937& mt, tree_node_t& tree_node) {
        uint32_t selection = mt() % (uint32_t) tree_node.total_mass;
        uint16_t bottom = 0, top = tree_node.number_of_choices - 1, 
            mid = tree_node.number_of_choices / 2;
        bool less_condition = selection < tree_node.cdf[mid];
        bool greater_condition = (mid < tree_node.number_of_choices - 1) && (selection >
            tree_node.cdf[mid + 1]);
        while (greater_condition || less_condition) {
            if (greater_condition) {
                bottom = mid;
                mid = (top + bottom) / 2;
            } else if (less_condition) {
                top = mid;
                mid = (top + bottom) / 2;
            } else {
                return mid + 1;
            }
            greater_condition = selection > tree_node.cdf[mid];
            less_condition = (mid < tree_node.number_of_choices - 1) &&
                tree_node.cdf[mid + 1];
        }
        return mid + 1;
    }

    inline void update_node(tree_node_t& tree_node, float reward, uint16_t selection) {
        if (reward != 0) {
            update_regret(tree_node, reward, selection);
            if (tree_node.total_positive_regret == 0) {
                float value = 1. / tree_node.number_of_choices;
                std::fill(tree_node.pdf, tree_node.pdf + tree_node.number_of_choices, value);
            } else {
                float* pdf = tree_node.pdf;
                float gamma_over_k = gamma / tree_node.number_of_choices;
                for (uint32_t i = 0; i < tree_node.number_of_choices; i++) {
                    pdf[i] = (1 - gamma)
                        * (tree_node.positive_regret[i] 
                           / tree_node.total_positive_regret) + gamma_over_k;
                }
            }
        }
    }

    inline uint32_t select_index(std::mt19937& mt, tree_node_t& tree_node) {
        construct_cdf(tree_node);
        return sample(mt, tree_node);
    }

    inline tree_node_t* allocate_node(free_memory_t& free_memory, 
                                      board_t& board) {
        tree_node_t* place = static_cast<tree_node*>(allocate_memory(free_memory,
                                                                     sizeof(tree_node_t)));
        return new (place) tree_node_t(board.a, board.b, free_memory);
    }
 
    inline float calculate_reward(player_t& a) {
        if (a.health == 0) return -1.;
        else return 1.;
    }

    tree_node_t* sm_mcts(std::mt19937& mt,
                         float& reward,
                         tree_node_t* tree_node,
                         free_memory_t& free_memory,
                         board_t& board, 
                         uint16_t current_turn) {
        if (tree_node == nullptr) {
            tree_node_t* result = allocate_node(free_memory, board);
            uint32_t index = select_index(mt, *result);
            uint16_t a_move = result->decode_a_move(index % result->number_of_choices_a);
            uint16_t b_move = result->decode_b_move(index / result->number_of_choices_a);
            simulate(mt, board.a, board.b, a_move, b_move, current_turn);
            reward = calculate_reward(board.a);
            update_node(*result, reward, index);
            return result;
        } else {
            uint32_t index = select_index(mt, *tree_node);
            uint16_t a_move = tree_node->decode_a_move(index % tree_node->number_of_choices_a);
            uint16_t b_move = tree_node->decode_b_move(index / tree_node->number_of_choices_a);
            advance_state(a_move, b_move, board.a, board.b, current_turn);
            tree_node->children[index] = sm_mcts(mt, 
                                                 reward, 
                                                 tree_node->children[index], 
                                                 free_memory,
                                                 board,
                                                 current_turn + 1);
            update_node(*tree_node, reward, index);
            return tree_node;
        }
    }

    inline uint16_t mcts_choose_move(board_t& initial_board, uint16_t current_turn) {
        std::mt19937 mt;
        std::unique_ptr<free_memory_t> memory(new free_memory());
        tree_node_t* root = allocate_node(*memory, initial_board);
        uint32_t iterations = 0;
        float reward = 0.;
        while (iterations < 1000) {
            board_t board_copy;
            copy_board(initial_board, board_copy);
            sm_mcts(mt, reward, root, *memory, board_copy, current_turn);
            iterations++;
        }
        uint16_t index_of_max_regret = root->index_of_maximum_regret();
        return root->decode_a_move(index_of_max_regret);
    }

}


#endif
