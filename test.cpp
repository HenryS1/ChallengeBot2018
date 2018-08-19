#include "search.hpp"
#include <gtest/gtest.h>


namespace bot {

    std::string state_path("test_state.json");

    TEST(Initialisation, CreatesUniformDistribution) {
        board_t board;
        bot::read_board(board, state_path);
        free_memory<100000> memory;
        tree_node<100000> node(board.a, board.b, memory);
        std::cout << "size of node " << sizeof(tree_node<100000>) << std::endl;
        std::cout << "size of float " << sizeof(float) << std::endl;
        ASSERT_EQ(node.number_of_choices, 65 * 65);
    }

    
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
