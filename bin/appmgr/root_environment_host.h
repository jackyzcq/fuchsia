// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_MANAGER_ROOT_ENVIRONMENT_HOST_H_
#define APPLICATION_SRC_MANAGER_ROOT_ENVIRONMENT_HOST_H_

#include <memory>

#include "lib/app/fidl/application_environment_host.fidl.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "garnet/bin/appmgr/application_environment_impl.h"
#include "garnet/bin/appmgr/root_application_loader.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace app {

class RootEnvironmentHost : public ApplicationEnvironmentHost,
                            public ServiceProvider {
 public:
  explicit RootEnvironmentHost(std::vector<std::string> application_path);
  ~RootEnvironmentHost() override;

  ApplicationEnvironmentImpl* environment() const { return environment_.get(); }

  // ApplicationEnvironmentHost implementation:

  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<ServiceProvider> environment_services) override;

  // ServiceProvider implementation:

  void ConnectToService(const fidl::String& interface_name,
                        zx::channel channel) override;

 private:
  RootApplicationLoader loader_;
  fidl::Binding<ApplicationEnvironmentHost> host_binding_;
  fidl::BindingSet<ApplicationLoader> loader_bindings_;
  fidl::BindingSet<ServiceProvider> service_provider_bindings_;

  std::vector<std::string> path_;
  std::unique_ptr<ApplicationEnvironmentImpl> environment_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RootEnvironmentHost);
};

}  // namespace app

#endif  // APPLICATION_SRC_MANAGER_ROOT_ENVIRONMENT_HOST_H_
