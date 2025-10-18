#include "PetKitApi.h"
#include "mbedtls/md5.h"

// MD5 and urlEncode functions remain the same...
String md5(String str)
{
    byte digest[16];
    char hex_digest[33];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    mbedtls_md5_update(&ctx, (const unsigned char *)str.c_str(), str.length());
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);

    for (int i = 0; i < 16; i++)
    {
        sprintf(&hex_digest[i * 2], "%02x", (unsigned int)digest[i]);
    }
    hex_digest[32] = 0;
    return String(hex_digest);
}

String PetKitApi::_urlEncode(String str)
{
    String encodedString = "";
    char c;
    char code0;
    char code1;
    for (int i = 0; i < str.length(); i++)
    {
        c = str.charAt(i);
        if (c == ' ')
        {
            encodedString += '+';
        }
        else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encodedString += c;
        }
        else
        {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9)
            {
                code1 = (c & 0xf) - 10 + 'A';
            }
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9)
            {
                code0 = c - 10 + 'A';
            }
            encodedString += '%';
            encodedString += code0;
            encodedString += code1;
        }
    }
    return encodedString;
}

PetKitApi::PetKitApi(const char *username, const char *password, const char *region, const char *timezone, int led) : _username(username),
                                                                                                             _password(password),
                                                                                                             _region(region),
                                                                                                             _timezone(timezone),
                                                                                                             _ledpin(led)
{
    _base_url = "https://passport.petkt.com";
}

// _getTimezoneOffset, _getBaseUrl, login, getDevices, getRecords, getPets, _sendRequest remain the same...
String PetKitApi::_getTimezoneOffset()
{
    // This is a simplified offset calculation. A robust library would be better.
    if (strcmp(_timezone, "America/New_York") == 0)
        return "-4.0";
    if (strcmp(_timezone, "America/Chicago") == 0)
        return "-5.0";
    if (strcmp(_timezone, "America/Denver") == 0)
        return "-6.0";
    if (strcmp(_timezone, "America/Los_Angeles") == 0)
        return "-7.0";
    if (strcmp(_timezone, "Europe/London") == 0)
        return "1.0";
    if (strcmp(_timezone, "Europe/Berlin") == 0)
        return "2.0";
    return "0.0";
}

bool PetKitApi::_getBaseUrl()
{
    Serial.println("Getting regional server URL...");
    String response = _sendRequest("/v1/regionservers", "", false);

    if (response == "")
    {
        Serial.println("Failed to get region servers list.");
        return false;
    }

    JsonDocument doc;
    deserializeJson(doc, response);

    JsonArray servers = doc["list"].as<JsonArray>();
    for (JsonObject server : servers)
    {
        String serverName = server["name"].as<String>();
        String serverId = server["id"].as<String>();
        String gateway = server["gateway"].as<String>();
        serverName.toLowerCase();
        serverId.toLowerCase();
        _region.toLowerCase();

        if (serverName == _region || serverId == _region)
        {
            if (gateway.endsWith("/"))
            {
                gateway.remove(gateway.length() - 1);
            }
            _base_url = gateway;
            _region = server["id"].as<String>();
            Serial.print("Found regional server: ");
            Serial.println(_base_url);
            return true;
        }
    }

    Serial.println("Your region was not found in the server list.");
    return false;
}

