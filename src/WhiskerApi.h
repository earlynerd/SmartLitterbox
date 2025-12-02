#ifndef WhiskerApi_h
#define WhiskerApi_h
#include <Arduino.h>

#include "SmartLitterbox.h" // Include Interface

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <vector>
#include "time.h"

// Internal Structs
struct WhiskerPet {
    String id;
    String name;
    float weight_lbs;
};

struct WhiskerRecord {
    String device_serial;
    String device_model;
    String pet_id;
    String pet_name;
    time_t timestamp;
    float weight_lbs;
    String event_type;
};

// Inherit from SmartLitterbox
class WhiskerApi : public SmartLitterbox {
public:
    WhiskerApi(const char* email, const char* password, const char* timezone = "UTC0");

    // --- SmartLitterbox Interface Implementation ---

    bool login() override;
    bool syncTime(const char* ntpServer = "pool.ntp.org", const char* tzInfo = "UTC0") override;
    bool fetchAllData(int limit = 10) override;
    void setDebug(bool enabled) override;

    // Adapter: Convert internal WhiskerPet to Unified SL_Pet
    std::vector<SL_Pet> getUnifiedPets() const override {
        std::vector<SL_Pet> unified;
        for (const auto& p : _pets) {
            SL_Pet slp;
            slp.id = p.id;
            slp.name = p.name;
            slp.weight_lbs = p.weight_lbs;
            unified.push_back(slp);
        }
        return unified;
    }

    // Adapter: Convert internal WhiskerRecord to Unified SL_Record
    std::vector<SL_Record> getUnifiedRecords() const override {
        std::vector<SL_Record> unified;
        for (const auto& r : _records) {
            // Only return actual pet visits, ignore machine events like "Drawer Full" for the unified view
            if (r.pet_name.length() > 0 || r.event_type == "Cat Detected") {
                SL_Record slr;
                slr.pet_name = r.pet_name.length() > 0 ? r.pet_name : "Unknown Cat";
                slr.timestamp = r.timestamp;
                slr.weight_lbs = r.weight_lbs;
                slr.duration_seconds = 0; // Whisker API doesn't readily provide duration in this endpoint
                slr.action = r.event_type;
                slr.source_device = r.device_model;
                unified.push_back(slr);
            }
        }
        return unified;
    }

    // --- Original Accessors ---
    const std::vector<WhiskerPet>& getPets() const;
    const std::vector<WhiskerRecord>& getRecords() const;
    std::vector<WhiskerRecord> getRecordsByPetId(String pet_id) const;

private:
    const char* _email;
    const char* _password;
    const char* _timezone;
    bool _debug;

    String _id_token;
    String _access_token;
    String _user_id;

    std::vector<WhiskerPet> _pets;
    std::vector<WhiskerRecord> _records;

    void _log(const String& msg);
    bool _parseJwtForUserId(const String& token);
    
    String _sendRequest(const char* url, const char* method, const String& payload, const char* contentType = "application/json");
    String _sendGraphQL(const char* url, const String& query, const String& variables = "{}");

    void _fetchPets();
    void _fetchPetWeightHistory(const WhiskerPet& pet, int limit);
    void _fetchRobotsAndCycles(int limit);
};

#endif