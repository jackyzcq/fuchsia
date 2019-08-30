// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    component_manager_lib::{
        elf_runner::{ElfRunner, ProcessLauncherConnector},
        framework::RealmServiceHost,
        klog,
        model::{AbsoluteMoniker, Model, ModelConfig, ModelParams},
        startup,
    },
    failure::{self, Error, ResultExt},
    fuchsia_async as fasync,
    futures::prelude::*,
    log::*,
    std::{process, sync::Arc},
};

const NUM_THREADS: usize = 2;

fn main() -> Result<(), Error> {
    klog::KernelLogger::init().expect("Failed to initialize logger");
    let args = match startup::Arguments::from_args() {
        Ok(args) => args,
        Err(err) => {
            error!("{}\n{}", err, startup::Arguments::usage());
            return Err(err);
        }
    };

    info!("Component manager is starting up...");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    let resolver_registry = startup::available_resolvers()?;
    let builtin_services = Arc::new(startup::BuiltinRootServices::new(&args)?);
    let launcher_connector = ProcessLauncherConnector::new(&args, builtin_services.clone());
    let params = ModelParams {
        builtin_services,
        root_component_url: args.root_component_url,
        root_resolver_registry: resolver_registry,
        root_default_runner: Arc::new(ElfRunner::new(launcher_connector)),
        config: ModelConfig::default(),
    };

    let model = Arc::new(Model::new(params));

    executor.run(run_root(model.clone()), NUM_THREADS);

    Ok(())
}

async fn run_root(model: Arc<Model>) {
    if let Err(error) = startup::install_hub_if_possible(&*model).await {
        error!("Failed to install hub: {:?}", error);
        process::exit(1)
    }

    let realm_service_host = RealmServiceHost::new((*model).clone());
    model.hooks.install(realm_service_host.hooks()).await;

    match model.look_up_and_bind_instance(AbsoluteMoniker::root()).await {
        Ok(()) => {
            // TODO: Exit the component manager when the root component's binding is lost
            // (when it terminates) or perhaps attempt to rebind automatically.
            // For now, the component manager just runs forever.
            future::pending::<()>().await
        }
        Err(error) => {
            error!("Failed to bind to root component: {:?}", error);
            process::exit(1)
        }
    }
}
