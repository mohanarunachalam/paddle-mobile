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

#include "operators/kernel/fetch_kernel.h"

namespace paddle_mobile {
namespace operators {

template <>
bool FetchKernel<FPGA, float>::Init(FetchParam<FPGA> *param) {
  auto input = const_cast<Tensor *>(param->InputX());
  auto output = param->Out();
  if (input->type() == typeid(float)) {
    return true;
  }
  output->init(typeid(float));
  output->Resize(input->dims());
  fpga::format_fp32_ofm(output);

  fpga::BypassArgs args = {fpga::DATA_TYPE_FP16};

  args.input_data_type = fpga::DATA_TYPE_FP16;
  args.output_data_type = fpga::DATA_TYPE_FP32;
  args.input_layout_type = fpga::LAYOUT_CHW;
  args.output_layout_type = fpga::LAYOUT_HWC;
  args.image.address = input->data<half>();
  args.image.channels = (uint32_t)product(input->dims());
  args.image.height = 1;
  args.image.width = 1;
  args.image.pad_height = 0;
  args.image.pad_width = 0;
  args.output.address = output->data<float>();
  args.output.scale_address = output->scale;
  param->fpga_bypass_args = args;

  return true;
}
void dealign(float *src, float *dst, int input_c, int input_h, int input_w) {
  int alignCW = paddle_mobile::fpga::align_to_x(input_c * input_w, 16);
  int dealignCW = input_c * input_w;
  for (int h = 0; h < input_h; ++h) {
    auto input_offset = h * alignCW;
    auto output_offset = h * dealignCW;
    memcpy((dst + output_offset), (src + input_offset),
           dealignCW * sizeof(float));
  }
}
template <>
void FetchKernel<FPGA, float>::Compute(const FetchParam<FPGA> &param) {
  auto input = param.InputX();
  if (input->type() == typeid(float)) {
    auto output = param.Out();
    output->ShareDataWith(*input);
    return;
  }
  fpga::PerformBypass(param.fpga_bypass_args);
  auto outC = param.Out()->dims()[1];
  auto outH = param.Out()->dims()[2];
  auto outW = param.Out()->dims()[3];
  fpga::fpga_invalidate(param.fpga_bypass_args.output.address,
                        outH *
                            (paddle_mobile::fpga::align_to_x(outC * outW, 16)) *
                            sizeof(float));

  float *outdata_ptr =
      reinterpret_cast<float *>(param.fpga_bypass_args.output.address);
  float *data_tmp =
      reinterpret_cast<float *>(malloc(outC * outH * outW * sizeof(float)));
  dealign(outdata_ptr, data_tmp, outC, outH, outW);
  memcpy(outdata_ptr, data_tmp, outC * outH * outW * sizeof(float));
}

template class FetchKernel<FPGA, float>;

}  // namespace operators
}  // namespace paddle_mobile
