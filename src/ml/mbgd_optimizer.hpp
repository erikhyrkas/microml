//
// Created by ehyrk on 11/24/2022.
//

#ifndef MICROML_MBGD_OPTIMIZER_HPP
#define MICROML_MBGD_OPTIMIZER_HPP

#include "neural_network.hpp"
#include "optimizer.hpp"
#include "../util/tensor_utils.hpp"

using namespace std;


// With gradient descent, a single batch is called Stocastich Gradient Descent.
// A batch with all records is called Batch Gradient Descent. And a batch anywhere
// in between is called Mini-Batch Gradient Descent. Mini-Batch is fastest, handles
// large datasets, and most commonly used of this optimization approach.
//
// stochastic gradient descent (SGD) is a trivial form of gradient decent that works well at finding generalized results.
// It isn't as popular as Adam, when it comes to optimizers, since it is slow at finding an optimal answer, but
// I've read that it is better at "generalization", which is finding a solution that works for many inputs.
//
// I'm only including it in microml as a starting point to prove everything works, and since it is so simple compared to
// Adam, it lets me test the rest of the code with less fear that I've made a mistake in the optimizer itself.
//
// If you wanted to visualize a tensor, you might think of it as a force pushing in a direction.
// A gradient is a type of tensor (or slope) related to the error in the model pointing toward the fastest improvement.
// Weights are values we use to show how important or unimportant an input is. A neural network has many steps, many of which
// have weights that we need to optimize.
// When we say "optimize", we mean: find the best weights to allow us to make predictions given new input data.
// Stochastic means random.
// So, Stochastic Gradient Descent is using training data in a random order to find the best set of weights to make predictions (inferences)
// given future input data.
namespace microml {
    struct MBGDLearningState {
        float learning_rate;
        float bias_learning_rate;
    };

// full conv2d will be similar to valid, but needs some changes.
//            // this will look crazy, but it's dealing with even kernel sizes, which
//            // are not really a great idea as far as I can tell, but I don't want the
//            // code to break.
//            size_t rows = 2*((size_t)std::round(((double)kernel_size - 1)/2.0)); // 1 = 0, 2 = 2, 3 = 2, 4 = 4, 5 = 4
//            size_t cols = rows; // I could eventually support other shapes, but it's not critical right now.
//            this->output_shape = {input_shape[0]+rows, input_shape[1]+cols, output_depth};
    // Here's an interesting, related read:
    // https://towardsdatascience.com/convolution-vs-correlation-af868b6b4fb5
    // also:
    // https://medium.com/@2017csm1006/forward-and-backpropagation-in-convolutional-neural-network-4dfa96d7b37e
    class MBGDConvolution2dFunction : public NeuralNetworkFunction {
    public:
        MBGDConvolution2dFunction(vector<size_t> input_shape, size_t filters, size_t kernel_size, uint8_t bits,
                                  const shared_ptr<MBGDLearningState> &learning_state) {
            this->input_shapes = vector<vector<size_t>>{input_shape};
            this->kernel_size = kernel_size;
            this->output_shape = {input_shape[0]-kernel_size+1, input_shape[1]-kernel_size+1, filters};
            this->bits = bits;
            this->weights = {};
            for(size_t next_weight_layer = 0; next_weight_layer<filters; next_weight_layer++) {
                this->weights.push_back(make_shared<TensorFromRandom>(kernel_size, kernel_size, input_shape[2], -0.5f, 0.5f, 42));
            }
            this->learning_state = learning_state;
            if(bits == 32) {
                mixed_precision_scale = 0.5f;
            } else if(bits == 16) {
                mixed_precision_scale = 2.f;
            } else {
                mixed_precision_scale = 3.f;
            }

        }
        shared_ptr<BaseTensor> forward(const vector<shared_ptr<BaseTensor>> &input) override {
            PROFILE_BLOCK(profileBlock);
            last_input = input[0]; // ignore other inputs, as they aren't valid. TODO: throw exception
            // filters are the number of output channels we have
            const size_t filters = output_shape[2];
            shared_ptr<BaseTensor> result = nullptr;
            for(size_t output_layer = 0; output_layer < filters; output_layer++) {
                const auto current_weights = weights[output_layer];
                const auto correlation2d = make_shared<TensorValidCrossCorrelation2dView>(last_input, current_weights);
                const shared_ptr<BaseTensor> summedCorrelation2d = make_shared<TensorToChannelView>(correlation2d, output_layer, filters);
                if(!result) {
                    result = summedCorrelation2d;
                } else {
                    // each summed correlation 2d tensor is in its own output channel
                    result = make_shared<TensorAddTensorView>(result, summedCorrelation2d);
                }
            }
            // todo: it would be faster to have some sort of CombinedTensor where rather than adding the tensors,
            //  passed a vector of tensors and we only use 1 layer from each. The tensor add object will
            //  cause us to add a 0 to each value for each layer. If you have tensors added together, you'll
            //  have an 256 additional + operations for every value you fetch -- and those operations aren't
            //  changing the outcome.

            return result;
        }

