// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_GAP_FAKE_ADAPTER_H_
#define SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_GAP_FAKE_ADAPTER_H_

#include "adapter.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/fake_channel.h"

namespace bt::gap::testing {

// FakeAdapter is a fake implementation of Adapter that can be used in higher layer unit tests (e.g.
// FIDL tests).
class FakeAdapter final : public Adapter {
 public:
  FakeAdapter();
  ~FakeAdapter() override = default;

  // Adapter overrides:

  AdapterId identifier() const override { return AdapterId(0); };

  bool Initialize(InitializeCallback callback, fit::closure transport_closed_callback) override;

  void ShutDown() override;

  bool IsInitializing() const override { return init_state_ == InitState::kInitializing; }

  bool IsInitialized() const override { return init_state_ == InitState::kInitialized; }

  // TODO(fxbug.dev/62791): Refactor AdapterState so that FakeAdapter can set member variables (or
  // remove AdapterState altogether).
  const AdapterState& state() const override { return state_; }

  class FakeLowEnergy final : public LowEnergy {
   public:
    struct RegisteredAdvertisement {
      AdvertisingData data;
      AdvertisingData scan_rsp;
      ConnectionCallback connect_callback;
      AdvertisingInterval interval;
      bool anonymous;
      bool include_tx_power_level;
    };

    explicit FakeLowEnergy(FakeAdapter* adapter) : adapter_(adapter) {}
    ~FakeLowEnergy() override = default;

    const std::unordered_map<AdvertisementId, RegisteredAdvertisement>&
    registered_advertisements() {
      return advertisements_;
    }

    // LowEnergy overrides:

    void Connect(PeerId peer_id, ConnectionResultCallback callback,
                 LowEnergyConnectionOptions connection_options) override {}

    bool Disconnect(PeerId peer_id) override { return false; }

    void RegisterRemoteInitiatedLink(hci::ConnectionPtr link, sm::BondableMode bondable_mode,
                                     ConnectionResultCallback callback) override {}

    void Pair(PeerId peer_id, sm::SecurityLevel pairing_level, sm::BondableMode bondable_mode,
              sm::StatusCallback cb) override {}

    void SetSecurityMode(LeSecurityMode mode) override {}

    LeSecurityMode security_mode() const override { return adapter_->le_security_mode_; }

    void StartAdvertising(AdvertisingData data, AdvertisingData scan_rsp,
                          ConnectionCallback connect_callback, AdvertisingInterval interval,
                          bool anonymous, bool include_tx_power_level,
                          AdvertisingStatusCallback status_callback) override;

    void StopAdvertising(AdvertisementId advertisement_id) override {}

    void StartDiscovery(bool active, SessionCallback callback) override {}

    void EnablePrivacy(bool enabled) override {}

    void set_irk(const std::optional<UInt128>& irk) override {}

    std::optional<UInt128> irk() const override { return std::nullopt; }

    void set_request_timeout_for_testing(zx::duration value) override {}

    void set_scan_period_for_testing(zx::duration period) override {}

   private:
    FakeAdapter* adapter_;
    AdvertisementId next_advertisement_id_ = AdvertisementId(1);
    std::unordered_map<AdvertisementId, RegisteredAdvertisement> advertisements_;
  };

  LowEnergy* le() const override { return fake_le_.get(); }
  FakeLowEnergy* fake_le() const { return fake_le_.get(); }

  class FakeBrEdr final : public BrEdr {
   public:
    struct RegisteredService {
      std::vector<sdp::ServiceRecord> records;
      l2cap::ChannelParameters channel_params;
      ServiceConnectCallback connect_callback;
    };

    FakeBrEdr() = default;
    ~FakeBrEdr() override = default;

    // Called with a reference to the l2cap::FakeChannel created when a channel is connected with
    // Connect().
    using ChannelCallback = fit::function<void(fbl::RefPtr<l2cap::testing::FakeChannel>)>;
    void set_l2cap_channel_callback(ChannelCallback cb) { channel_cb_ = std::move(cb); }

