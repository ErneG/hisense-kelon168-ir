#include "hisense_kelon168.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

namespace esphome {
namespace hisense_kelon168 {

static const char *const TAG = "hisense_kelon168";
static const char *const FAN_MID_LOW = "Mid-Low";
static const char *const FAN_MID_HIGH = "Mid-High";
static const uint32_t FRAME_GAP_MS = 400;
static const uint32_t ECHO_GUARD_MS = 800;

HisenseKelon168Climate::HisenseKelon168Climate()
    : climate_ir::ClimateIR(
          16, 30, 1.0f, true, true,
          climate::ClimateFanModeMask{climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                                      climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
          climate::ClimateSwingModeMask{climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL,
                                        climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH},
          climate::ClimatePresetMask{climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO,
                                     climate::CLIMATE_PRESET_BOOST, climate::CLIMATE_PRESET_SLEEP}) {
  this->set_supported_custom_fan_modes({FAN_MID_LOW, FAN_MID_HIGH});
  hs_state_default(&this->dev_);
  this->dev_.power = false;
}

static hs_fan_t fan_from_state(const climate::Climate &c) {
  if (c.has_custom_fan_mode()) {
    const char *m = c.get_custom_fan_mode().c_str();
    if (strcmp(m, FAN_MID_LOW) == 0)
      return HsFanMidLow;
    if (strcmp(m, FAN_MID_HIGH) == 0)
      return HsFanMidHigh;
    return HsFanAuto;
  }
  if (!c.fan_mode.has_value())
    return HsFanAuto;
  switch (*c.fan_mode) {
    case climate::CLIMATE_FAN_LOW: return HsFanLow;
    case climate::CLIMATE_FAN_MEDIUM: return HsFanMid;
    case climate::CLIMATE_FAN_HIGH: return HsFanHigh;
    default: return HsFanAuto;
  }
}

void HisenseKelon168Climate::sync_from_climate_() {
  this->dev_.power = this->mode != climate::CLIMATE_MODE_OFF;
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL: this->dev_.mode = HsModeCool; break;
    case climate::CLIMATE_MODE_DRY: this->dev_.mode = HsModeDry; break;
    case climate::CLIMATE_MODE_FAN_ONLY: this->dev_.mode = HsModeFan; break;
    default: this->dev_.mode = HsModeHeat; break;
  }
  float t = std::isnan(this->target_temperature) ? 22.0f : this->target_temperature;
  this->dev_.temp = (uint8_t) clamp(lroundf(t), 16L, 30L);
  this->dev_.fan = fan_from_state(*this);
  auto p = this->preset.value_or(climate::CLIMATE_PRESET_NONE);
  this->dev_.sleep = p == climate::CLIMATE_PRESET_SLEEP;
  this->dev_.turbo = p == climate::CLIMATE_PRESET_BOOST;
  this->dev_.economy = p == climate::CLIMATE_PRESET_ECO;
  this->swing_v_ = this->swing_mode == climate::CLIMATE_SWING_VERTICAL || this->swing_mode == climate::CLIMATE_SWING_BOTH;
  this->swing_h_ =
      this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL || this->swing_mode == climate::CLIMATE_SWING_BOTH;
}

void HisenseKelon168Climate::setup() {
  climate_ir::ClimateIR::setup();
  if (!this->fan_mode.has_value() && !this->has_custom_fan_mode())
    this->fan_mode = climate::CLIMATE_FAN_AUTO;
  if (!this->preset.has_value())
    this->preset = climate::CLIMATE_PRESET_NONE;
  this->sync_from_climate_();
}

void HisenseKelon168Climate::dump_config() {
  climate_ir::ClimateIR::dump_config();
  ESP_LOGCONFIG(TAG, "  Clock source: %s", this->time_ != nullptr ? "time_id" : "00:00 fixed");
}

void HisenseKelon168Climate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value())
    this->target_temperature = *call.get_target_temperature();
  if (call.get_fan_mode().has_value())
    this->set_fan_mode_(*call.get_fan_mode());
  else if (call.has_custom_fan_mode())
    this->set_custom_fan_mode_(call.get_custom_fan_mode());
  if (call.get_swing_mode().has_value())
    this->swing_mode = *call.get_swing_mode();
  if (call.get_preset().has_value())
    this->set_preset_(*call.get_preset());
  this->transmit_state();
}