        shared_ptr<BaseTensor> backward(const shared_ptr<BaseTensor> &output_error) override {
            shared_ptr<BaseTensor> input_error = nullptr;

            // input error for each input channel is
            // the sum of the fullConvolve2d of the output errors and the weights
            // filters are the number of output channels we have
            const size_t filters = output_shape[2];

            // todo: delete temp
            auto temp_li_shape = last_input->getShape();

            for(size_t output_layer = 0; output_layer < filters; output_layer++) {
                const auto output_error_channel = make_shared<TensorChannelToTensorView>(output_error, output_layer);

                const auto next_input_error = make_shared<TensorFullConvolve2dView>(output_error_channel,  weights[output_layer]);

                // todo: delete temp
                const auto temp_nie_shape = next_input_error->getShape();
                if( temp_li_shape[0] != temp_nie_shape[0] || temp_li_shape[1] != temp_nie_shape[1] || temp_li_shape[2] != temp_nie_shape[2]) {
                    throw exception("last input shape doesn't match the shape of the error we are back propagating");
                }

                if(input_error) {
                    input_error = make_shared<TensorAddTensorView>(input_error, next_input_error);
                } else {
                    input_error = next_input_error;
                }

                const auto next_weight_error = make_shared<TensorValidCrossCorrelation2dView>(last_input, output_error_channel);
                const auto next_weight_error_at_learning_rate = make_shared<TensorMultiplyByScalarView>(next_weight_error,
                                                                                                        learning_state->learning_rate * mixed_precision_scale);
                const auto adjusted_weights = make_shared<TensorMinusTensorView>(weights[output_layer], next_weight_error_at_learning_rate);
                weights[output_layer] = materialize_tensor(adjusted_weights, bits);
            }
            auto temp_ie_shape = input_error->getShape();
            if( temp_li_shape[0] != temp_ie_shape[0] || temp_li_shape[1] != temp_ie_shape[1] || temp_li_shape[2] != temp_ie_shape[2]) {
                throw exception("last input shape doesn't match the shape of the error we are back propagating");
            }
            last_input.reset();
            return input_error;
        }
    private:
        shared_ptr<BaseTensor> last_input;
        vector<shared_ptr<BaseTensor>> weights;
        uint8_t bits;
        float mixed_precision_scale;
        vector<vector<size_t>> input_shapes;
        vector<size_t> output_shape;
        size_t kernel_size;
        shared_ptr<MBGDLearningState> learning_state;
    };

    class MBGDFullyConnectedNeurons : public NeuralNetworkFunction {
    public:
        MBGDFullyConnectedNeurons(size_t input_size, size_t output_size, uint8_t bits,
                                  const shared_ptr<MBGDLearningState> &learning_state) {
            this->input_shapes = vector<vector<size_t>>{{1, input_size, 1}};
            this->output_shape = vector<size_t>{1, output_size, 1};
            this->weights = make_shared<TensorFromRandom>(input_size, output_size, 1, -0.5f, 0.5f, 42);
            this->bits = bits;
            this->learning_state = learning_state;
            if(bits == 32) {
                mixed_precision_scale = 0.5f;
            } else if(bits == 16) {
                mixed_precision_scale = 2.f;
            } else {
                mixed_precision_scale = 3.f;
            }
        }

