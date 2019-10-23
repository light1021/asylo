#
# Copyright 2019 Asylo authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""Remote backend direct dependencies for WORKSPACE to use."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

def remote_deps():
    """Macro to include Asylo remote backend dependencies in a WORKSPACE."""

    if "io_opencensus_cpp" not in native.existing_rules():
        http_archive(
            name = "io_opencensus_cpp",
            sha256 = "37318503885badf91043934a4c0579072005e37aed75bc2dfefad06474315233",
            strip_prefix = "opencensus-cpp-dec85ad71dd2ceab49b0e6b531534aa117327952",
            url = "https://github.com/census-instrumentation/opencensus-cpp/archive/dec85ad71dd2ceab49b0e6b531534aa117327952.tar.gz",
        )

    # Remote uses grpc for communications between proxy client and server.
    grpc_deps()
