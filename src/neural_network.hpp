//
// Created by ehyrk on 11/23/2022.
//

#ifndef MICROML_NEURAL_NETWORK_HPP
#define MICROML_NEURAL_NETWORK_HPP

#include <vector>
#include <set>
#include "tensor.hpp"
#include "dataset.hpp"
#include "loss.hpp"
#include "unit_test.hpp"
#include "activation.hpp"
#include "neural_network_function.hpp"
#include "optimizer.hpp"

using namespace std;

namespace microml {

    class ElapsedTimer {
    public:
        ElapsedTimer() {
            start_time = std::chrono::high_resolution_clock::now();
        }

        long long int getMicroseconds() {
            auto stop_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop_time - start_time);
            start_time = std::chrono::high_resolution_clock::now();
            return duration.count();
        }

        long long int getMilliseconds() {
            auto stop_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - start_time);
            start_time = std::chrono::high_resolution_clock::now();
            return duration.count();
        }

        long long int getSeconds() {
            auto stop_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop_time - start_time);
            start_time = std::chrono::high_resolution_clock::now();
            return duration.count();
        }

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
    };



    // A node is a vertex in a graph
    class NeuralNetworkNode : public std::enable_shared_from_this<NeuralNetworkNode> {
    public:
        explicit NeuralNetworkNode(const shared_ptr<NeuralNetworkFunction> &neuralNetworkFunction) { //}, const shared_ptr<Optimizer> &optimizer) {
            this->neuralNetworkFunction = neuralNetworkFunction;
            //this->optimizer = optimizer;
        }

        virtual void sendOutput(shared_ptr<BaseTensor> &output) {

        }

        // todo: right now, i'm assuming this is a directed acyclic graph, this may not work for everything.
        //  It is possible, I'll have to track visited nodes to avoid infinite cycles.
        void doForward(const vector<shared_ptr<BaseTensor>> &inputs) {
            auto input_to_next = neuralNetworkFunction->forward(inputs);
            if (connection_outputs.empty()) {
                // there are no nodes after this one, so we return our result.
                sendOutput(input_to_next);
                return;
            }
            for (const auto &output_connection: connection_outputs) {
                output_connection->next_input = input_to_next;
                output_connection->to->forwardFromConnection();
            }
        }

        void forwardFromInput(const shared_ptr<BaseTensor> &input) {
            return doForward({input});
        }

        void forwardFromConnection() {
            vector<shared_ptr<BaseTensor>> inputs;
            for (const auto &input: connection_inputs) {
                if (input.lock()->next_input == nullptr) {
                    return; // a different branch will populate the rest of the inputs, and we'll proceed then.
                }
                inputs.push_back(input.lock()->next_input);
            }
            doForward(inputs);
            for (const auto &input: connection_inputs) {
                input.lock()->next_input = nullptr;
            }
        }

        // todo: right now, i'm assuming this is a directed acyclic graph, this may not work for everything.
        //  It is possible, I'll have to track visited nodes to avoid infinite cycles.
        void backward(shared_ptr<BaseTensor> &output_error) {
            // TODO: for multiple errors, I'm currently averaging the errors as they propagate, but it probably should be a weighted average
            auto prior_error = neuralNetworkFunction->backward(output_error);
            for (const auto &input_connection: connection_inputs) {
                auto conn = input_connection.lock();
                auto from = conn->from.lock();
                if (from->connection_outputs.size() == 1) {
                    // most of the time there is only one from, so, ship it instead of doing extra wasted calculations
                    from->backward(prior_error);
                } else {
                    // We'll save the error we calculated, because we need to sum the errors from all outputs
                    // and not all outputs may be ready yet.
                    conn->prior_error = prior_error;
                    bool ready = true;
                    shared_ptr<BaseTensor> sum = nullptr;
                    for (const auto& output_conn: from->connection_outputs) {
                        if (output_conn->prior_error == nullptr) {
                            ready = false;
                            break;
                        }
                        if (sum == nullptr) {
                            sum = make_shared<TensorAddTensorView>(prior_error, output_conn->prior_error);
                        } else {
                            sum = make_shared<TensorAddTensorView>(sum, output_conn->prior_error);
                        }
                    }
                    if (!ready) {
                        continue;
                    }
                    shared_ptr<BaseTensor> average_error = make_shared<TensorMultiplyByScalarView>(sum, 1.0f / (float)from->connection_outputs.size());
                    from->backward(average_error);
                    for (const auto& output_conn: from->connection_outputs) {
                        output_conn->prior_error = nullptr;
                    }
                }
            }
        }

//        bool hasCycle(set<NeuralNetworkNode *> &visited) {
//            if (visited.count(this) > 0) {
//                return true;
//            }
//            visited.insert(this);
//            for (shared_ptr<NeuralNetworkNode> c: children) {
//                if (c->hasCycle(visited)) {
//                    return true;
//                }
//            }
//            visited.erase(this);
//            return false;
//        }

        // A connection is also known as an "edge" in a graph, but not everybody remembers technical terms
        struct NeuralNetworkConnection {
            shared_ptr<BaseTensor> next_input;
            shared_ptr<BaseTensor> prior_error;
            weak_ptr<NeuralNetworkNode> from;
            shared_ptr<NeuralNetworkNode> to;
        };


        shared_ptr<NeuralNetworkNode> add(const shared_ptr<NeuralNetworkNode> &child) {
            auto connection = make_shared<NeuralNetworkConnection>();
            // Avoid memory leaks created by circular strong reference chains.
            // We strongly own objects from the start of the graph toward the end,
            // rather than the end toward the start.
            connection->to = child; // strong reference to child
            connection_outputs.push_back(connection); // strong reference to connection to child
            connection->from = shared_from_this(); // weak reference to parent
            child->connection_inputs.push_back(connection); // weak reference to connection from parent
            connection->next_input = nullptr;
            return child; // this lets us chain together calls in a builder format.
        }

//        shared_ptr<NeuralNetworkNode> addFullyConnected(size_t input_size, size_t output_size, bool use_32_bit) {
//            auto networkFunction = optimizer->createFullyConnectedNeurons(input_size, output_size, use_32_bit);
//            auto networkNode = make_shared<NeuralNetworkNode>(networkFunction, optimizer);
//            return add(networkNode);
//        }
//
//        shared_ptr<NeuralNetworkNode> addActivation(const shared_ptr<ActivationFunction> &activationFunction) {
//            auto networkFunction = make_shared<NeuralNetworkActivationFunction>(activationFunction);
//            auto networkNode = make_shared<NeuralNetworkNode>(networkFunction, optimizer);
//            return add(networkNode);
//        }
//        shared_ptr<NeuralNetworkNode> addBias() {
//            auto networkFunction = make_shared<NeuralNetworkBias>(activationFunction);
//            auto networkNode = make_shared<NeuralNetworkNode>(networkFunction, optimizer);
//            return add(networkNode);
//        }

    private:
        vector<weak_ptr<NeuralNetworkConnection>> connection_inputs;
        vector<shared_ptr<NeuralNetworkConnection>> connection_outputs;
        shared_ptr<NeuralNetworkFunction> neuralNetworkFunction;
//        shared_ptr<Optimizer> optimizer;
    };

    class NeuralNetworkOutputNode : public NeuralNetworkNode {
    public:
        explicit NeuralNetworkOutputNode(const shared_ptr<NeuralNetworkFunction> &neuralNetworkFunction) : NeuralNetworkNode(neuralNetworkFunction) {
        }

        void sendOutput(shared_ptr<BaseTensor> &output) override {
            lastOutput = output;
        }

        shared_ptr<BaseTensor> consumeLastOutput() {
            auto temp = lastOutput;
            lastOutput = nullptr;
            return temp;
        }

    private:
        shared_ptr<BaseTensor> lastOutput;
    };


    // TODO: this supports training and inference,
    // but we could load a Neural network for inference (prediction) that was lower overhead.
    //
    // You don't need an optimizer for predictions if you already have weights and you aren't going
    // to change those weights. Optimizers save extra state while doing predictions that
    // we wouldn't need to save if we are never going to use it.
    class NeuralNetwork {
    public:

        float predict_scalar(const shared_ptr<BaseTensor> &given_inputs) {
            return scalar(predict(given_inputs)[0]);
        }

        vector<shared_ptr<BaseTensor>> predict(const shared_ptr<BaseTensor> &given_inputs) {
            return predict(vector<shared_ptr<BaseTensor>>{given_inputs});
        }
        // predict/infer
        // I chose the word "predict" because it is more familiar than the word "infer"
        // and the meaning is more or less the same.
        vector<shared_ptr<BaseTensor>> predict(const vector<shared_ptr<BaseTensor>> &given_inputs) {
            if (given_inputs.size() != head_nodes.size()) {
                throw exception("infer requires as many input tensors as there are input nodes");
            }
            for (size_t i = 0; i < head_nodes.size(); i++) {
                head_nodes[i]->forwardFromInput(given_inputs[i]);
            }
            vector<shared_ptr<BaseTensor>> results;
            for (const auto& output: output_nodes) {
                results.push_back(output->consumeLastOutput());
            }

            return results;
        }
        void addHead(const shared_ptr<NeuralNetworkNode> &head) {
            head_nodes.push_back(head);
        }
        void addOutput(const shared_ptr<NeuralNetworkOutputNode> &output) {
            output_nodes.push_back(output);
        }
    protected:
        vector<shared_ptr<NeuralNetworkNode>> head_nodes;
        vector<shared_ptr<NeuralNetworkOutputNode>> output_nodes;
    };

    class NeuralNetworkForTraining : public NeuralNetwork {
    public:
        NeuralNetworkForTraining(const shared_ptr<LossFunction> &lossFunction, const shared_ptr<Optimizer> &optimizer) {
            this->lossFunction = lossFunction;
            this->optimizer = optimizer;
        }

        void setLossFunction(const shared_ptr<LossFunction> &loss_function) {
            this->lossFunction = loss_function;
        }

        shared_ptr<Optimizer> getOptimizer() {
            return optimizer;
        }
        // train/fit
        void train(const shared_ptr<TrainingDataSet> &source, size_t epochs) {
            ElapsedTimer total_timer;
            cout << endl;
            for (size_t epoch = 0; epoch < epochs; epoch++) {
                ElapsedTimer timer;
                source->shuffle();
                // todo: use a batch
                auto total_records = source->record_count();
                size_t current_record = 0;
                auto next_record = source->next_record();
                while (next_record != nullptr) {
                    current_record++;
                    auto next_given = next_record->getGiven();
                    auto next_expected = next_record->getExpected();
//                    cout << "current record: " << current_record << endl;
//                    cout << "Given: " << endl;
//                    for(auto ng :next_given) {
//                        ng->print();
//                    }
//                    cout << "Expected: " << endl;
//                    for(auto ne :next_expected) {
//                        ne->print();
//                    }
                    auto next_result = predict(next_given);
                    for (size_t i = 0; i < output_nodes.size(); i++) {
                        auto next_result_tensor = next_result[i];
                        auto loss = lossFunction->computeTotalForDisplay(next_expected[i], next_result_tensor);
                        auto elapsed_time = timer.getMilliseconds();
                        printf("%zd ms Epoch: %zd/%zd Batch: %zd/%zd Loss: %f\r", elapsed_time, (epoch+1), epochs, current_record, total_records, loss);
//                        cout << '\r' << elapsed_time << " ms Epoch: " << (epoch+1) << "/" << epochs << " " << current_record << "/" << total_records << " Loss: "
//                             << loss << '\r';
                        auto loss_derivative = lossFunction->partialDerivative(next_expected[i], next_result_tensor);
                        // todo: we don't weight loss when there are multiple outputs back propagating. we should.
                        output_nodes[i]->backward(loss_derivative);
                    }
                    next_record = source->next_record();
                }
                source->restart();
            }
            cout << endl << "Finished training in " << total_timer.getSeconds() << " seconds." << endl;
        }

//        bool hasCycle() {
//            set<NeuralNetworkNode *> visited;
//            for (shared_ptr<NeuralNetworkNode> node: heads) {
//                if (node->hasCycle(visited)) {
//                    return true;
//                }
//            }
//            return false;
//        }

//        shared_ptr<NeuralNetworkNode> addFullyConnected(const shared_ptr<Optimizer> &optimizer, size_t input_size, size_t output_size, bool use_32_bit) {
//            auto networkNode = optimizer->createFullyConnectedNeurons(input_size, output_size, use_32_bit);
//            head_nodes.push_back(networkNode);
//            return networkNode;
//        }
//
//        shared_ptr<NeuralNetworkNode> addOutput(const shared_ptr<NeuralNetworkNode> &previous, const shared_ptr<ActivationFunction> &activationFunction) {
//            auto networkFunction = make_shared<NeuralNetworkActivationFunction>(activationFunction);
//            auto networkNode = make_shared<NeuralNetworkOutputNode>(networkFunction, optimizer);
//            output_nodes.push_back(output);
//            return previous->add(networkNode);
//        }
    private:
        shared_ptr<Optimizer> optimizer;
        shared_ptr<LossFunction> lossFunction;
    };

}
#endif //MICROML_NEURAL_NETWORK_HPP
