/* Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "operators/math/math_function.h"
#include <cstring>
#include <string>
#include "operators/math/gemm.h"

namespace paddle_mobile {
namespace operators {
namespace math {

template <>
void matmul<float>(const framework::Tensor &matrix_a, bool trans_a,
                   const framework::Tensor &matrix_b, bool trans_b, float alpha,
                   framework::Tensor *matrix_out, float beta, bool relu,
                   float *bias) {
  auto dim_a = matrix_a.dims();
  auto dim_b = matrix_b.dims();
  auto dim_out = matrix_out->dims();
  PADDLE_MOBILE_ENFORCE(
      dim_a.size() == 2 && dim_b.size() == 2 && dim_out.size() == 2,
      "The input and output of matmul be matrix");

  int M = dim_out[0];
  int N = dim_out[1];
  int K = (!trans_a) ? dim_a[1] : dim_a[0];
  Gemm gemm;

  if (trans_a) {
    int numel = matrix_a.numel();
    int m = matrix_a.dims()[0];
    int n = matrix_a.dims()[1];
    float *tmp = (float *)(matrix_a.data<float>());  // NOLINT
    float *a = static_cast<float *>(
        paddle_mobile::memory::Alloc(sizeof(float) * numel));
    int index = 0;
    for (int j = 0; j < n; j++) {
      for (int i = 0; i < m; i++) {
        a[index++] = tmp[i * n + j];
      }
    }

#ifdef _OPENMP

    gemm.Sgemm_omp(M, N, K, alpha, a, K, matrix_b.data<float>(), N, beta,
                   matrix_out->data<float>(), N, relu, bias);
#else
    gemm.Sgemm(M, N, K, alpha, a, K, matrix_b.data<float>(), N, beta,
               matrix_out->data<float>(), N, relu, bias);
#endif
  } else {
#ifdef _OPENMP
    gemm.Sgemm_omp(M, N, K, alpha, matrix_a.data<float>(), K,
                   matrix_b.data<float>(), N, beta, matrix_out->data<float>(),
                   N, relu, bias);
#else
    gemm.Sgemm(M, N, K, alpha, matrix_a.data<float>(), K,
               matrix_b.data<float>(), N, beta, matrix_out->data<float>(), N,
               relu, bias);
#endif
  }
}

template <>
void matmulWithBn<float>(const framework::Tensor &matrix_a, bool trans_a,
                         const framework::Tensor &matrix_b, bool trans_b,
                         float alpha, framework::Tensor *matrix_out, float beta,
                         bool relu, framework::Tensor *new_scale,
                         framework::Tensor *new_bias, int group, float *bias) {
  Gemm gemm;
  auto dim_a = matrix_a.dims();
  auto dim_b = matrix_b.dims();
  auto dim_out = matrix_out->dims();
  PADDLE_MOBILE_ENFORCE(
      dim_a.size() == 2 && dim_b.size() == 2 && dim_out.size() == 2,
      "The input and output of matmul be matrix");

  int M = dim_out[0];
  int N = dim_out[1];
  int K = (!trans_a) ? dim_a[1] : dim_a[0];

#ifdef _OPENMP
  gemm.SgemmWithBn_omp(
      M, N, K, alpha, matrix_a.data<float>(), K, matrix_b.data<float>(), N,
      beta, matrix_out->data<float>(), N, relu,
      new_scale->data<float>() + group, new_bias->data<float>() + group, bias);
#else
  gemm.SgemmWithBn(M, N, K, alpha, matrix_a.data<float>(), K,
                   matrix_b.data<float>(), N, beta, matrix_out->data<float>(),
                   N, relu, new_scale->data<float>() + group,
                   new_bias->data<float>() + group, bias);
#endif
}
void matmulWithPRelu(const framework::Tensor &matrix_a, bool trans_a,
                     const framework::Tensor &matrix_b, bool trans_b,
                     framework::Tensor *matrix_out, float *p, std::string mode,
                     float *bias, float *bias1) {
  Gemm gemm;
  auto dim_a = matrix_a.dims();
  auto dim_b = matrix_b.dims();
  auto dim_out = matrix_out->dims();
  PADDLE_MOBILE_ENFORCE(
      dim_a.size() == 2 && dim_b.size() == 2 && dim_out.size() == 2,
      "The input and output of matmul be matrix");

  int M = dim_out[0];
  int N = dim_out[1];
  int K = (!trans_a) ? dim_a[1] : dim_a[0];

#ifdef _OPENMP
  gemm.SgemmWithPRelu_omp(M, N, K, matrix_a.data<float>(), K,
                          matrix_b.data<float>(), N, matrix_out->data<float>(),
                          N, p, mode, bias, bias1);
#else
  gemm.SgemmWithPRelu(M, N, K, matrix_a.data<float>(), K,
                      matrix_b.data<float>(), N, matrix_out->data<float>(), N,
                      p, mode, bias, bias1);

#endif
}

template <typename T>
struct ClearTensor<CPU, T> {
  void operator()(framework::Tensor *tensor) {
    auto size = tensor->numel();
    auto *tensor_data = tensor->data<T>();
    memset((void *)tensor_data, 0, sizeof(T) * size);  // NOLINT
  }
};

template <typename T>
struct RowwiseAdd<CPU, T> {
  void operator()(const framework::Tensor &input,
                  const framework::Tensor &vector, framework::Tensor *output) {
    auto in_dims = input.dims();
    auto size = input.numel() / in_dims[0];
    PADDLE_MOBILE_ENFORCE((vector.numel() == size),
                          "vector.numel() must be equal to size.");
    PADDLE_MOBILE_ENFORCE((output->dims() == in_dims),
                          "output->dims() must be equal to in_dims.");

    auto *input_data = input.data<T>();
    auto *out_data = output->data<T>();
    auto *vec_data = vector.data<T>();
    for (int64_t i = 0; i < in_dims[0]; ++i) {
      for (int64_t j = 0; j < size; ++j) {
        out_data[i * size + j] = input_data[i * size + j] + vec_data[j];
      }
    }
  }
};

template struct RowwiseAdd<CPU, float>;
template struct ClearTensor<CPU, float>;

}  // namespace math
}  // namespace operators
}  // namespace paddle_mobile
