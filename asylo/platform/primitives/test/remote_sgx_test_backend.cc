/*
 *
 * Copyright 2018 Asylo authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "asylo/platform/primitives/test/remote_sgx_test_backend.h"

#include <sys/wait.h>

#include <string>

#include "absl/flags/flag.h"
#include "asylo/platform/primitives/sgx/loader.pb.h"
#include "asylo/platform/primitives/test/remote_test_backend.h"
#include "asylo/platform/primitives/untrusted_primitives.h"
#include "asylo/util/path.h"
#include "asylo/util/remote/remote_loader.pb.h"

namespace asylo {
namespace primitives {
namespace test {

void RemoteSgxTestBackend::PrepareBackendLoaderParameters(
    RemoteLoadConfig *remote_config, absl::string_view enclave_binary) const {
  // Build SgxLoadConfig
  auto sgx_config = remote_config->mutable_sgx_load_config();
  sgx_config->set_debug(true);

  // Set FileEnclaveConfig parameters
  auto file_enclave_config = sgx_config->mutable_file_enclave_config();
  file_enclave_config->set_enclave_path(enclave_binary.data(),
                                        enclave_binary.size());
}

TestBackend *TestBackend::Get() {
  static TestBackend *backend = new RemoteSgxTestBackend;
  return backend;
}

}  // namespace test
}  // namespace primitives
}  // namespace asylo
