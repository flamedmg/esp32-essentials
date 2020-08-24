#include "essentials/mqtt.hpp"

#include "esp_log.h"
#include "mqtt_client.h"

namespace essentials {

const char* TAG_MQTT = "mqtt";

struct Mqtt::Private {
  std::string uri;
  std::string_view cert;
  std::string username;
  std::string password;
  std::string topicsPrefix;
  esp_mqtt_client_handle_t client;
  bool isConnected = false;

  std::unordered_multimap<std::string, Subscription*> subscribers;

  Private(
    std::string_view uri, 
    std::string_view cert, 
    std::string_view username, 
    std::string_view password, 
    std::string_view topicsPrefix
  ) : uri(uri), cert(cert), username(username), password(password), topicsPrefix(topicsPrefix) {

    esp_mqtt_client_config_t config{};
    config.uri = this->uri.c_str();
    config.cert_pem = cert.data();
    config.username = this->username.c_str();
    config.password = this->password.c_str();

    ESP_LOGI(TAG_MQTT, "Free memory: %d bytes", esp_get_free_heap_size());
    client = esp_mqtt_client_init(&config);
    esp_mqtt_client_register_event(client, esp_mqtt_event_id_t(ESP_EVENT_ANY_ID), &Private::eventHandler, this);
    esp_mqtt_client_start(client);
  }

  void publish(std::string_view topic, std::string_view data, Qos qos, bool isRetained) {
    std::string prefixedTopic = makeTopic(topic);
    esp_mqtt_client_publish(client, prefixedTopic.c_str(), data.data(), data.size(), int(qos), isRetained ? 1 : 0);
  }

  std::unique_ptr<Subscription> subscribe(std::string_view topic, Qos qos, std::function<void(std::string_view)> reaction) {
    std::string prefixedTopic = makeTopic(topic);
    if (isConnected) {
      esp_mqtt_client_subscribe(client, prefixedTopic.c_str(), int(qos));
    }
    auto subscription = std::make_unique<Subscription>();
    const auto subscriber = subscribers.emplace(prefixedTopic, subscription.get());
    subscriber->second->qos = qos;
    subscriber->second->topic = subscriber->first;
    subscriber->second->_reaction = std::move(reaction);
    subscriber->second->_unsubscribe = [prefixedTopic, this, subscriber]() {
      subscribers.erase(subscriber);
      esp_mqtt_client_unsubscribe(client, prefixedTopic.c_str());
    };

    return std::move(subscription);
  }

  std::string makeTopic(std::string_view topic) {
    if (topicsPrefix.empty()) return std::string(topic);

    std::string topicWithPrefix = topicsPrefix;
    topicWithPrefix += "/";
    topicWithPrefix += topic;

    return topicWithPrefix;
  }

  static void eventHandler(void *arg, esp_event_base_t base, int32_t eventId, void *eventData) {
    auto* p = static_cast<Private*>(arg);    
    auto event = static_cast<esp_mqtt_event_handle_t>(eventData);
    if (eventId == MQTT_EVENT_CONNECTED) {
      p->isConnected = true;
      for (const auto& [prefixedTopic, subscriber] : p->subscribers) {
        esp_mqtt_client_subscribe(p->client, prefixedTopic.c_str(), int(subscriber->qos));
      }
    }
    else if (eventId == MQTT_EVENT_DISCONNECTED) {
      p->isConnected = false;
    }
    else if (eventId == MQTT_EVENT_SUBSCRIBED) {}
    else if (eventId == MQTT_EVENT_UNSUBSCRIBED) {}
    else if (eventId == MQTT_EVENT_PUBLISHED) {}
    else if (eventId == MQTT_EVENT_DATA) {
      std::string topic(event->topic, event->topic + event->topic_len);
      const auto [begin, end] = p->subscribers.equal_range(topic);
      for (auto it = begin; it != end; it++) {
        it->second->_reaction(std::string_view(event->data, event->data_len));
      }
    }
    else if (eventId == MQTT_EVENT_ERROR) {
      ESP_LOGE(TAG_MQTT, "MQTT_EVENT_ERROR");
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
        ESP_LOGE(TAG_MQTT, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
        ESP_LOGE(TAG_MQTT, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
      } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        ESP_LOGE(TAG_MQTT, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
      } else {
        ESP_LOGE(TAG_MQTT, "Unknown error type: 0x%x", event->error_handle->error_type);
      }
    }
    else {
      ESP_LOGW(TAG_MQTT, "Unknown event, id: %d", event->event_id);
    }
  }
};

Mqtt::Mqtt(ConnectionInfo connectionInfo, std::string_view topicsPrefix) 
  : p(std::make_unique<Private>(
    connectionInfo.uri, 
    connectionInfo.cert, 
    connectionInfo.username, 
    connectionInfo.password,
    topicsPrefix)) {}
Mqtt::~Mqtt() = default;


std::unique_ptr<Mqtt::Subscription> Mqtt::subscribe(std::string_view topic, Qos qos, std::function<void(std::string_view)> reaction) {
  return p->subscribe(topic, qos, reaction);
}

void Mqtt::publish(std::string_view topic, std::string_view data, Qos qos, bool isRetained) {
  p->publish(topic, data, qos, isRetained);
}

}