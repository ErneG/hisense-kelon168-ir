#pragma once

#include <array>
#include <deque>

#include "esphome/components/button/button.h"
#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/helpers.h"

#include "hisense_decode.h"

namespace esphome {
namespace hisense_kelon168 {

enum ButtonAction : uint8_t {
  ACTION_TOGGLE_V_SWING = 0,
  ACTION_TOGGLE_H_SWING = 1,
  ACTION_TOGGLE_DISPLAY = 2,
  ACTION_RESEND_STATE = 3,
};

class HisenseKelon168Climate : public climate_ir::ClimateIR {
 public:
  HisenseKelon168Climate();

  void setup() override;
  void dump_config() override;
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  void button_action(uint8_t action);

 protected:
  using Frame = std::array<uint8_t, HS_FRAME_BYTES>;

  void control(const climate::ClimateCall &call) override;
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

  void queue_frame_(const hs_state_t &s, uint8_t cmd);
  void send_next_();
  void transmit_frame_(const Frame &f);
  void publish_from_device_();
  void sync_from_climate_();

  hs_state_t dev_{};
  bool swing_v_{false};
  bool swing_h_{false};
  bool display_{true};
  bool fan_stash_valid_{false};
  hs_fan_t fan_stash_{HsFanAuto};
  bool temp_stash_valid_{false};
  uint8_t temp_stash_{22};
  std::deque<Frame> queue_;
  bool sending_{false};
  uint32_t last_tx_ms_{0};
  bool ever_tx_{false};
  time::RealTimeClock *time_{nullptr};
};

class HisenseButton : public button::Button, public Parented<HisenseKelon168Climate> {
 public:
  void set_action(uint8_t action) { this->action_ = action; }

 protected:
  void press_action() override { this->parent_->button_action(this->action_); }
  uint8_t action_{0};
};

}  // namespace hisense_kelon168
}  // namespace esphome
