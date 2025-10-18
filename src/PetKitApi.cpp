#include "PetKitApi.h"
#include "mbedtls/md5.h"
#include <WiFi.h>
#include <algorithm> // For std::sort and std::max_element

String md5(String str) {
    byte digest[16];
    char hex_digest[33];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    mbedtls_md5_update(&ctx, (const unsigned char *)str.c_str(), str.length());
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);

    for (int i = 0; i < 16; i++) {
        sprintf(&hex_digest[i * 2], "%02x", (unsigned int)digest[i]);
    }
    hex_digest[32] = 0;
    return String(hex_digest);
}

PetKitApi::PetKitApi(const char *username, const char *password, const char *region, const char *timezone, int led)
    : _username(username),
      _password(password),
      _region(region),
      _timezone(timezone),
      _ledpin(led),
      _debug(false) // Debugging is off by default
{
    _base_url = "https://passport.petkt.com";
    if (_ledpin > 0) {
        pinMode(_ledpin, OUTPUT);
    }
}

// --- Quality of Life Methods ---

void PetKitApi::_log(const char* message) {
    if (_debug && Serial) {
        Serial.println(message);
    }
}

void PetKitApi::_log(const String& message) {
    if (_debug && Serial) {
        Serial.println(message);
    }
}

void PetKitApi::setDebug(bool enabled) {
    _debug = enabled;
}

bool PetKitApi::syncTime(const char* ntpServer, const char* tzInfo) {
    _log("Synchronizing time...");
    configTzTime(tzInfo, ntpServer);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) { // 10-second timeout
        _log("Failed to obtain time from NTP server.");
        return false;
    }
    setenv("TZ", tzInfo, 1);
    tzset();
    _log("Time synchronized successfully.");
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%A, %B %d %Y %H:%M:%S", &timeinfo);
    _log(String("Current local time: ") + timeStr);
    return true;
}

// --- Core API Methods ---

bool PetKitApi::login() {
    if (WiFi.status() != WL_CONNECTED) {
        _log("Error: WiFi not connected. Cannot log in.");
        return false;
    }
    if (time(nullptr) < 8 * 3600 * 2) {
        _log("Error: Time is not set. Please call syncTime() before login().");
        return false;
    }

    if (!_getBaseUrl()) {
        return false;
    }
    if (_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
    
    _log("Attempting to log in...");
    JsonDocument client_nfo;
    client_nfo["locale"] = "en-US";
    client_nfo["name"] = "23127PN0CG";
    client_nfo["osVersion"] = "15.1";
    client_nfo["platform"] = "android";
    client_nfo["source"] = "app.petkit-android";
    client_nfo["version"] = "12.4.1";
    client_nfo["timezoneId"] = _timezone;
    client_nfo["timezone"] = _getTimezoneOffset();
    String client_nfo_str;
    serializeJson(client_nfo, client_nfo_str);

    String payload = "oldVersion=12.4.1";
    payload += "&client=" + _urlEncode(client_nfo_str);
    payload += "&encrypt=1";
    payload += "&region=" + _region;
    payload += "&username=" + _urlEncode(_username);
    payload += "&password=" + md5(_password);

    String response = _sendRequest("/user/login", payload, true, true);

    if (response == "") {
        _log("Login request failed. No response from server.");
        return false;
    }
    if (_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));

    JsonDocument result;
    DeserializationError error = deserializeJson(result, response);

    if (error) {
        _log(String("Login JSON parsing failed: ") + error.c_str());
        _log("Response was: " + response);
        return false;
    }

    if (result["session"]) {
        _session_id = result["session"]["id"].as<String>();
        _log("Login successful!");
        return true;
    } else {
        _log("Login failed. Please check credentials and region.");
        if (_debug) {
            _log("Server response:");
            serializeJsonPretty(result, Serial);
            Serial.println();
        }
        return false;
    }
}

bool PetKitApi::fetchAllData(int days_back) {
    if (_session_id == "") {
        _log("Error: Not logged in. Please call login() first.");
        return false;
    }
    _getDevices();
    _parsePets();
    _getLitterboxData(days_back);
    return true;
}

// --- Data Accessors ---

const std::vector<Pet>& PetKitApi::getPets() const {
    return _pets;
}

const std::vector<LitterboxRecord>& PetKitApi::getLitterboxRecords() const {
    return _litterbox_records;
}

const std::vector<StatusRecord>& PetKitApi::getStatusRecords() const {
    return _status_records;
}

