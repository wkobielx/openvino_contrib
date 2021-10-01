// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "concat_transformation.hpp"

#include <exec_graph_info.hpp>
#include <ngraph/op/concat.hpp>
#include <ngraph/pattern/op/wrap_type.hpp>
#include <ngraph/rt_info.hpp>
#include <ngraph/variant.hpp>

#include "nodes/concat_optimized.hpp"

namespace ngraph::pass {

NGRAPH_RTTI_DEFINITION(ngraph::pass::ConcatTransformation,
                       "ConcatTransformation", 0);

bool change_concat_to_concat_optimized(ngraph::pattern::Matcher &m) {
    using CUDAPlugin::nodes::ConcatOptimized;

    auto concat = std::dynamic_pointer_cast<ngraph::op::v0::Concat>(m.get_match_root());
    for (auto& in : concat->inputs()) {
        if (dynamic_cast<ngraph::op::Constant*>(in.get_node())) {
            return false;
        }
    }

    const auto& outputShape = concat->get_output_shape(0);
    const int64_t axis = concat->get_axis();
    if (axis < 0 || axis >= outputShape.size()) {
        return false;
    }
    auto num_chunks = std::accumulate(outputShape.begin(), outputShape.begin()+axis+1, 1, std::multiplies<size_t>());
    const size_t sizeAboveAxis = num_chunks / outputShape[axis];
    if (sizeAboveAxis != 1) {
        return false;
    }

    const auto &ins = concat->inputs();
    ngraph::OutputVector inOuts;
    std::transform(ins.begin(), ins.end(), std::back_inserter(inOuts),
                   [](const auto& i) {
                     return i.get_source_output();
                   });

    auto concat_optimized = std::make_shared<ConcatOptimized>(inOuts, concat->get_axis());
    concat_optimized->set_friendly_name(concat->get_friendly_name());
    ngraph::copy_runtime_info(concat, concat_optimized);

    auto& rt_info = concat->get_rt_info();
    if (auto found = rt_info.find(ExecGraphInfoSerialization::ORIGINAL_NAMES);
        found != rt_info.end()) {
        auto& rt_info_layer_names = found->second;
        const auto original_names =
            std::dynamic_pointer_cast<ngraph::VariantImpl<std::string>>(
                rt_info_layer_names);
        const std::string original_names_with_activation =
            concat->get_friendly_name() + "," + original_names->get();
        rt_info_layer_names = std::make_shared<ngraph::VariantWrapper<std::string>>(
            original_names_with_activation);
    }

    ngraph::replace_node(concat, concat_optimized);

    return true;
}

ConcatTransformation::ConcatTransformation() {
    auto concat = ngraph::pattern::wrap_type<ngraph::op::v0::Concat>();

    matcher_pass_callback callback = [](ngraph::pattern::Matcher &m) {
      return change_concat_to_concat_optimized(m);
    };

    auto m = std::make_shared<ngraph::pattern::Matcher>(
        concat, "ConcatTransformation");
    register_matcher(m, callback);
}

}