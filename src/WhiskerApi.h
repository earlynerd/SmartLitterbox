#ifndef WhiskerApi_h
#define WhiskerApi_h

#include <Arduino.h>
#include "SmartLitterbox.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <vector>

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

// New Status Structure
struct WhiskerStatus {
    String device_serial;
    String device_model;
    time_t timestamp;
    int litter_level_percent;   // Calculated from ToF sensor
    int waste_level_percent;    // DFI Level
    bool is_drawer_full;
    String robot_status;        // e.g., ROBOT_IDLE, ROBOT_CLEAN
};

class WhiskerApi : public SmartLitterbox {
public:
    WhiskerApi(const char* email, const char* password, const char* timezone);
    ~WhiskerApi();
    // --- Interface Implementation ---
    bool login() override;
    bool fetchAllData(int limit = 10) override;
    void setDebug(bool enabled) override;

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

    std::vector<SL_Record> getUnifiedRecords() const override {
        std::vector<SL_Record> unified;
        for (const auto& r : _records) {
            if (r.pet_name.length() > 0 || r.event_type == "Pet Weight Recorded") {
                SL_Record slr;
                slr.pet_name = r.pet_name.length() > 0 ? r.pet_name : "Unknown Cat";
                slr.timestamp = r.timestamp;
                slr.weight_lbs = r.weight_lbs;
                slr.duration_seconds = 0; 
                slr.action = r.event_type;
                slr.source_device = r.device_model;
                slr.PetId = r.pet_id.toInt();
                unified.push_back(slr);
            }
        }
        return unified;
    }

    // New Unified Status Implementation
    SL_Status getUnifiedStatus() const override {
        if (_status_records.empty()) return SL_Status{ApiType::WHISKER,"", "", 0, 0, 0, false, false, "Unknown"};
        
        const auto& r = _status_records.front();
        SL_Status s;
        s.api_type = ApiType::WHISKER;
        s.device_name = r.device_serial; // Whisker uses Serial as primary ID often
        s.device_type = r.device_model;
        s.timestamp = r.timestamp;
        s.litter_level_percent = r.litter_level_percent;
        s.waste_level_percent = r.waste_level_percent;
        s.is_drawer_full = r.is_drawer_full;
        
        // Map common Whisker statuses to text
        s.status_text = r.robot_status; 
        if (r.robot_status == "ROBOT_IDLE") s.status_text = "Ready";
        else if (r.robot_status == "ROBOT_CLEAN") s.status_text = "Cleaning";
        else if (r.robot_status == "ROBOT_CAT_DETECT") s.status_text = "Cat Detected";
        
        s.is_error_state = (r.robot_status.indexOf("FAULT") != -1);
        
        return s;
    }

    const std::vector<WhiskerStatus>& getStatusRecords() const { return _status_records; }
    
    WhiskerStatus getLatestStatus() const {
        if (_status_records.empty()) return WhiskerStatus{};
        return _status_records.front();
    }

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
    std::vector<WhiskerStatus> _status_records; 

    void _log(const String& msg);
    bool _parseJwtForUserId(const String& token);
    
    String _sendRequest(const char* url, const char* method, const String& payload, const char* contentType = "application/json");
    String _sendGraphQL(const char* url, const String& query, const String& variables = "{}");

    void _fetchPets();
    void _fetchPetWeightHistory(const WhiskerPet& pet, int limit);
    void _fetchRobotsAndCycles(int limit);
};

#endif