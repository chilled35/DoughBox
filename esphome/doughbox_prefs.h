#pragma once
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"

namespace doughbox_prefs {

  static const char* TAG = "prefs";
  static ESPPreferenceObject pref_mode;
  static ESPPreferenceObject pref_low;
  static ESPPreferenceObject pref_high;
  static bool initialised = false;

  inline void init() {
    if (initialised) return;
    pref_mode  = global_preferences->make_preference<float>(1001, true);
    pref_low   = global_preferences->make_preference<float>(1002, true);
    pref_high  = global_preferences->make_preference<float>(1003, true);
    initialised = true;
  }

  inline void save_all(float mode, float low, float high) {
    init();
    pref_mode.save(&mode);
    pref_low.save(&low);
    pref_high.save(&high);
    global_preferences->sync();
    ESP_LOGI(TAG, "Saved: mode=%.0f, low=%.1f, high=%.1f", mode, low, high);
  }

  inline float load_mode() {
    init();
    float val = 0.0f;
    pref_mode.load(&val);
    return val;
  }

  inline float load_temp_low() {
    init();
    float val = 3.0f;
    pref_low.load(&val);
    return val;
  }

  inline float load_temp_high() {
    init();
    float val = 5.0f;
    pref_high.load(&val);
    return val;
  }

}
