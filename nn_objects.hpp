#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <iostream>

struct TensorInfo
{
    uint32_t rows;
    uint32_t cols;
    std::vector<float> data;
};

struct WorkAssignment
{
    uint32_t worker_id;

    TensorInfo batch_x;
    TensorInfo batch_y;

    std::vector<TensorInfo> weights;
    std::vector<TensorInfo> biases;
};

struct GradientResult
{
    uint32_t worker_id;

    std::vector<TensorInfo> weight_gradients;
    std::vector<TensorInfo> bias_gradients;
};