void HisenseKelon168Climate::queue_frame_(const hs_state_t &s, uint8_t cmd) {
  hs_state_t st = s;
  st.clock_h = 0;
  st.clock_m = 0;
  if (this->time_ != nullptr) {
    auto now = this->time_->now();
    if (now.is_valid()) {
      st.clock_h = now.hour;
      st.clock_m = now.minute;
    }
  }
  Frame f;
  if (!hs_build_frame(&st, cmd, f.data())) {
    ESP_LOGW(TAG, "Invalid state, frame for cmd 0x%02X not built", cmd);
    return;
  }
  this->queue_.push_back(f);
  if (!this->sending_) {
    this->sending_ = true;
    this->send_next_();
  }
}

void HisenseKelon168Climate::send_next_() {
  if (this->queue_.empty()) {
    this->sending_ = false;
    return;
  }
  Frame f = this->queue_.front();
  this->queue_.pop_front();
  this->transmit_frame_(f);
  this->set_timeout("hs_queue", FRAME_GAP_MS, [this]() { this->send_next_(); });
}

void HisenseKelon168Climate::transmit_frame_(const Frame &f) {
  uint32_t timings[HS_TIMINGS_COUNT];
  size_t n = hs_frame_to_timings(f.data(), timings, HS_TIMINGS_COUNT);
  if (n == 0)
    return;
  ESP_LOGD(TAG, "TX cmd 0x%02X: %s", f[15], format_hex_pretty(f.data(), f.size()).c_str());
  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();
  data->set_carrier_frequency(HS_CARRIER_HZ);
  data->reserve(n);
  for (size_t i = 0; i < n; i++) {
    if (i % 2 == 0)
      data->mark(timings[i]);
    else
      data->space(timings[i]);
  }
  transmit.perform();
  this->last_tx_ms_ = millis();
  this->ever_tx_ = true;
}

static void apply_low_fan(hs_state_t &w, bool &stash_valid, hs_fan_t &stash, bool turned_on) {
  if (turned_on) {
    if (!stash_valid) {
      stash = w.fan;
      stash_valid = true;
    }
    w.fan = HsFanLow;
  } else if (!w.sleep && !w.economy && stash_valid) {
    w.fan = stash;
    stash_valid = false;
  }
}

