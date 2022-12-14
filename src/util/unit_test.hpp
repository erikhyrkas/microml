//
// Created by Erik Hyrkas on 10/25/2022.
// Copyright 2022. Usable under MIT license.
//

#ifndef HAPPYML_UNIT_TEST_HPP
#define HAPPYML_UNIT_TEST_HPP

#include <iostream>

#define FAIL_TEST(e) \
            std::cout << "Test failed at " \
                      << __FILE__ << ", " << __LINE__ << ", " << __func__ \
                      << std::endl; \
            throw e

#define PASS_TEST() \
            std::cout << "Test passed at " \
                          << __FILE__ << ", " << __LINE__ << ", " << __func__ \
                          << std::endl

#define ASSERT_TRUE(arg) \
            if(!(arg)) { \
                std::cout << "Test failed at " \
                          << __FILE__ << ", " << __LINE__ << ", " << __func__ << ": " \
                          << #arg \
                          << std::endl; \
               throw std::exception("Test failed."); \
            } \
            std::cout << "Test passed at " \
                          << __FILE__ << ", " << __LINE__ << ", " << __func__ << ": " \
                          << #arg \
                          << std::endl

#define ASSERT_FALSE(arg) \
            if((arg)) { \
                std::cout << "Test failed at " \
                          << __FILE__ << ", " << __LINE__ << ", " << __func__ << ": " \
                          << #arg \
                          << std::endl; \
               throw std::exception("Test failed."); \
            }             \
            std::cout << "Test passed at " \
                          << __FILE__ << ", " << __LINE__ << ", " << __func__ << ": " \
                          << #arg \
                          << std::endl


#endif //HAPPYML_UNIT_TEST_HPP
