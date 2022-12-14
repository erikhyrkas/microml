//
// Created by Erik Hyrkas on 12/9/2022.
// Copyright 2022. Usable under MIT license.
//

#ifndef HAPPYML_TENSOR_UTILS_HPP
#define HAPPYML_TENSOR_UTILS_HPP

#include "../types/half_float.hpp"
#include "../types/quarter_float.hpp"
#include "../types/tensor.hpp"
#include "../types/tensor_views.hpp"
#include "../types/materialized_tensors.hpp"
#include <iomanip>
#include <vector>
#include <utility>
#include <iterator>
#include <future>
#include <execution>
#include <sstream>

using namespace std;

namespace happyml {

    shared_ptr<PixelTensor> pixelTensor(const vector<vector<vector<float>>> &t) {
        return make_shared<PixelTensor>(t);
    }

    shared_ptr<FullTensor> columnVector(const vector<float> &t) {
        return make_shared<FullTensor>(t);
    }

    shared_ptr<BaseTensor> randomTensor(size_t rows, size_t cols, size_t channels, float min_value, float max_value) {
        return make_shared<TensorFromRandom>(rows, cols, channels, min_value, max_value, 42);
    }

    float scalar(const shared_ptr<BaseTensor> &tensor) {
        if (tensor->size() < 1) {
            return 0.f;
        }
        return tensor->getValue(0);
    }

    shared_ptr<BaseTensor> round(const shared_ptr<BaseTensor> &tensor) {
        return make_shared<TensorRoundedView>(tensor);
    }

    size_t maxIndex(const shared_ptr<BaseTensor> &tensor) {
        return tensor->maxIndex(0, 0);
    }

    int estimateBias(int estimate_min, int estimate_max, const float adj_min, const float adj_max) {
        int quarter_bias = estimate_min;
        for (int proposed_quarter_bias = estimate_max; proposed_quarter_bias >= estimate_min; proposed_quarter_bias--) {
            const float bias_max = quarterToFloat(QUARTER_MAX, proposed_quarter_bias);
            const float bias_min = -bias_max;
            if (adj_min > bias_min && adj_max < bias_max) {
                quarter_bias = proposed_quarter_bias;
                break;
            }
        }
        return quarter_bias;
    }

    shared_ptr<BaseTensor> materializeTensor(const shared_ptr<BaseTensor> &tensor, uint8_t bits) {
        if (bits == 32) {
            if (tensor->isMaterialized()) {
                // there is no advantage to materializing an already materialized tensor to 32 bits.
                // whether other bit options may reduce memory footprint.
                return tensor;
            }
            return make_shared<FullTensor>(tensor);
        } else if (bits == 16) {
            return make_shared<HalfTensor>(tensor);
        }
        auto minMax = tensor->range();
        int quarterBias = estimateBias(4, 15, minMax.first, minMax.second);
        return make_shared<QuarterTensor>(tensor, quarterBias);
    }

// channels, rows, columns
    shared_ptr<BaseTensor> materializeTensor(const shared_ptr<BaseTensor> &other) {
        if (other->isMaterialized()) {
            return other;
        }
        return make_shared<FullTensor>(other);
    }

    shared_ptr<FullTensor> tensor(const vector<vector<vector<float>>> &t) {
        return make_shared<FullTensor>(t);
    }

    shared_ptr<BaseTensor> loadTensor(const string &path, uint8_t bits) {
        if (bits == 16) {
            return make_shared<HalfTensor>(path);
        } else if (bits == 8) {
            // TODO:
            //  we don't know what bias to use, so we load up the tensor in 16-bit then size to fit.
            //  This is an imperfect solution, and temporary.
            //  Option 1: we could update the quarter tensor load logic to scan the file to
            //  figure out the min and max values, or
            //  Option 2: we could persist bias in the neural network function and then pass it here.
            //  Option 2, saving the bias, sounds tempting, but would take some finagling, since the
            //  actual bias used isn't readily available -- but it would be an efficient option.
            //  We'd probably have to track it every time it changed in back propagation, and it
            //  would be 0 for tensors that were not 8-bit. My preference is option 2, but it isn't
            //  critical to solve for alpha.
            return materializeTensor(make_shared<HalfTensor>(path), 8);
        }
        return make_shared<FullTensor>(path);
    }

    void assertEqual(const shared_ptr<BaseTensor> &t1, const shared_ptr<BaseTensor> &t2) {
        if (t1->channelCount() != t2->channelCount()) {
            throw exception("Tensors don't have the same number of channels.");
        }
        if (t1->rowCount() != t2->rowCount()) {
            throw exception("Tensors don't have the same number of rows.");
        }
        if (t1->columnCount() != t2->columnCount()) {
            throw exception("Tensors don't have the same number of columns.");
        }
        for (size_t channel = 0; channel < t1->channelCount(); channel++) {
            for (size_t row = 0; row < t1->rowCount(); row++) {
                for (size_t col = 0; col < t1->columnCount(); col++) {
                    if (!roughlyEqual(t1->getValue(row, col, channel), t2->getValue(row, col, channel))) {
                        ostringstream message;
                        message << "Value " << t1->getValue(row, col, channel) << " does not equal "
                                << t2->getValue(row, col, channel) << " at " << row << ", " << col << ", " << channel;
                        throw exception(message.str().c_str());
                    }
                }
            }
        }
    }
}

#endif //HAPPYML_TENSOR_UTILS_HPP
