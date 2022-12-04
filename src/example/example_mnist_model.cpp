//
// Created by Erik Hyrkas on 11/28/2022.
//
#include <memory>
#include "../model.hpp"
#include "../file_reader.hpp"
#include "../dataset.hpp"

using namespace std;
using namespace microml;
using namespace micromldsl;

int main() {
    try {
        map<string, size_t> categories;
        categories["0"] = 0;
        categories["1"] = 1;
        categories["2"] = 2;
        categories["3"] = 3;
        categories["4"] = 4;
        categories["5"] = 5;
        categories["6"] = 6;
        categories["7"] = 7;
        categories["8"] = 8;
        categories["9"] = 9;
        auto expectedEncoder = make_shared<TextToCategoryEncoder>(categories);
        auto givenEncoder = make_shared<TextToPixelEncoder>();
        // making the shape square (28x28) just to test the auto-flattening capabilities of the network.
        //"..\\test_data\\small_mnist_format.csv"
        //"..\\data\\mnist_test.csv"
        auto mnistDataSource = make_shared<InMemoryDelimitedValuesTrainingDataSet>("..\\data\\mnist_test.csv", ',',
                                                                                 true, false, true,
                                                                                 1, 28*28,
                                                                                 vector<size_t>{1,10,1},vector<size_t>{28, 28,1},//vector<size_t>{28,28,1},
                                                                                 expectedEncoder, givenEncoder);
        cout << "Loaded training data." << endl;

        auto neuralNetwork = neuralNetworkBuilder()
                ->addInput(mnistDataSource->getGivenShape(), 5, NodeType::full, ActivationType::tanh_approx)
                ->addNode(50, NodeType::full, ActivationType::tanh_approx)
                ->addOutput(mnistDataSource->getExpectedShape(), ActivationType::tanh_approx)
                ->build();
        neuralNetwork->train(mnistDataSource, 100, 128);


        auto testMnistDataSource = make_shared<InMemoryDelimitedValuesTrainingDataSet>("..\\test_data\\small_mnist_format.csv", ',',
                                                                                   true, false, true,
                                                                                   1, 28*28,
                                                                                   vector<size_t>{1,10,1},vector<size_t>{28, 28,1},//vector<size_t>{28,28,1},,vector<size_t>{28,28,1},
                                                                                   expectedEncoder, givenEncoder);
        cout << "Loaded test data." << endl;
        cout << fixed << setprecision(2);
        auto nextRecord = testMnistDataSource->next_record();
        while(nextRecord) {
            auto prediction = max_index(neuralNetwork->predictOne(nextRecord->getFirstGiven()));
            cout << "mnist truth: " << max_index(nextRecord->getFirstExpected()) << " microml prediction: " << prediction << endl;
            nextRecord = testMnistDataSource->next_record();
        }



    } catch (const std::exception &e) {
        std::cout << e.what() << std::endl;
    }
}