std::vector<LitterboxRecord> PetKitApi::getLitterboxRecordsByPetId(int pet_id) const {
    std::vector<LitterboxRecord> pet_records;
    // This efficiently iterates through all records and copies only the ones matching the pet's ID.
    for (const auto& record : _litterbox_records) {
        if (record.pet_id == pet_id) {
            pet_records.push_back(record);
        }
    }
    return pet_records;
}

StatusRecord PetKitApi::getLatestStatus() const {
    if (_status_records.empty()) {
        return StatusRecord{}; // Return an empty record if none exist
    }
    // Records are pre-sorted, so the first one is the latest
    return _status_records.front();
}

// --- Private Helper Methods ---

String PetKitApi::_getTimezoneOffset() {
    time_t now = time(nullptr);
    struct tm local_info;
    struct tm gmt_info;

    localtime_r(&now, &local_info);
    gmtime_r(&now, &gmt_info);

    time_t local_time = mktime(&local_info);
    time_t gmt_time = mktime(&gmt_info);

    long offset_sec = (long)difftime(local_time, gmt_time);
    float offset_hours = (float)offset_sec / 3600.0;
    
    return String(offset_hours, 1);
}

bool PetKitApi::_getBaseUrl() {
    _log("Getting regional server URL...");
    String response = _sendRequest("/v1/regionservers", "", false);

    if (response == "") {
        _log("Failed to get region servers list.");
        return false;
    }

    JsonDocument doc;
    deserializeJson(doc, response);

    JsonArray servers = doc["list"].as<JsonArray>();
    for (JsonObject server : servers) {
        String serverName = server["name"].as<String>();
        String serverId = server["id"].as<String>();
        String gateway = server["gateway"].as<String>();
        serverName.toLowerCase();
        serverId.toLowerCase();
        _region.toLowerCase();

        if (serverName == _region || serverId == _region) {
            if (gateway.endsWith("/")) {
                gateway.remove(gateway.length() - 1);
            }
            _base_url = gateway;
            _region = server["id"].as<String>();
            _log(String("Found regional server: ") + _base_url);
            return true;
        }
    }
    _log("Error: Your region was not found in the server list.");
    return false;
}