void HisenseKelon168Climate::transmit_state() {
  hs_state_t w = this->dev_;
  bool want_power = this->mode != climate::CLIMATE_MODE_OFF;

  if (want_power != w.power) {
    w.power = want_power;
    this->queue_frame_(w, HsCmdPower);
  }

  if (want_power) {
    bool mode_ok = true;
    hs_mode_t m = w.mode;
    switch (this->mode) {
      case climate::CLIMATE_MODE_HEAT: m = HsModeHeat; break;
      case climate::CLIMATE_MODE_COOL: m = HsModeCool; break;
      case climate::CLIMATE_MODE_DRY: m = HsModeDry; break;
      case climate::CLIMATE_MODE_FAN_ONLY: m = HsModeFan; break;
      default: mode_ok = false; break;
    }
    if (mode_ok && m != w.mode) {
      w.mode = m;
      this->queue_frame_(w, HsCmdMode);
    }

    float t = std::isnan(this->target_temperature) ? 22.0f : this->target_temperature;
    uint8_t want_temp = (uint8_t) clamp(lroundf(t), 16L, 30L);
    if (want_temp != w.temp) {
      w.temp = want_temp;
      this->temp_stash_valid_ = false;
      this->queue_frame_(w, HsCmdTemp);
    }

    hs_fan_t want_fan = fan_from_state(*this);
    if (want_fan != w.fan) {
      w.fan = want_fan;
      this->fan_stash_valid_ = false;
      this->queue_frame_(w, HsCmdFan);
    }

    auto p = this->preset.value_or(climate::CLIMATE_PRESET_NONE);
    bool want_sleep = p == climate::CLIMATE_PRESET_SLEEP;
    bool want_turbo = p == climate::CLIMATE_PRESET_BOOST;
    bool want_eco = p == climate::CLIMATE_PRESET_ECO;

    if (w.turbo && !want_turbo) {
      w.turbo = false;
      if (this->temp_stash_valid_) {
        w.temp = this->temp_stash_;
        this->temp_stash_valid_ = false;
      }
      this->queue_frame_(w, HsCmdTurbo);
    }
    if (w.economy && !want_eco) {
      w.economy = false;
      apply_low_fan(w, this->fan_stash_valid_, this->fan_stash_, false);
      this->queue_frame_(w, HsCmdEconomy);
    }
    if (w.sleep && !want_sleep) {
      w.sleep = false;
      apply_low_fan(w, this->fan_stash_valid_, this->fan_stash_, false);
      this->queue_frame_(w, HsCmdSleep);
    }
    if (want_turbo && !w.turbo) {
      w.turbo = true;
      if (w.mode == HsModeHeat) {
        if (!this->temp_stash_valid_) {
          this->temp_stash_ = w.temp;
          this->temp_stash_valid_ = true;
        }
        w.temp = 30;
      }
      this->queue_frame_(w, HsCmdTurbo);
    }
    if (want_eco && !w.economy) {
      w.economy = true;
      apply_low_fan(w, this->fan_stash_valid_, this->fan_stash_, true);
      this->queue_frame_(w, HsCmdEconomy);
    }
    if (want_sleep && !w.sleep) {
      w.sleep = true;
      apply_low_fan(w, this->fan_stash_valid_, this->fan_stash_, true);
      this->queue_frame_(w, HsCmdSleep);
    }

    bool want_v = this->swing_mode == climate::CLIMATE_SWING_VERTICAL || this->swing_mode == climate::CLIMATE_SWING_BOTH;
    bool want_h =
        this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL || this->swing_mode == climate::CLIMATE_SWING_BOTH;
    if (want_v != this->swing_v_) {
      this->swing_v_ = want_v;
      this->queue_frame_(w, HsCmdVSwing);
    }
    if (want_h != this->swing_h_) {
      this->swing_h_ = want_h;
      this->queue_frame_(w, HsCmdHSwing);
    }
  }

  this->dev_ = w;
  this->publish_from_device_();
}

void HisenseKelon168Climate::publish_from_device_() {
  const hs_state_t &s = this->dev_;
  if (this->mode != climate::CLIMATE_MODE_OFF) {
    this->target_temperature = s.temp;
    switch (s.fan) {
      case HsFanLow: this->set_fan_mode_(climate::CLIMATE_FAN_LOW); break;
      case HsFanMid: this->set_fan_mode_(climate::CLIMATE_FAN_MEDIUM); break;
      case HsFanHigh: this->set_fan_mode_(climate::CLIMATE_FAN_HIGH); break;
      case HsFanMidLow: this->set_custom_fan_mode_(FAN_MID_LOW); break;
      case HsFanMidHigh: this->set_custom_fan_mode_(FAN_MID_HIGH); break;
      default: this->set_fan_mode_(climate::CLIMATE_FAN_AUTO); break;
    }
  }
  this->set_preset_(s.turbo     ? climate::CLIMATE_PRESET_BOOST
                    : s.economy ? climate::CLIMATE_PRESET_ECO
                    : s.sleep   ? climate::CLIMATE_PRESET_SLEEP
                                : climate::CLIMATE_PRESET_NONE);
  if (this->swing_v_ && this->swing_h_)
    this->swing_mode = climate::CLIMATE_SWING_BOTH;
  else if (this->swing_v_)
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  else if (this->swing_h_)
    this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
  else
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  this->publish_state();
}