bool PetKitApi::login()
{
    if (!_getBaseUrl())
    {
        return false;
    }
    if(_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
    Serial.println("Attempting to log in...");
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
    if(_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
    String payload = "oldVersion=12.4.1";
    payload += "&client=" + _urlEncode(client_nfo_str);
    payload += "&encrypt=1";
    payload += "&region=" + _region;
    payload += "&username=" + _urlEncode(_username);
    payload += "&password=" + md5(_password);

    String response = _sendRequest("/user/login", payload, true, true);

    if (response == "")
    {
        Serial.println("Login request failed. No response from server.");
        return false;
    }
    if(_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
    JsonDocument result;
    DeserializationError error = deserializeJson(result, response);

    if (error)
    {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        Serial.println("Response was: " + response);
        return false;
    }

    if (result["session"])
    {
        _session_id = result["session"]["id"].as<String>();
        Serial.println("Login successful!");
        return true;
    }
    else
    {
        Serial.println("Login failed. Check credentials and region.");
        Serial.println("Server response:");
        serializeJsonPretty(result, Serial);
        Serial.println();
        return false;
    }
}

void PetKitApi::getDevices()
{
    if(_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
    Serial.println("Fetching device list...");
    String response = _sendRequest("/group/family/list", "", false);
    if (response != "")
    {
        deserializeJson(_device_data, response);
        Serial.println("Device list fetched.");
    }
    else
    {
        Serial.println("Failed to fetch device list.");
    }
}

LitterboxRecord *PetKitApi::getRecords(int &count)
{
    JsonArray recordsArray = _litterbox_records.as<JsonArray>();
    count = recordsArray.size();
    if (count == 0)
        return nullptr;
    LitterboxRecord *records = new LitterboxRecord[count];

    int i = 0;
    for (JsonObject record : recordsArray)
    {
        if(_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
        records[i].device_name = record["device_name"].as<String>();
        records[i].device_type = record["device_type"].as<String>();
        records[i].pet_id = record["pet_id"].as<int>();
        records[i].pet_name = record["pet_name"].as<String>();
        records[i].date = record["date"].as<String>();
        records[i].time = record["time"].as<String>();
        records[i].weight_grams = record["weight_grams"].as<int>();
        records[i].duration_seconds = record["duration_seconds"].as<int>();
        // records[i].litter_percent = record["litter_percent"].as<int>();
        // records[i].box_full = record["boxFull"].as<bool>();
        i++;
    }
    return records;
}

StatusRecord *PetKitApi::getStatusRecords(int &count)
{
    JsonArray recordsArray = _status_records.as<JsonArray>();
    count = recordsArray.size();
    if (count == 0)
        return nullptr;
    StatusRecord *records = new StatusRecord[count];

    int i = 0;
    for (JsonObject record : recordsArray)
    {
        records[i].device_name = record["device_name"].as<String>();
        records[i].device_type = record["device_type"].as<String>();
        
        records[i].date = record["date"].as<String>();
        records[i].time = record["time"].as<String>();
        records[i].litter_percent = record["litter_percent"].as<int>();
        records[i].box_full = record["boxFull"].as<bool>();
        records[i].sandLack = record["sandlack"].as<bool>();
        i++;
    }
    return records;
}

Pet *PetKitApi::getPets(int &count)
{
    count = 0;
    JsonArray accounts = _device_data.as<JsonArray>();
    for (JsonObject account : accounts)
    {
        count += account["petList"].as<JsonArray>().size();
    }
    if (count == 0)
        return nullptr;

    Pet *pet_list = new Pet[count];
    int i = 0;
    for (JsonObject account : accounts)
    {
        JsonArray pets = account["petList"].as<JsonArray>();
        for (JsonObject pet : pets)
        {
            pet_list[i].id = pet["petId"].as<int>();
            pet_list[i].name = pet["petName"].as<String>();
            i++;
        }
    }
    return pet_list;
}

String PetKitApi::_sendRequest(String url, String payload, bool isPost, bool isFormUrlEncoded)
{
    HTTPClient http;
    String finalUrl = _base_url + url;

    // Serial.println("--------------------");
    // Serial.print("Requesting URL: ");
    // Serial.println(finalUrl);

    http.begin(finalUrl);

    http.addHeader("Accept", "*/*");
    http.addHeader("X-Api-Version", "12.4.1");
    http.addHeader("X-Client", "android(15.1;23127PN0CG)");
    http.addHeader("User-Agent", "okhttp/3.12.11");
    if (_session_id != "")
    {
        http.addHeader("X-Session", _session_id);
    }

    if (isPost)
    {
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    }

    int httpCode;
    if (isPost)
    {
        // Serial.println("Method: POST");
        if (payload != "")
        {
            // Serial.print("Payload: ");
            // Serial.println(payload);
        }
        httpCode = http.POST(payload);
    }
    else
    {
        // Serial.println("Method: GET");
        httpCode = http.GET();
    }

    // Serial.print("HTTP Code: ");
    // Serial.println(httpCode);

    String response = "";
    if (httpCode > 0)
    {
        response = http.getString();
        // Serial.println("Response:");
        if (response.length() < 1000)
        {
            // Serial.println(response);
        }
        else
        {
            // Serial.print(response.substring(0, 1000));
            // Serial.println("... (response truncated)");
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        if (error)
        {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return response;
        }

        if (doc["result"])
        {
            String resultStr;
            serializeJson(doc["result"], resultStr);
            return resultStr;
        }
        else
        {
            return response;
        }
    }
    else
    {
        Serial.print("HTTP Error: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    // Serial.println("--------------------");
    http.end();
    return response;
}

void PetKitApi::getLitterboxWeightData(int days_back)
{
    _litterbox_records.clear();
    _status_records.clear();
    JsonArray records = _litterbox_records.to<JsonArray>();

    JsonArray accounts = _device_data.as<JsonArray>();
    for (JsonObject account : accounts)
    {
        JsonArray devices = account["deviceList"].as<JsonArray>();
        for (JsonObject device : devices)
        {
            String device_type = device["deviceType"].as<String>();
            device_type.toLowerCase(); // *** THIS IS THE FIX ***

            if (device_type == "t3" || device_type == "t4" || device_type == "t5" || device_type == "t6")
            {
                _fetchHistoricalData(device, days_back);
            }
        }
    }
}

void PetKitApi::_fetchHistoricalData(JsonObject device, int days_back)
{
    time_t now_ts;
    time(&now_ts);
    struct tm *p_tm = localtime(&now_ts);

    String deviceId = device["deviceId"].as<String>();
    String deviceType = device["deviceType"].as<String>();
    deviceType.toLowerCase(); // Also convert to lower case here for consistency

    Serial.printf("\nFetching records for device: %s (%s, type: %s)\n", device["deviceName"].as<const char *>(), deviceId.c_str(), deviceType.c_str());

    JsonArray recordsArray = _litterbox_records.to<JsonArray>();
    JsonArray statusArray = _status_records.to<JsonArray>();
    JsonDocument doc;

    for (int i = 0; i < days_back; i++)
    {
        if(_ledpin > 0) digitalWrite(_ledpin, !digitalRead(_ledpin));
        char date_str_ymd[9];
        strftime(date_str_ymd, sizeof(date_str_ymd), "%Y%m%d", p_tm);

        String endpoint;
        String payload_str;

        endpoint = "/" + deviceType + "/getDeviceRecord";

        String dateKey = (deviceType == "t3") ? "day" : "date";
        payload_str = dateKey + "=" + String(date_str_ymd) + "&deviceId=" + deviceId;

        String response = _sendRequest(endpoint, payload_str, true, true);

        DeserializationError error = deserializeJson(doc, response);
        if (error)
        {
            Serial.print("Failed to parse JSON response for records: ");
            Serial.println(error.c_str());
            p_tm->tm_mday -= 1;
            mktime(p_tm);
            continue;
        }

        JsonArray records = doc.as<JsonArray>();

        if (records.isNull() || records.size() == 0)
        {
            Serial.printf("No records found for %s\n", date_str_ymd);
        }
        else
        {
            Serial.printf("Found %d records for %s\n", records.size(), date_str_ymd);
        }

        for (JsonObject record : records)
        {

            if (!record["enumEventType"])
            {
                continue;
            }
            if (!record["petId"] || record["petId"].isNull() || !record["content"] || record["content"].isNull())
            {
                continue;
            }
            if (record["enumEventType"].as<String>() == "clean_over")
            {
                JsonObject new_record = statusArray.add<JsonObject>();
                new_record["device_name"] = device["deviceName"].as<String>();
                new_record["device_type"] = deviceType;
                new_record["boxfull"] = record["content"]["boxFull"].as<bool>();
                new_record["sandlack"] = record["content"]["sandLack"].as<bool>();
                new_record["litterpercent"] = record["content"]["litterPercent"].as<int>();
                long record_ts = record["timestamp"].as<long>();
                time_t record_time = record_ts;
                struct tm *record_tm = localtime(&record_time);
                char record_date_str[11];
                char record_time_str[9];
                strftime(record_date_str, sizeof(record_date_str), "%Y-%m-%d", record_tm);
                strftime(record_time_str, sizeof(record_time_str), "%H:%M:%S", record_tm);

                new_record["date"] = record_date_str;
                new_record["time"] = record_time_str;
                // boxfull = record["content"]["boxFull"].as<bool>();
                // litterpercent = record["content"]["litterPercent"].as<int>();
            }
            else
            {
                JsonObject new_record = recordsArray.add<JsonObject>();
                new_record["device_name"] = device["deviceName"].as<String>();
                new_record["device_type"] = deviceType;
                new_record["pet_id"] = record["petId"].as<int>();
                new_record["pet_name"] = record["petName"].as<String>();

                long record_ts = record["timestamp"].as<long>();
                time_t record_time = record_ts;
                struct tm *record_tm = localtime(&record_time);
                char record_date_str[11];
                char record_time_str[9];
                strftime(record_date_str, sizeof(record_date_str), "%Y-%m-%d", record_tm);
                strftime(record_time_str, sizeof(record_time_str), "%H:%M:%S", record_tm);

                new_record["date"] = record_date_str;
                new_record["time"] = record_time_str;

                if (record["content"])
                {
                    new_record["weight_grams"] = record["content"]["petWeight"].as<int>();
                    long time_in = record["content"]["timeIn"].as<long>();
                    long time_out = record["content"]["timeOut"].as<long>();
                    new_record["duration_seconds"] = (time_out > time_in) ? (time_out - time_in) : 0;
                    //new_record["litter_percent"] = litterpercent;
                    //new_record["box_full"] = boxfull;
                }
            }
        }

        p_tm->tm_mday -= 1;
        mktime(p_tm);

        if (deviceType == "t5" || deviceType == "t6")
        {
            break;
        }
    }
}