        vector<vector<size_t>> getInputShapes() {
            return input_shapes;
        }

        vector<size_t> getOutputShape() {
            return output_shape;
        }

        // predicting
        shared_ptr<BaseTensor> forward(const vector<shared_ptr<BaseTensor>> &input) override {
            PROFILE_BLOCK(profileBlock);
            last_input = input[0];
            return make_shared<TensorDotTensorView>(last_input, weights);
        }

        // learning
        // TODO: I think this can return a unique pointer.
        shared_ptr<BaseTensor> backward(const shared_ptr<BaseTensor> &output_error) override {
            PROFILE_BLOCK(profileBlock);

//            output_error->printMaterializationPlanLine();

            // find the error
            auto weights_transposed = make_shared<TensorTransposeView>(weights);
            // TODO: we greatly improve performance by materializing the tensor into a FullTensor here, but sometimes this will use
            //  considerably more memory than we need. Part of me thinks that all dot product tensors should be materialized,
            //  and part of me thinks that there are situations of simple dot products don't need to be.
            shared_ptr<BaseTensor> input_error = make_shared<FullTensor>(make_shared<TensorDotTensorView>(output_error, weights_transposed));
//            shared_ptr<BaseTensor> input_error = make_shared<TensorDotTensorView>(output_error, weights_transposed);

            // update weights
            auto input_transposed = make_shared<TensorTransposeView>(last_input);
            auto weights_error = make_shared<TensorDotTensorView>(input_transposed, output_error);
            auto weights_error_at_learning_rate = make_shared<TensorMultiplyByScalarView>(weights_error,
                                                                                          learning_state->learning_rate*mixed_precision_scale);
            auto adjusted_weights = make_shared<TensorMinusTensorView>(weights, weights_error_at_learning_rate);
            weights = materialize_tensor(adjusted_weights, bits);

            last_input.reset();
            return input_error;
        }



    private:
        shared_ptr<BaseTensor> weights;
        shared_ptr<BaseTensor> last_input;
        uint8_t bits;
        float mixed_precision_scale;
        vector<vector<size_t>> input_shapes;
        vector<size_t> output_shape;
        shared_ptr<MBGDLearningState> learning_state;
    };

    class MBGDBias : public NeuralNetworkFunction {
    public:
        MBGDBias(const vector<size_t> &input_shape, const vector<size_t> &output_shape, uint8_t bits,
                 const shared_ptr<MBGDLearningState> &learning_state) {
            this->input_shapes = vector<vector<size_t>>{input_shape};
            this->output_shape = output_shape;
            // In my experiments, at least for the model I was testing, we found the correct results faster by starting at 0 bias.
            // This may be a mistake.
            this->bias = make_shared<UniformTensor>(output_shape[0], output_shape[1],output_shape[2], 0.f);
            // Original code started with a random value between -0.5 and 0.5:
            //this->bias = make_shared<TensorFromRandom>(output_shape[0], output_shape[1],output_shape[2], -0.5f, 0.5f, 42);
            this->bits = bits;
            this->learning_state = learning_state;
            // With models that are not fully 32-bit, if you don't scale the loss
            // you'll have precision errors that are difficult to deal with.
            // I chose to scale the learning rate, which brings a lot of potential issues
            // but is relatively straight forward to do and fast.
            // The biggest issue is that the caller might try to use a learning rate that is too big,
            // and it will not be possible to find good results.
            // There is an nvidia paper on the topic, and they scale the values before they store the weights
            // and then scale those weights back down when they use them. The advantage of this is that
            // a learning rate of X on a 32-bit model, it will work the same if you change some portions
            // to 16-bit. With my approach, if you change any of the portions of the model's precision, you may
            // have to pick new a new learning rate to get good results.
            // I went this route because it is expensive to scale up and down tensors using a view with
            // this framework when you end up with a huge stack of views, since that scaling will constantly
            // get re-applied with every future view that sits over weights.
            // There are situations where I have hundreds of views over a tensor, and adding a single view to
            // the weights will change that to thousands because many of the views sit over multiple weight
            // tensors.
            if(bits == 32) {
                // NOTE: I am taking a small shortcut here. Even without mixed precision, it's important to
                //  reduce the rate we train bias. If bias is trained at the same rate as weights, my observation
                //  is that it can "overpower" the weights, where it causes us to wildly oscillate above and below
                //  our target without ever reaching it.
                // I made this number up. it seemed to work well for both mixed-precision models and for models
                // that are entirely 32-bit.
                mixed_precision_scale = 0.1f;
            } else if(bits == 16) {
                if( learning_state->learning_rate < 0.45) {
                    // I made this number up. it seemed to work well for mixed-precision models.
                    mixed_precision_scale = 2.f;
                } else {
                    mixed_precision_scale = 1.f;
                }
            } else {
                if( learning_state->learning_rate < 0.3) {
                    // I made this number up. it seemed to work well for mixed-precision models.
                    mixed_precision_scale = 3.0f;
                } else {
                    mixed_precision_scale = 1.0f;
                }
            }
        }

