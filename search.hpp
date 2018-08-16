#ifndef SEARCH_H
#define SEARCH_H

#include "bot.hpp"
#include <stdint.h>
#include <algorithm>
#include <random>

namespace bot {
    
    //const int number_of_choices = 768;
    const float gamma = 0.05;
    //const float uniform_density = 1. / number_of_choices;

    struct free_memory {
        uint8_t buffer[100000000];
        uint32_t free_index = 0;
    };

    typedef struct free_memory free_memory_t;

    void* allocate_memory(free_memory_t& free_memory, uint16_t bytes) {
        void* buffer_block = &(free_memory.buffer[free_memory.free_index]);
        free_memory.free_index += bytes;
        return buffer_block;
    }

    struct tree_node {
        uint16_t number_of_choices;
        tree_node** children;
        float* pdf;
        float* cdf;
        float* positive_regret;
        float total_positive_regret = 0.0;
        float total_mass = 0.0;

        tree_node(uint16_t number_of_choices, free_memory_t& free_memory) {
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
        for (int i = 1; i < tree_node.number_of_choices; i++) {
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

    // inline void update_node(tree_node_t& tree_node, float reward, uint16_t selection) {
    //     update_regret(tree_node, reward, selection);
    //     if (tree_node.total_positive_regret == 0) {
    //         float value = 1. / number_of_choices;
    //         std::fill(tree_node.pdf, tree_node.pdf + number_of_choices, value);
    //     } else {
    //         float* pdf = tree_node.pdf;
    //         float gamma_over_k = gamma / number_of_choices;
    //         for (int i = 0; i < number_of_choices; i++) {
    //             pdf[i] = (1 - gamma)
    //                 * (tree_node.positive_regret[i] 
    //                    / tree_node.total_positive_regret) + gamma_over_k;
    //         }
    //     }
    // }

    inline uint16_t select_index(std::mt19937& mt, tree_node_t& tree_node) {
        construct_cdf(tree_node);
        return sample(mt, tree_node);
    }

    // tree_node_t* allocate_node(free_nodes_t& free_nodes) {
    //     uint32_t free_index = free_nodes.free_index;
    //     tree_node_t* node = &(free_nodes.nodes[free_index]);
    //     free_nodes.free_index++;
    //     return new (node) tree_node_t();
    // }

    // tree_node* sm_mcts(std::mt19937& mt,
    //                    float& reward,
    //                    tree_node_t* tree_node,
    //                    free_nodes_t& free_nodes,
    //                    board_t& board, 
    //                    uint16_t current_turn) {
    //     if (tree_node == nullptr) {
    //         reward = simulate(mt, board.a, board.b, current_turn);
    //         return allocate_node(free_nodes);
    //     } else {
    //         uint16_t index = select_index(mt, *tree_node);
    //         return sm_mcts(mt, 
    //                        reward, 
    //                        tree_node->children[index], 
    //                        free_nodes,
    //                        board,
    //                        current_turn + 1);
    //     }
    // }

}


#endif
