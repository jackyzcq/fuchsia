// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ieee80211::{Bssid, Ssid},
    lazy_static::lazy_static,
    wlan_common::bss::Protection,
    wlan_hw_sim::*,
};

const BSS_FOO: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);
const BSS_FOO_2: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x66, 0x66]);
const BSS_BAR: Bssid = Bssid([0x62, 0x73, 0x73, 0x62, 0x61, 0x72]);
const BSS_BAR_2: Bssid = Bssid([0x63, 0x74, 0x74, 0x63, 0x62, 0x73]);
const BSS_BAZ: Bssid = Bssid([0x62, 0x73, 0x73, 0x62, 0x61, 0x7a]);
const BSS_BAZ_2: Bssid = Bssid([0x60, 0x70, 0x70, 0x60, 0x60, 0x70]);

lazy_static! {
    static ref SSID_FOO: Ssid = Ssid::from("foo");
    static ref SSID_BAR: Ssid = Ssid::from("bar");
    static ref SSID_BAZ: Ssid = Ssid::from("baz");
}

/// Test scan is working by simulating some fake APs that sends out beacon frames on specific
/// channel and verify all beacon frames are correctly reported as valid networks.
#[fuchsia_async::run_singlethreaded(test)]
async fn simulate_scan() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;

    let () = loop_until_iface_is_found().await;

    let phy = helper.proxy();
    let beacons = [
        test_utils::ScanTestBeacon {
            channel: 1,
            bssid: BSS_FOO,
            ssid: SSID_FOO.clone(),
            protection: Protection::Wpa2Personal,
            rssi: Some(-60),
        },
        test_utils::ScanTestBeacon {
            channel: 2,
            bssid: BSS_FOO_2,
            ssid: SSID_FOO.clone(),
            protection: Protection::Open,
            rssi: Some(-60),
        },
        test_utils::ScanTestBeacon {
            channel: 3,
            bssid: BSS_BAR,
            ssid: SSID_BAR.clone(),
            protection: Protection::Wpa2Personal,
            rssi: Some(-60),
        },
        test_utils::ScanTestBeacon {
            channel: 4,
            bssid: BSS_BAR_2,
            ssid: SSID_BAR.clone(),
            protection: Protection::Wpa2Personal,
            rssi: Some(-40),
        },
        test_utils::ScanTestBeacon {
            channel: 5,
            bssid: BSS_BAZ,
            ssid: SSID_BAZ.clone(),
            protection: Protection::Open,
            rssi: Some(-60),
        },
        test_utils::ScanTestBeacon {
            channel: 6,
            bssid: BSS_BAZ_2,
            ssid: SSID_BAZ.clone(),
            protection: Protection::Wpa2Personal,
            rssi: Some(-60),
        },
    ];
    let mut scan_results = test_utils::scan_for_networks(&phy, &beacons, &mut helper).await;
    scan_results.sort();

    let mut expected_aps = [
        (SSID_FOO.clone(), BSS_FOO.0, true, -60),
        (SSID_FOO.clone(), BSS_FOO_2.0, true, -60),
        (SSID_BAR.clone(), BSS_BAR.0, true, -60),
        (SSID_BAR.clone(), BSS_BAR_2.0, true, -40),
        (SSID_BAZ.clone(), BSS_BAZ.0, true, -60),
        (SSID_BAZ.clone(), BSS_BAZ_2.0, true, -60),
    ];
    expected_aps.sort();
    assert_eq!(&expected_aps, &scan_results[..]);
    helper.stop().await;
}
