// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "crop_vulkan.h"
#include <algorithm>

namespace ncnn {

DEFINE_LAYER_CREATOR(Crop_vulkan)

Crop_vulkan::Crop_vulkan()
{
    support_vulkan = true;

    pipeline_crop = 0;
    pipeline_crop_pack4 = 0;
    pipeline_crop_pack1to4 = 0;
    pipeline_crop_pack4to1 = 0;
}

int Crop_vulkan::create_pipeline(const Option& opt)
{
    std::vector<vk_specialization_type> specializations;

    // pack1
    {
        pipeline_crop = new Pipeline(vkdev);
        pipeline_crop->set_optimal_local_size_xyz();
        pipeline_crop->create("crop", opt, specializations, 2, 13);
    }

    // pack4
    {
        pipeline_crop_pack4 = new Pipeline(vkdev);
        pipeline_crop_pack4->set_optimal_local_size_xyz();
        pipeline_crop_pack4->create("crop_pack4", opt, specializations, 2, 13);
    }

    // pack1to4
    {
        pipeline_crop_pack1to4 = new Pipeline(vkdev);
        pipeline_crop_pack1to4->set_optimal_local_size_xyz();
        pipeline_crop_pack1to4->create("crop_pack1to4", opt, specializations, 2, 13);
    }

    // pack4to1
    {
        pipeline_crop_pack4to1 = new Pipeline(vkdev);
        pipeline_crop_pack4to1->set_optimal_local_size_xyz();
        pipeline_crop_pack4to1->create("crop_pack4to1", opt, specializations, 2, 13);
    }

    return 0;
}

int Crop_vulkan::destroy_pipeline(const Option& /*opt*/)
{
    delete pipeline_crop;
    pipeline_crop = 0;

    delete pipeline_crop_pack4;
    pipeline_crop_pack4 = 0;

    delete pipeline_crop_pack1to4;
    pipeline_crop_pack1to4 = 0;

    delete pipeline_crop_pack4to1;
    pipeline_crop_pack4to1 = 0;

    return 0;
}

int Crop_vulkan::forward(const VkMat& bottom_blob, VkMat& top_blob, VkCompute& cmd, const Option& opt) const
{
    int w = bottom_blob.w;
    int h = bottom_blob.h;
    int channels = bottom_blob.c;
    int dims = bottom_blob.dims;
    size_t elemsize = bottom_blob.elemsize;
    int elempack = bottom_blob.elempack;

    int _woffset = woffset;
    int _hoffset = hoffset;
    int _coffset = coffset;
    int _woffset2 = woffset2;
    int _hoffset2 = hoffset2;
    int _coffset2 = coffset2;
    int _outw;
    int _outh;
    int _outc;

    bool numpy_style_slice = !starts.empty() && !ends.empty();
    if (numpy_style_slice)
    {
        _woffset = 0;
        _hoffset = 0;
        _coffset = 0;
        _outw = dims == 1 ? w * elempack : w;
        _outh = dims == 2 ? h * elempack : h;
        _outc = dims == 3 ? channels * elempack : channels;

        const int* starts_ptr = starts;
        const int* ends_ptr = ends;
        const int* axes_ptr = axes;

        int _axes[4] = {0,1,2,3};
        int num_axis = axes.w;
        if (num_axis == 0)
        {
            num_axis = dims + 1;// +1 for N-dim
        }
        else
        {
            for (int i=0; i<num_axis; i++)
            {
                int axis = axes_ptr[i];
                if (axis < 0)
                    axis = dims + 1 + axis;// +1 for N-dim
                _axes[i] = axis;
            }
        }

        for (int i=0; i<num_axis; i++)
        {
            int axis = _axes[i];
            if (axis == 0)
                continue;// skip N-dim

            int start = starts_ptr[i];
            int end = ends_ptr[i];

            if (dims == 1) // axis == 1
            {
                _woffset = start > 0 ? start : w * elempack + start;
                _outw = std::min(w * elempack, end > 0 ? end : w * elempack + end) - _woffset;
            }
            if (dims == 2)
            {
                if (axis == 1)
                {
                    _hoffset = start > 0 ? start : h * elempack + start;
                    _outh = std::min(h * elempack, end > 0 ? end : h * elempack + end) - _woffset;
                }
                if (axis == 2)
                {
                    _woffset = start > 0 ? start : w + start;
                    _outw = std::min(w, end > 0 ? end : w + end) - _woffset;
                }
            }
            if (dims == 3)
            {
                if (axis == 1)
                {
                    _coffset = start > 0 ? start : channels * elempack + start;
                    _outc = std::min(channels * elempack, end > 0 ? end : channels * elempack + end) - _coffset;
                }
                if (axis == 2)
                {
                    _hoffset = start > 0 ? start : h + start;
                    _outh = std::min(h, end > 0 ? end : h + end) - _woffset;
                }
                if (axis == 3)
                {
                    _woffset = start > 0 ? start : w + start;
                    _outw = std::min(w, end > 0 ? end : w + end) - _woffset;
                }
            }
        }
    }
    else
    {
        if (dims == 1)
        {
            if (outw == -233)
                _outw = w * elempack - _woffset - _woffset2;
            else
                _outw = std::min(outw, w * elempack - _woffset - _woffset2);
        }
        if (dims == 2)
        {
            if (_hoffset == -233)
            {
                _woffset = 0;
                _woffset2 = 0;
                _outw = w;

                _hoffset = woffset;
                _hoffset2 = woffset2;

                if (outw == -233)
                    _outh = h * elempack - _hoffset - _hoffset2;
                else
                    _outh = std::min(outw, h * elempack - _hoffset - _hoffset2);
            }
            else
            {
                if (outw == -233)
                    _outw = w - _woffset - _woffset2;
                else
                    _outw = std::min(outw, w - _woffset - _woffset2);

                if (outh == -233)
                    _outh = h * elempack - _hoffset - _hoffset2;
                else
                    _outh = std::min(outh, h * elempack - _hoffset - _hoffset2);
            }
        }
        if (dims == 3)
        {
            if (_hoffset == -233 && _coffset == -233)
            {
                _woffset = 0;
                _woffset2 = 0;
                _outw = w;
                _hoffset = 0;
                _hoffset2 = 0;
                _outh = h;

                _coffset = woffset;
                _coffset2 = woffset2;

                if (outw == -233)
                    _outc = channels * elempack - _coffset - _coffset2;
                else
                    _outc = std::min(outw, channels * elempack - _coffset - _coffset2);
            }
            else if (_hoffset == -233)
            {
                _woffset = 0;
                _woffset2 = 0;
                _outw = w;

                _hoffset = woffset;
                _hoffset2 = woffset2;

                if (outw == -233)
                    _outh = h - _hoffset - _hoffset2;
                else
                    _outh = std::min(outw, h - _hoffset - _hoffset2);

                _coffset = hoffset;
                _coffset2 = hoffset2;

                if (outh == -233)
                    _outc = channels * elempack - _coffset - _coffset2;
                else
                    _outc = std::min(outh, channels * elempack - _coffset - _coffset2);
            }
            else
            {
                if (outw == -233)
                    _outw = w - _woffset - _woffset2;
                else
                    _outw = std::min(outw, w - _woffset - _woffset2);

                if (outh == -233)
                    _outh = h - _hoffset - _hoffset2;
                else
                    _outh = std::min(outh, h - _hoffset - _hoffset2);

                if (outc == -233)
                    _outc = channels * elempack - _coffset - _coffset2;
                else
                    _outc = std::min(outc, channels * elempack - _coffset - _coffset2);
            }

        }
    }

    // TODO vec and image crop

    if (dims == 3)
    {
        int out_elempack = _outc % 4 == 0 ? 4 : 1;
        size_t out_elemsize = elemsize / elempack * out_elempack;

        if (opt.use_fp16_packed && !opt.use_fp16_storage)
        {
            if (out_elempack == 4) out_elemsize = 4*2u;
            if (out_elempack == 1) out_elemsize = 4u;
        }

        top_blob.create(_outw, _outh, _outc / out_elempack, out_elemsize, out_elempack, opt.blob_vkallocator, opt.staging_vkallocator);
        if (top_blob.empty())
            return -100;

        std::vector<VkMat> bindings(2);
        bindings[0] = bottom_blob;
        bindings[1] = top_blob;

        std::vector<vk_constant_type> constants(13);
        constants[0].i = bottom_blob.dims;
        constants[1].i = bottom_blob.w;
        constants[2].i = bottom_blob.h;
        constants[3].i = bottom_blob.c;
        constants[4].i = bottom_blob.cstep;
        constants[5].i = top_blob.dims;
        constants[6].i = top_blob.w;
        constants[7].i = top_blob.h;
        constants[8].i = top_blob.c;
        constants[9].i = top_blob.cstep;
        constants[10].i = _woffset;
        constants[11].i = _hoffset;
        constants[12].i = _coffset;

        const Pipeline* pipeline = 0;
        if (elempack == 1 && out_elempack == 1)
        {
            pipeline = pipeline_crop;
        }
        else if (elempack == 4 && out_elempack == 4)
        {
            constants[12].i = _coffset / 4;// TODO pack4to1to4

            pipeline = pipeline_crop_pack4;
        }
        else if (elempack == 1 && out_elempack == 4)
        {
            pipeline = pipeline_crop_pack1to4;
        }
        else if (elempack == 4 && out_elempack == 1)
        {
            pipeline = pipeline_crop_pack4to1;
        }

        cmd.record_pipeline(pipeline, bindings, constants, top_blob);
    }

    return 0;
}

int Crop_vulkan::forward(const std::vector<VkMat>& bottom_blobs, std::vector<VkMat>& top_blobs, VkCompute& cmd, const Option& opt) const
{
    const VkMat& bottom_blob = bottom_blobs[0];
    const VkMat& reference_blob = bottom_blobs[1];

    int h = bottom_blob.h;
    int channels = bottom_blob.c;
    int dims = bottom_blob.dims;
    size_t elemsize = bottom_blob.elemsize;
    int elempack = bottom_blob.elempack;

    int ref_elempack = reference_blob.elempack;

    int _woffset = woffset;
    int _hoffset = hoffset;
    int _coffset = coffset;
    int _outw;
    int _outh;
    int _outc;

    if (dims == 1)
    {
        if (_woffset == -233)
        {
            const int* param_data = reference_blob.mapped();

            _woffset = param_data[0];
            _outw = param_data[3];
        }
        else
        {
            if (reference_blob.dims == 1)
            {
                _outw = reference_blob.w * ref_elempack;
            }
            else if (reference_blob.dims == 2)
            {
                _outw = reference_blob.w;
            }
            else // if (reference_blob.dims == 3)
            {
                _outw = reference_blob.w;
            }
        }
    }
    if (dims == 2)
    {
        if (_woffset == -233 && _hoffset == -233)
        {
            const int* param_data = reference_blob.mapped();

            _woffset = param_data[0];
            _hoffset = param_data[1];
            _outw = param_data[3];
            _outh = param_data[4];
        }
        else
        {
            if (reference_blob.dims == 1)
            {
                _outw = reference_blob.w * ref_elempack;
                _outh = h * elempack;
            }
            else if (reference_blob.dims == 2)
            {
                _outw = reference_blob.w;
                _outh = reference_blob.h * ref_elempack;
            }
            else // if (reference_blob.dims == 3)
            {
                _outw = reference_blob.w;
                _outh = reference_blob.h;
            }
        }
    }
    if (dims == 3)
    {
        if (_woffset == -233 && _hoffset == -233 && _coffset == -233)
        {
            const int* param_data = reference_blob.mapped();

            _woffset = param_data[0];
            _hoffset = param_data[1];
            _coffset = param_data[2];
            _outw = param_data[3];
            _outh = param_data[4];
            _outc = param_data[5];
        }
        else
        {
            if (reference_blob.dims == 1)
            {
                _outw = reference_blob.w * ref_elempack;
                _outh = h;
                _outc = channels * elempack;
            }
            else if (reference_blob.dims == 2)
            {
                _outw = reference_blob.w;
                _outh = reference_blob.h * ref_elempack;
                _outc = channels * elempack;
            }
            else // if (reference_blob.dims == 3)
            {
                _outw = reference_blob.w;
                _outh = reference_blob.h;
                _outc = reference_blob.c * ref_elempack;
            }
        }
    }

    // TODO vec and image crop

    if (dims == 3)
    {
        int out_elempack = _outc % 4 == 0 ? 4 : 1;
        size_t out_elemsize = elemsize / elempack * out_elempack;

        if (opt.use_fp16_packed && !opt.use_fp16_storage)
        {
            if (out_elempack == 4) out_elemsize = 4*2u;
            if (out_elempack == 1) out_elemsize = 4u;
        }

        VkMat& top_blob = top_blobs[0];

        top_blob.create(_outw, _outh, _outc / out_elempack, out_elemsize, out_elempack, opt.blob_vkallocator, opt.staging_vkallocator);
        if (top_blob.empty())
            return -100;

        std::vector<VkMat> bindings(2);
        bindings[0] = bottom_blob;
        bindings[1] = top_blob;

        std::vector<vk_constant_type> constants(13);
        constants[0].i = bottom_blob.dims;
        constants[1].i = bottom_blob.w;
        constants[2].i = bottom_blob.h;
        constants[3].i = bottom_blob.c;
        constants[4].i = bottom_blob.cstep;
        constants[5].i = top_blob.dims;
        constants[6].i = top_blob.w;
        constants[7].i = top_blob.h;
        constants[8].i = top_blob.c;
        constants[9].i = top_blob.cstep;
        constants[10].i = _woffset;
        constants[11].i = _hoffset;
        constants[12].i = _coffset;

        const Pipeline* pipeline = 0;
        if (elempack == 1 && out_elempack == 1)
        {
            pipeline = pipeline_crop;
        }
        else if (elempack == 4 && out_elempack == 4)
        {
            constants[12].i = _coffset / 4;// TODO pack4to1to4

            pipeline = pipeline_crop_pack4;
        }
        else if (elempack == 1 && out_elempack == 4)
        {
            pipeline = pipeline_crop_pack1to4;
        }
        else if (elempack == 4 && out_elempack == 1)
        {
            pipeline = pipeline_crop_pack4to1;
        }

        cmd.record_pipeline(pipeline, bindings, constants, top_blob);
    }

    return 0;
}

} // namespace ncnn