    const std::map<RegistrationHandle, RegisteredService>& registered_services() const {
      return registered_services_;
    }

    // BrEdr overrides:
    [[nodiscard]] bool Connect(PeerId peer_id, ConnectResultCallback callback) override {
      return false;
    }

    bool Disconnect(PeerId peer_id, DisconnectReason reason) override { return false; }

    void OpenL2capChannel(PeerId peer_id, l2cap::PSM psm,
                          BrEdrSecurityRequirements security_requirements,
                          l2cap::ChannelParameters params, l2cap::ChannelCallback cb) override;

    PeerId GetPeerId(hci::ConnectionHandle handle) const override { return PeerId(); }

    SearchId AddServiceSearch(const UUID& uuid, std::unordered_set<sdp::AttributeId> attributes,
                              SearchCallback callback) override {
      return SearchId();
    }

    bool RemoveServiceSearch(SearchId id) override { return false; }

    void Pair(PeerId peer_id, BrEdrSecurityRequirements security,
              hci::StatusCallback callback) override {}

    void SetConnectable(bool connectable, hci::StatusCallback status_cb) override {}

    void RequestDiscovery(DiscoveryCallback callback) override {}

    void RequestDiscoverable(DiscoverableCallback callback) override {}

    RegistrationHandle RegisterService(std::vector<sdp::ServiceRecord> records,
                                       l2cap::ChannelParameters chan_params,
                                       ServiceConnectCallback conn_cb) override;

    bool UnregisterService(RegistrationHandle handle) override { return false; }

    std::optional<ScoRequestHandle> OpenScoConnection(
        PeerId peer_id, hci::SynchronousConnectionParameters parameters,
        sco::ScoConnectionManager::OpenConnectionCallback callback) override {
      return std::nullopt;
    }

    std::optional<ScoRequestHandle> AcceptScoConnection(
        PeerId peer_id, std::vector<hci::SynchronousConnectionParameters> parameters,
        sco::ScoConnectionManager::AcceptConnectionCallback callback) override {
      return std::nullopt;
    }

   private:
    // Callback used by tests to get new channel refs.
    ChannelCallback channel_cb_;
    RegistrationHandle next_registration_handle_ = 1;
    std::map<RegistrationHandle, RegisteredService> registered_services_;
  };

  BrEdr* bredr() const override { return fake_bredr_.get(); }
  FakeBrEdr* fake_bredr() const { return fake_bredr_.get(); }

  PeerCache* peer_cache() override { return &peer_cache_; }

  bool AddBondedPeer(BondingData bonding_data) override { return true; }

  void SetPairingDelegate(fxl::WeakPtr<PairingDelegate> delegate) override {}

  bool IsDiscoverable() const override { return is_discoverable_; }

  bool IsDiscovering() const override { return is_discovering_; }

  void SetLocalName(std::string name, hci::StatusCallback callback) override;

  std::string local_name() const override { return local_name_; }

  void SetDeviceClass(DeviceClass dev_class, hci::StatusCallback callback) override;

  void set_auto_connect_callback(AutoConnectCallback callback) override {}

  void AttachInspect(inspect::Node& parent, std::string name) override {}

  fxl::WeakPtr<Adapter> AsWeakPtr() override { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  enum InitState {
    kNotInitialized = 0,
    kInitializing,
    kInitialized,
  };

  InitState init_state_;
  AdapterState state_;
  PeerCache peer_cache_;
  std::unique_ptr<FakeLowEnergy> fake_le_;
  std::unique_ptr<FakeBrEdr> fake_bredr_;
  bool is_discoverable_ = true;
  bool is_discovering_ = true;
  std::string local_name_;
  DeviceClass device_class_;
  LeSecurityMode le_security_mode_;

  fxl::WeakPtrFactory<FakeAdapter> weak_ptr_factory_;
};

}  // namespace bt::gap::testing

#endif  // SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_GAP_FAKE_ADAPTER_H_
