#include "essentials/wifi.hpp"
#include "essentials/config.hpp"
#include "essentials/esp32_storage.hpp"
#include "essentials/settings_server.hpp"

#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C"
void app_main() {
  namespace es = essentials;

  es::Esp32Storage configStorage{"config"};
  es::Config config{configStorage};

  auto ssid = config.get<std::string>("ssid");
  auto wifiPass = config.get<std::string>("wifiPass");
  auto myValue = config.get<std::string>("myValue");
  
  es::Wifi wifi;
  es::SettingsServer settingsServer{
    80,
    "drawboard",
    "1.0.0",
    {
      {"WiFi SSID", ssid},
      {"WiFi Password", wifiPass},
      {"My Value", myValue},
    }
  };

  if (!wifi.connect(*ssid, *wifiPass)) {
    wifi.startAccessPoint("esp32", "12345678", es::Wifi::Channel::Channel5);
  }
  settingsServer.start();

  while (true) {
    std::optional<es::Ipv4Address> ip = wifi.ipv4();
    if (ip) {
      printf("IP: %s\n", ip->toString().c_str());
    } else {
      printf("Don't have IP\n");
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
