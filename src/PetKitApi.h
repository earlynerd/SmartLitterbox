#ifndef PetKitApi_h
#define PetKitApi_h

#include "Arduino.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Helper function for MD5 hashing
String md5(String str);

struct Pet {
    int id;
    String name;
};

struct LitterboxRecord {
    String device_name;
    String device_type;
    int pet_id;
    String pet_name;
    String date;
    String time;
    int weight_grams;
    int duration_seconds;
};

struct StatusRecord {
    String device_name;
    String device_type;
    String date;
    String time;
    int litter_percent;
    bool box_full;
    bool sandLack;
};


class PetKitApi {
public:
    PetKitApi(const char* username, const char* password, const char* region, const char* timezone, int led = -1);
    bool login();
    void getDevices();
    void getLitterboxWeightData(int days_back = 30);
    LitterboxRecord* getRecords(int& count);
    StatusRecord* getStatusRecords(int& count);
    Pet* getPets(int& count);

private:
    int _ledpin;
    const char* _username;
    const char* _password;
    String _region;
    const char* _timezone;
    String _session_id;
    String _base_url;

    JsonDocument _device_data;
    JsonDocument _pet_data;
    JsonDocument _litterbox_records;
    JsonDocument _status_records;

    bool _getBaseUrl();
    String _sendRequest(String url, String payload, bool isPost = true, bool isFormUrlEncoded = false);
    void _fetchHistoricalData(JsonObject device, int days_back);
    String _getTimezoneOffset();
    String _urlEncode(String str);
};

#endif