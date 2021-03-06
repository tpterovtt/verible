# 'formatting' contains functionality for language-agnostic formatting
licenses(["notice"])

package(
    default_visibility = [
        "//verilog/formatting:__subpackages__",
    ],
)

cc_library(
    name = "align",
    srcs = ["align.cc"],
    hdrs = ["align.h"],
    deps = [
        ":format_token",
        ":token_partition_tree",
        ":unwrapped_line",
        "//common/strings:display_utils",
        "//common/strings:position",
        "//common/strings:range",
        "//common/text:concrete_syntax_leaf",
        "//common/text:concrete_syntax_tree",
        "//common/text:token_info",
        "//common/text:tree_context_visitor",
        "//common/text:tree_utils",
        "//common/util:algorithm",
        "//common/util:container_iterator_range",
        "//common/util:logging",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "align_test",
    srcs = ["align_test.cc"],
    deps = [
        ":align",
        ":format_token",
        ":unwrapped_line_test_utils",
        "//common/text:tree_builder_test_util",
        "//common/util:spacer",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "basic_format_style",
    hdrs = ["basic_format_style.h"],
)

cc_library(
    name = "format_token",
    srcs = ["format_token.cc"],
    hdrs = ["format_token.h"],
    deps = [
        "//common/strings:range",
        "//common/text:concrete_syntax_leaf",
        "//common/text:token_info",
        "//common/util:logging",
        "//common/util:spacer",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "format_token_test",
    srcs = ["format_token_test.cc"],
    deps = [
        ":format_token",
        "//common/text:token_info",
        "//common/util:range",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "token_partition_tree",
    srcs = ["token_partition_tree.cc"],
    hdrs = [
        "token_partition_tree.h",
    ],
    deps = [
        ":basic_format_style",
        ":format_token",
        ":line_wrap_searcher",
        ":unwrapped_line",
        "//common/strings:display_utils",
        "//common/text:tree_utils",
        "//common/util:container_iterator_range",
        "//common/util:logging",
        "//common/util:spacer",
        "//common/util:top_n",
        "//common/util:vector_tree",
    ],
)

cc_test(
    name = "token_partition_tree_test",
    srcs = ["token_partition_tree_test.cc"],
    deps = [
        ":format_token",
        ":token_partition_tree",
        ":unwrapped_line",
        ":unwrapped_line_test_utils",
        "//common/util:container_iterator_range",
        "//common/util:vector_tree",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "unwrapped_line",
    srcs = ["unwrapped_line.cc"],
    hdrs = [
        "unwrapped_line.h",
    ],
    deps = [
        ":format_token",
        "//common/strings:display_utils",
        "//common/text:symbol",
        "//common/text:tree_utils",
        "//common/util:container_iterator_range",
        "//common/util:container_util",
        "//common/util:spacer",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "unwrapped_line_test_utils",
    testonly = 1,
    srcs = ["unwrapped_line_test_utils.cc"],
    hdrs = ["unwrapped_line_test_utils.h"],
    deps = [
        ":format_token",
        ":unwrapped_line",
        "//common/text:token_info",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "unwrapped_line_test",
    srcs = ["unwrapped_line_test.cc"],
    deps = [
        ":format_token",
        ":unwrapped_line",
        ":unwrapped_line_test_utils",
        "//common/text:tree_builder_test_util",
        "//common/util:container_iterator_range",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "tree_annotator",
    srcs = ["tree_annotator.cc"],
    hdrs = [
        "tree_annotator.h",
    ],
    deps = [
        ":format_token",
        "//common/text:concrete_syntax_leaf",
        "//common/text:symbol",
        "//common/text:syntax_tree_context",
        "//common/text:token_info",
        "//common/text:token_stream_view",
        "//common/text:tree_context_visitor",
    ],
)

cc_test(
    name = "tree_annotator_test",
    srcs = ["tree_annotator_test.cc"],
    deps = [
        ":format_token",
        ":tree_annotator",
        "//common/text:constants",
        "//common/text:token_info",
        "//common/text:tree_builder_test_util",
        "//common/util:iterator_range",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "tree_unwrapper",
    srcs = ["tree_unwrapper.cc"],
    hdrs = [
        "tree_unwrapper.h",
    ],
    deps = [
        ":format_token",
        ":token_partition_tree",
        ":unwrapped_line",
        "//common/text:concrete_syntax_tree",
        "//common/text:symbol",
        "//common/text:syntax_tree_context",
        "//common/text:text_structure",
        "//common/text:token_info",
        "//common/text:token_stream_view",
        "//common/text:tree_context_visitor",
        "//common/util:logging",
        "//common/util:value_saver",
        "//common/util:vector_tree",
    ],
)

cc_test(
    name = "tree_unwrapper_test",
    srcs = ["tree_unwrapper_test.cc"],
    deps = [
        ":format_token",
        ":tree_unwrapper",
        ":unwrapped_line",
        "//common/text:concrete_syntax_leaf",
        "//common/text:concrete_syntax_tree",
        "//common/text:text_structure",
        "//common/text:text_structure_test_utils",
        "//common/text:token_info",
        "//common/text:token_stream_view",
        "//common/util:container_iterator_range",
        "//common/util:range",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "state_node",
    srcs = ["state_node.cc"],
    hdrs = [
        "state_node.h",
    ],
    deps = [
        ":basic_format_style",
        ":format_token",
        ":unwrapped_line",
        "//common/strings:position",
        "//common/strings:range",
        "//common/text:token_info",
        "//common/util:container_iterator_range",
        "//common/util:iterator_adaptors",
        "//common/util:iterator_range",
        "//common/util:logging",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "line_wrap_searcher",
    srcs = ["line_wrap_searcher.cc"],
    hdrs = ["line_wrap_searcher.h"],
    deps = [
        ":basic_format_style",
        ":format_token",
        ":state_node",
        ":unwrapped_line",
        "//common/text:token_info",
        "//common/util:logging",
        "//common/util:spacer",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "state_node_test",
    srcs = ["state_node_test.cc"],
    deps = [
        ":basic_format_style",
        ":format_token",
        ":state_node",
        ":unwrapped_line",
        ":unwrapped_line_test_utils",
        "//common/util:logging",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "line_wrap_searcher_test",
    srcs = ["line_wrap_searcher_test.cc"],
    deps = [
        ":basic_format_style",
        ":format_token",
        ":line_wrap_searcher",
        ":unwrapped_line",
        ":unwrapped_line_test_utils",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)
