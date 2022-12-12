//
// Created by Erik Hyrkas on 11/6/2022.
//
#include <iostream>
#include "../training_data/training_dataset.hpp"
#include "../training_data/generated_datasets.hpp"

using namespace microml;
using namespace std;

void testAdditionSource() {
    TestAdditionGeneratedDataSource testAdditionGeneratedDataSource(10);
    std::shared_ptr<TrainingPair> nextRecord;
    do {
        nextRecord = testAdditionGeneratedDataSource.nextRecord();
        if (nextRecord) {
            std::cout << "GIVEN: " << std::endl;
            for (const auto &given: nextRecord->getGiven()) {
                given->print();
            }
            std::cout << "EXPECTED: " << std::endl;
            for (size_t i = 0; i < nextRecord->getExpectedSize(); i++) {
                for (const auto &expected: nextRecord->getExpected()) {
                    expected->print();
                }
            }

        }
    } while (nextRecord);

//    ASSERT_TRUE(test_one_quarter(NAN, 4));
}

int main() {
    try {
        testAdditionSource();
    } catch (const std::exception &e) {
        std::cout << e.what() << std::endl;
    }

    return 0;
}