void PetKitApi::_getDevices() {
    if (_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
    _log("Fetching device list...");
    String response = _sendRequest("/group/family/list", "", false);
    if (response != "") {
        deserializeJson(_device_doc, response);
        _log("Device list fetched.");
    } else {
        _log("Failed to fetch device list.");
    }
}

void PetKitApi::_parsePets() {
    _pets.clear();
    JsonArray accounts = _device_doc.as<JsonArray>();
    for (JsonObject account : accounts) {
        JsonArray petList = account["petList"].as<JsonArray>();
        for (JsonObject pet_json : petList) {
            Pet p;
            p.id = pet_json["petId"].as<int>();
            p.name = pet_json["petName"].as<String>();
            _pets.push_back(p);
        }
    }
    _log(String("Found ") + _pets.size() + " pets.");
}

void PetKitApi::_getLitterboxData(int days_back) {
    _litterbox_records.clear();
    _status_records.clear();

    JsonArray accounts = _device_doc.as<JsonArray>();
    for (JsonObject account : accounts) {
        JsonArray devices = account["deviceList"].as<JsonArray>();
        for (JsonObject device : devices) {
            String device_type = device["deviceType"].as<String>();
            device_type.toLowerCase();

            if (device_type == "t3" || device_type == "t4" || device_type == "t5" || device_type == "t6") {
                _fetchHistoricalData(device, days_back);
            }
        }
    }
    // Sort records by timestamp, descending (newest first)
    std::sort(_litterbox_records.begin(), _litterbox_records.end(), [](const LitterboxRecord& a, const LitterboxRecord& b) {
        return a.timestamp > b.timestamp;
    });
    std::sort(_status_records.begin(), _status_records.end(), [](const StatusRecord& a, const StatusRecord& b) {
        return a.timestamp > b.timestamp;
    });
}

void PetKitApi::_fetchHistoricalData(JsonObject device, int days_back) {
    time_t now_ts;
    time(&now_ts);
    struct tm p_tm;
    localtime_r(&now_ts, &p_tm);

    String deviceId = device["deviceId"].as<String>();
    String deviceType = device["deviceType"].as<String>();
    deviceType.toLowerCase();

    _log(String("\nFetching records for device: ") + device["deviceName"].as<const char *>() + " (" + deviceId + ", type: " + deviceType + ")");

    JsonDocument doc;

    for (int i = 0; i < days_back; i++) {
        if (_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
        
        char date_str_ymd[9];
        strftime(date_str_ymd, sizeof(date_str_ymd), "%Y%m%d", &p_tm);
        
        String endpoint = "/" + deviceType + "/getDeviceRecord";
        String dateKey = (deviceType == "t3") ? "day" : "date";
        String payload_str = dateKey + "=" + String(date_str_ymd) + "&deviceId=" + deviceId;

        String response = _sendRequest(endpoint, payload_str, true, true);

        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            _log(String("Failed to parse records for date ") + date_str_ymd + ": " + error.c_str());
            p_tm.tm_mday -= 1;
            mktime(&p_tm);
            continue;
        }

        JsonArray records = doc.as<JsonArray>();
        if (!records.isNull() && records.size() > 0) {
            _log(String("Found ") + records.size() + " records for " + date_str_ymd);
        }

        for (JsonObject record : records) {
            if (!record["enumEventType"]) continue;

            time_t record_ts = record["timestamp"].as<long>();

            if (record["enumEventType"].as<String>() == "clean_over") {
                StatusRecord sr;
                sr.device_name = device["deviceName"].as<String>();
                sr.device_type = deviceType;
                sr.timestamp = record_ts;
                sr.box_full = record["content"]["boxFull"].as<bool>();
                sr.sand_lack = record["content"]["sandLack"].as<bool>();
                sr.litter_percent = record["content"]["litterPercent"].as<int>();
                _status_records.push_back(sr);
            } else {
                if (!record["petId"] || record["petId"].isNull() || !record["content"] || record["content"].isNull()) continue;
                LitterboxRecord lr;
                lr.device_name = device["deviceName"].as<String>();
                lr.device_type = deviceType;
                lr.pet_id = record["petId"].as<int>();
                lr.pet_name = record["petName"].as<String>();
                lr.timestamp = record_ts;

                if (record["content"]) {
                    lr.weight_grams = record["content"]["petWeight"].as<int>();
                    long time_in = record["content"]["timeIn"].as<long>();
                    long time_out = record["content"]["timeOut"].as<long>();
                    lr.duration_seconds = (time_out > time_in) ? (time_out - time_in) : 0;
                }
                _litterbox_records.push_back(lr);
            }
        }
        
        p_tm.tm_mday -= 1;
        mktime(&p_tm);

        if (deviceType == "t5" || deviceType == "t6") {
            break; // These devices return all data at once, no need to iterate days
        }
    }
}

String PetKitApi::_urlEncode(const String& str) {
    String encodedString = "";
    char c;
    char code0;
    char code1;
    for (unsigned int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == ' ') {
            encodedString += '+';
        } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encodedString += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) {
                code1 = (c & 0xf) - 10 + 'A';
            }
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) {
                code0 = c - 10 + 'A';
            }
            encodedString += '%';
            encodedString += code0;
            encodedString += code1;
        }
    }
    return encodedString;
}

String PetKitApi::_sendRequest(const String& url, const String& payload, bool isPost, bool isFormUrlEncoded) {
    HTTPClient http;
    String finalUrl = _base_url + url;

    if (_debug) {
        _log("--------------------");
        _log("Requesting URL: " + finalUrl);
    }
    
    http.begin(finalUrl);

    http.addHeader("Accept", "*/*");
    http.addHeader("X-Api-Version", "12.4.1");
    http.addHeader("X-Client", "android(15.1;23127PN0CG)");
    http.addHeader("User-Agent", "okhttp/3.12.11");
    if (_session_id != "") {
        http.addHeader("X-Session", _session_id);
    }

    if (isPost) {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    }

    int httpCode;
    if (isPost) {
        if (_debug) {
            _log("Method: POST");
            if (payload != "") _log("Payload: " + payload);
        }
        httpCode = http.POST(payload);
    } else {
        if (_debug) _log("Method: GET");
        httpCode = http.GET();
    }

    if (_debug) _log("HTTP Code: " + String(httpCode));

    String response = "";
    if (httpCode > 0) {
        response = http.getString();
        if (_debug) {
            _log("Response:");
            JsonDocument doc;
            if (deserializeJson(doc, response) == DeserializationError::Ok) {
                serializeJsonPretty(doc, Serial);
                Serial.println();
            } else {
                _log(response); // Print raw response if not valid JSON
            }
        }

        // The API wraps successful results in a "result" object. This extracts it.
        JsonDocument doc;
        if (deserializeJson(doc, response) == DeserializationError::Ok && doc["result"]) {
            String resultStr;
            serializeJson(doc["result"], resultStr);
            http.end();
            return resultStr;
        }
        // Otherwise, return the full response (e.g., for login)
    } else {
        _log(String("HTTP Error: ") + http.errorToString(httpCode).c_str());
    }

    http.end();
    return response;
}


