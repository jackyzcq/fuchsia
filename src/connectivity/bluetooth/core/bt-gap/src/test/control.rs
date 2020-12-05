// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_control::ControlMarker,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_zircon::DurationNum,
    futures::FutureExt,
};

use crate::services::start_control_service;

// Open a channel, spawn the stream, queue a message and close the remote end before running the
// loop and see if that halts within a timeout
#[fuchsia_async::run_singlethreaded(test)]
async fn close_channel_when_client_dropped() -> Result<(), Error> {
    let (client, server) = fidl::endpoints::create_proxy_and_stream::<ControlMarker>()?;
    let hd = crate::host_dispatcher::test::make_simple_test_dispatcher();
    let serve_until_done = start_control_service(hd, server);

    // Send a FIDL request
    let _response = client.is_bluetooth_available();
    fasync::Task::spawn(_response.map(|_| ())).detach();
    // Before receiving a response, drop our end of the channel so that the remote end should
    // terminate
    std::mem::drop(client);

    let timeout = 5.seconds();
    // As we have dropped the client, this should terminate successfully before the timeout
    serve_until_done.on_timeout(timeout.after_now(), move || Err(format_err!("Timed out"))).await
}