        vector<vector<size_t>> getInputShapes() {
            return input_shapes;
        }

        vector<size_t> getOutputShape() {
            return output_shape;
        }

        // predicting
        shared_ptr<BaseTensor> forward(const vector<shared_ptr<BaseTensor>> &input) override {
            PROFILE_BLOCK(profileBlock);
            last_input = input[0];
            return make_shared<TensorAddTensorView>(last_input, bias);
        }

        // learning
        shared_ptr<BaseTensor> backward(const shared_ptr<BaseTensor> &output_error) override {
            PROFILE_BLOCK(profileBlock);

            auto bias_error_at_learning_rate = std::make_shared<TensorMultiplyByScalarView>(output_error,
                                                                                            learning_state->bias_learning_rate*mixed_precision_scale);
            auto adjusted_bias = std::make_shared<TensorMinusTensorView>(bias, bias_error_at_learning_rate);
            bias = materialize_tensor(adjusted_bias, bits);

            last_input.reset();
//            auto adjusted_output = std::make_shared<TensorMinusTensorView>(output_error, adjusted_bias);
            // TODO: partial derivative of bias would always be 1, so we pass along original error. I'm fairly sure this is right.
            // but I notice that the quarter float doesn't handle big shifts in scale very well
            return output_error;
        }

    private:
        shared_ptr<BaseTensor> bias;
        shared_ptr<BaseTensor> last_input;
        uint8_t bits;
        float mixed_precision_scale;
        vector<vector<size_t>> input_shapes;
        vector<size_t> output_shape;
        shared_ptr<MBGDLearningState> learning_state;
    };

    class SGDOptimizer : public Optimizer {
    public:
        explicit SGDOptimizer(float learning_rate) {
            this->sgdLearningState = make_shared<MBGDLearningState>();
            this->sgdLearningState->learning_rate = learning_rate;
            this->sgdLearningState->bias_learning_rate = learning_rate * 0.1;
        }
        explicit SGDOptimizer(float learning_rate, float bias_learning_rate) {
            this->sgdLearningState = make_shared<MBGDLearningState>();
            this->sgdLearningState->learning_rate = learning_rate;
            this->sgdLearningState->bias_learning_rate = bias_learning_rate;
        }

        shared_ptr<NeuralNetworkFunction> createFullyConnectedNeurons(size_t input_size,
                                                                      size_t output_size,
                                                                      uint8_t bits) override {
            return make_shared<MBGDFullyConnectedNeurons>(input_size, output_size, bits, sgdLearningState);
        }

        shared_ptr<NeuralNetworkFunction> createBias(vector<size_t> input_shape,
                                                     vector<size_t> output_shape, uint8_t bits) override {
            return make_shared<MBGDBias>(input_shape, output_shape, bits, sgdLearningState);
        }
        shared_ptr<NeuralNetworkFunction> createConvolutional2d(vector<size_t> input_shape,
                                                                size_t filters, size_t kernel_size,
                                                                uint8_t bits) override {
            return make_shared<MBGDConvolution2dFunction>(input_shape, filters, kernel_size, bits, sgdLearningState);
        }

    private:
        shared_ptr<MBGDLearningState> sgdLearningState;
    };
}
#endif //MICROML_MBGD_OPTIMIZER_HPP