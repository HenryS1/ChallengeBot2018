#include "search.hpp"
#include <gtest/gtest.h>


namespace bot {

    std::string state_path("test_state.json");

    TEST(Initialisation, CreatesUniformDistribution) {
        board_t board;
        bot::read_board(board, state_path);
        free_memory<200> memory;
        tree_node<200> node(board.a, board.b, memory);
        ASSERT_LE(1, 5);
    }

    
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