void HisenseKelon168Climate::button_action(uint8_t action) {
  switch (action) {
    case ACTION_TOGGLE_V_SWING:
      this->swing_v_ = !this->swing_v_;
      this->queue_frame_(this->dev_, HsCmdVSwing);
      this->publish_from_device_();
      break;
    case ACTION_TOGGLE_H_SWING:
      this->swing_h_ = !this->swing_h_;
      this->queue_frame_(this->dev_, HsCmdHSwing);
      this->publish_from_device_();
      break;
    case ACTION_TOGGLE_DISPLAY:
      this->display_ = !this->display_;
      this->queue_frame_(this->dev_, HsCmdDisplay);
      ESP_LOGI(TAG, "Display assumed %s", this->display_ ? "on" : "off");
      break;
    case ACTION_RESEND_STATE:
      this->queue_frame_(this->dev_, HsCmdTemp);
      break;
    default:
      break;
  }
}

bool HisenseKelon168Climate::on_receive(remote_base::RemoteReceiveData data) {
  if (this->sending_ || (this->ever_tx_ && millis() - this->last_tx_ms_ < ECHO_GUARD_MS))
    return false;
  const auto &raw = data.get_raw_data();
  if (raw.size() < 300)
    return false;
  uint8_t f[HS_FRAME_BYTES];
  if (!hisense_kelon168::decode_timings(raw.data(), raw.size(), f)) {
    ESP_LOGV(TAG, "RX: not a KELON168 frame (%u timings)", (unsigned) raw.size());
    return false;
  }
  hisense_kelon168::DecodedFrame d;
  if (!hisense_kelon168::parse_frame(f, &d)) {
    ESP_LOGW(TAG, "RX: bad checksum or header: %s", format_hex_pretty(f, HS_FRAME_BYTES).c_str());
    return false;
  }
  ESP_LOGD(TAG, "RX cmd 0x%02X: %s", d.cmd, format_hex_pretty(f, HS_FRAME_BYTES).c_str());
  if (d.auto_mode)
    ESP_LOGW(TAG, "RX: auto mode is not mapped, mode kept");

  const hs_state_t old = this->dev_;
  hs_state_t n = d.state;
  if (d.auto_mode)
    n.mode = old.mode;
  n.temp = clamp<uint8_t>(n.temp, 16, 30);
  if ((n.sleep || n.economy) && !(old.sleep || old.economy)) {
    this->fan_stash_ = old.fan;
    this->fan_stash_valid_ = true;
  } else if (!n.sleep && !n.economy) {
    this->fan_stash_valid_ = false;
  }
  if (n.turbo && !old.turbo && n.mode == HsModeHeat) {
    this->temp_stash_ = old.temp;
    this->temp_stash_valid_ = true;
  } else if (!n.turbo) {
    this->temp_stash_valid_ = false;
  }
  this->dev_ = n;

  if (d.cmd == HsCmdVSwing)
    this->swing_v_ = !this->swing_v_;
  else if (d.cmd == HsCmdHSwing)
    this->swing_h_ = !this->swing_h_;
  else if (d.cmd == HsCmdDisplay)
    this->display_ = !this->display_;

  if (!n.power) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (n.mode) {
      case HsModeCool: this->mode = climate::CLIMATE_MODE_COOL; break;
      case HsModeDry: this->mode = climate::CLIMATE_MODE_DRY; break;
      case HsModeFan: this->mode = climate::CLIMATE_MODE_FAN_ONLY; break;
      default: this->mode = climate::CLIMATE_MODE_HEAT; break;
    }
  }
  this->publish_from_device_();
  return true;
}

}  // namespace hisense_kelon168
}  // namespace esphome
