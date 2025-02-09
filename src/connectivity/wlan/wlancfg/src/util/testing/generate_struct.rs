// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_sme as fidl_sme, rand::Rng as _,
    std::convert::TryInto as _,
};

pub fn generate_random_channel() -> fidl_common::WlanChannel {
    let mut rng = rand::thread_rng();
    generate_channel(rng.gen::<u8>())
}

pub fn generate_channel(channel: u8) -> fidl_common::WlanChannel {
    let mut rng = rand::thread_rng();
    fidl_common::WlanChannel {
        primary: channel,
        cbw: match rng.gen_range(0, 5) {
            0 => fidl_common::ChannelBandwidth::Cbw20,
            1 => fidl_common::ChannelBandwidth::Cbw40,
            2 => fidl_common::ChannelBandwidth::Cbw40Below,
            3 => fidl_common::ChannelBandwidth::Cbw80,
            4 => fidl_common::ChannelBandwidth::Cbw160,
            5 => fidl_common::ChannelBandwidth::Cbw80P80,
            _ => panic!(),
        },
        secondary80: rng.gen::<u8>(),
    }
}

pub fn generate_random_bss_description() -> fidl_fuchsia_wlan_internal::BssDescription {
    let mut rng = rand::thread_rng();
    fidl_fuchsia_wlan_internal::BssDescription {
        bssid: (0..6).map(|_| rng.gen::<u8>()).collect::<Vec<u8>>().try_into().unwrap(),
        bss_type: fidl_fuchsia_wlan_internal::BssType::Personal,
        beacon_period: rng.gen::<u16>(),
        timestamp: rng.gen::<u64>(),
        local_time: rng.gen::<u64>(),
        capability_info: rng.gen::<u16>(),
        ies: (0..1024).map(|_| rng.gen::<u8>()).collect(),
        rssi_dbm: rng.gen::<i8>(),
        channel: generate_random_channel(),
        snr_db: rng.gen::<i8>(),
    }
}

pub fn generate_random_sme_scan_result() -> fidl_sme::ScanResult {
    let mut rng = rand::thread_rng();
    let bssid = (0..6).map(|_| rng.gen::<u8>()).collect::<Vec<u8>>();
    fidl_sme::ScanResult {
        bssid: bssid.as_slice().try_into().unwrap(),
        ssid: format!("rand ssid {}", rng.gen::<i32>()).as_bytes().to_vec(),
        rssi_dbm: rng.gen_range(-100, 20),
        channel: generate_random_channel(),
        snr_db: rng.gen_range(-20, 50),
        compatible: rng.gen::<bool>(),
        protection: match rng.gen_range(0, 6) {
            0 => fidl_sme::Protection::Open,
            1 => fidl_sme::Protection::Wep,
            2 => fidl_sme::Protection::Wpa1,
            3 => fidl_sme::Protection::Wpa1Wpa2Personal,
            4 => fidl_sme::Protection::Wpa2Personal,
            5 => fidl_sme::Protection::Wpa2Enterprise,
            6 => fidl_sme::Protection::Wpa3Enterprise,
            _ => panic!(),
        },
        bss_description: generate_random_bss_description(),
    }
}

pub fn generate_disconnect_info(is_sme_reconnecting: bool) -> fidl_sme::DisconnectInfo {
    let mut rng = rand::thread_rng();
    fidl_sme::DisconnectInfo {
        is_sme_reconnecting,
        reason_code: rng.gen::<u16>(),
        disconnect_source: match rng.gen_range(0, 2) {
            0 => fidl_sme::DisconnectSource::Ap,
            1 => fidl_sme::DisconnectSource::User,
            2 => fidl_sme::DisconnectSource::Mlme,
            _ => panic!(),
        },
    }
}
