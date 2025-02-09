// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::{self, Proxy},
    fidl::HandleBased,
    fidl_fidl_test_components as ftest, fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, MODE_TYPE_SERVICE},
    fidl_fuchsia_process as fprocess, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    io_util::{self, OPEN_RIGHT_READABLE},
    std::path::PathBuf,
};

#[fasync::run_singlethreaded(test)]
async fn collections() {
    let realm = client::connect_to_protocol::<fsys::RealmMarker>()
        .expect("could not connect to Realm service");

    // Create a couple child components.
    for name in vec!["a", "b"] {
        let mut collection_ref = fsys::CollectionRef { name: "coll".to_string() };
        let child_decl = fsys::ChildDecl {
            name: Some(name.to_string()),
            url: Some(format!(
                "fuchsia-pkg://fuchsia.com/collections_integration_test#meta/trigger_{}.cm",
                name
            )),
            startup: Some(fsys::StartupMode::Lazy),
            environment: None,
            ..fsys::ChildDecl::EMPTY
        };
        let child_args =
            fsys::CreateChildArgs { numbered_handles: None, ..fsys::CreateChildArgs::EMPTY };
        realm
            .create_child(&mut collection_ref, child_decl, child_args)
            .await
            .expect(&format!("create_child {} failed", name))
            .expect(&format!("failed to create child {}", name));
    }

    let children = list_children(&realm).await.expect("failed to list children");
    assert_eq!("coll:a,coll:b", &children);

    // Bind to children, causing them to execute.
    for name in vec!["a", "b"] {
        let mut child_ref = new_child_ref(name, "coll");
        let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        realm
            .open_exposed_dir(&mut child_ref, server_end)
            .await
            .expect(&format!("open_exposed_dir {} failed", name))
            .expect(&format!("failed to open exposed dir of child {}", name));
        let trigger = open_trigger_svc(&dir).expect("failed to open trigger service");
        let out = trigger.run().await.expect(&format!("trigger {} failed", name));
        assert_eq!(out, format!("Triggered {}", name));
    }

    // Destroy one.
    {
        let mut child_ref = new_child_ref("a", "coll");
        realm
            .destroy_child(&mut child_ref)
            .await
            .expect("destroy_child a failed")
            .expect("failed to destroy child");
    }

    // Binding to destroyed child should fail.
    {
        let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let mut child_ref = new_child_ref("a", "coll");
        let res = realm
            .open_exposed_dir(&mut child_ref, server_end)
            .await
            .expect("second open_exposed_dir a failed");
        let err = res.expect_err("expected open_exposed_dir a to fail");
        assert_eq!(err, fcomponent::Error::InstanceNotFound);
    }

    let children = list_children(&realm).await.expect("failed to list children");
    assert_eq!("coll:b", &children);

    // Recreate child (with different URL), and bind to it. Should work.
    {
        let mut collection_ref = fsys::CollectionRef { name: "coll".to_string() };
        let child_decl = fsys::ChildDecl {
            name: Some("a".to_string()),
            url: Some(
                "fuchsia-pkg://fuchsia.com/collections_integration_test#meta/trigger_realm.cm"
                    .to_string(),
            ),
            startup: Some(fsys::StartupMode::Lazy),
            environment: None,
            ..fsys::ChildDecl::EMPTY
        };
        let child_args =
            fsys::CreateChildArgs { numbered_handles: None, ..fsys::CreateChildArgs::EMPTY };
        realm
            .create_child(&mut collection_ref, child_decl, child_args)
            .await
            .expect("second create_child a failed")
            .expect("failed to create second child a");
    }
    {
        let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let mut child_ref = new_child_ref("a", "coll");
        realm
            .open_exposed_dir(&mut child_ref, server_end)
            .await
            .expect("open_exposed_dir a failed")
            .expect("failed to open exposed dir of child a");
        let trigger = open_trigger_svc(&dir).expect("failed to open trigger service");
        let out = trigger.run().await.expect("second trigger a failed");
        assert_eq!(&out, "Triggered a");
    }

    let children = list_children(&realm).await.expect("failed to list children");
    assert_eq!("coll:a,coll:b", &children);
}

#[fasync::run_singlethreaded(test)]
async fn child_args() {
    let realm = client::connect_to_protocol::<fsys::RealmMarker>()
        .expect("could not connect to Realm service");

    // Providing numbered handles to a component that is not in a single run collection should fail.
    {
        let name = "a";
        let mut collection_ref = fsys::CollectionRef { name: "not_single_run".to_string() };
        let child_decl = fsys::ChildDecl {
            name: Some(name.to_string()),
            url: Some(format!(
                "fuchsia-pkg://fuchsia.com/collections_integration_test#meta/trigger_{}.cm",
                name
            )),
            startup: Some(fsys::StartupMode::Lazy),
            environment: None,
            ..fsys::ChildDecl::EMPTY
        };
        let (_, socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("Couldn't create socket");
        let numbered_handles = vec![fprocess::HandleInfo { handle: socket.into_handle(), id: 0 }];
        let child_args = fsys::CreateChildArgs {
            numbered_handles: Some(numbered_handles),
            ..fsys::CreateChildArgs::EMPTY
        };
        let res = realm
            .create_child(&mut collection_ref, child_decl, child_args)
            .await
            .expect(&format!("create_child {} failed", name));
        let err = res.expect_err("expected create_child a to fail");
        assert_eq!(err, fcomponent::Error::Unsupported);
    }
    // Providing numbered handles to a component that is in a single run collection should succeed.
    {
        let name = "a";
        let mut collection_ref = fsys::CollectionRef { name: "single_run".to_string() };
        let child_decl = fsys::ChildDecl {
            name: Some(name.to_string()),
            url: Some(format!(
                "fuchsia-pkg://fuchsia.com/collections_integration_test#meta/trigger_{}.cm",
                name
            )),
            startup: Some(fsys::StartupMode::Lazy),
            environment: None,
            ..fsys::ChildDecl::EMPTY
        };
        let (_, socket) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("Couldn't create socket");
        let numbered_handles = vec![fprocess::HandleInfo { handle: socket.into_handle(), id: 0 }];
        let child_args = fsys::CreateChildArgs {
            numbered_handles: Some(numbered_handles),
            ..fsys::CreateChildArgs::EMPTY
        };
        realm
            .create_child(&mut collection_ref, child_decl, child_args)
            .await
            .expect(&format!("create_child {} failed", name))
            .expect(&format!("failed to create child {}", name));
    }
}

fn new_child_ref(name: &str, collection: &str) -> fsys::ChildRef {
    fsys::ChildRef { name: name.to_string(), collection: Some(collection.to_string()) }
}

async fn list_children(realm: &fsys::RealmProxy) -> Result<String, Error> {
    let (iterator_proxy, server_end) = endpoints::create_proxy().unwrap();
    let mut collection_ref = fsys::CollectionRef { name: "coll".to_string() };
    realm
        .list_children(&mut collection_ref, server_end)
        .await
        .expect("list_children failed")
        .expect("failed to list children");
    let res = iterator_proxy.next().await;
    let children = res.expect("failed to iterate over children");
    let children: Vec<_> = children
        .iter()
        .map(|c| format!("{}:{}", c.collection.as_ref().expect("no collection"), &c.name))
        .collect();
    Ok(children.join(","))
}

fn open_trigger_svc(dir: &DirectoryProxy) -> Result<ftest::TriggerProxy, Error> {
    let node_proxy = io_util::open_node(
        dir,
        &PathBuf::from("fidl.test.components.Trigger"),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .context("failed to open trigger service")?;
    Ok(ftest::TriggerProxy::new(node_proxy.into_channel().unwrap()))
}
