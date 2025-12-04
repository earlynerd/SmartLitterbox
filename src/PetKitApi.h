#ifndef PetKitApi_h
#define PetKitApi_h

#include "SmartLitterbox.h"
#include "Arduino.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

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
    time_t timestamp;
    int weight_grams;
    int duration_seconds;
};

struct StatusRecord {
    String device_name;
    String device_type;
    time_t timestamp; 
    int litter_percent;
    bool box_full;
    bool sand_lack;
};

class PetKitApi : public SmartLitterbox {
public:
    PetKitApi(const char* username, const char* password, const char* region, const char* timezone, int led = -1);
    ~PetKitApi();
    // --- Interface Implementation ---
    bool login() override;
    bool fetchAllData(int days_back = 30) override;
    void setDebug(bool enabled) override;

    std::vector<SL_Pet> getUnifiedPets() const override {
        std::vector<SL_Pet> unified;
        for (const auto& p : _pets) {
            SL_Pet slp;
            slp.id = String(p.id); 
            slp.name = p.name;
            slp.weight_lbs = 0.0;
            unified.push_back(slp);
        }
        return unified;
    }

    std::vector<SL_Record> getUnifiedRecords() const override {
        std::vector<SL_Record> unified;
        for (const auto& r : _litterbox_records) {
            SL_Record slr;
            slr.pet_name = r.pet_name;
            slr.timestamp = r.timestamp;
            slr.weight_lbs = r.weight_grams * 0.00220462;
            slr.duration_seconds = (float)r.duration_seconds;
            slr.action = "Visit";
            slr.source_device = r.device_type;
            slr.PetId = r.pet_id;
            unified.push_back(slr);
        }
        return unified;
    }

    // New Unified Status Implementation
    SL_Status getUnifiedStatus() const override {
        if (_status_records.empty()) return SL_Status{ApiType::PETKIT,"", "", 0, 0, 0, false, false, "Unknown"};
        
        const auto& r = _status_records.front(); // Get latest
        SL_Status s;
        s.api_type = ApiType::PETKIT;
        s.device_name = r.device_name;
        s.device_type = r.device_type;
        s.timestamp = r.timestamp;
        s.litter_level_percent = r.litter_percent;
        s.waste_level_percent = r.box_full ? 100 : 0; // PetKit is binary for full/not full usually
        s.is_drawer_full = r.box_full;
        s.is_error_state = false; // PetKit API doesn't easily expose this in history
        
        if (r.box_full) s.status_text = "Drawer Full";
        else if (r.sand_lack) s.status_text = "Low Litter";
        else s.status_text = "Ready";
        
        return s;
    }

    // --- Original Methods ---
    const std::vector<Pet>& getPets() const;
    const std::vector<LitterboxRecord>& getLitterboxRecords() const;
    const std::vector<StatusRecord>& getStatusRecords() const;
    std::vector<LitterboxRecord> getLitterboxRecordsByPetId(int pet_id) const;
    StatusRecord getLatestStatus() const;

private:
    void _log(const char* message);
    void _log(const String& message);

    int _ledpin;
    bool _debug;
    const char* _username;
    const char* _password;
    String _region;
    String _timezone;
    String _session_id;
    String _base_url;

    JsonDocument _device_doc;
    std::vector<Pet> _pets;
    std::vector<LitterboxRecord> _litterbox_records;
    std::vector<StatusRecord> _status_records;

    bool _getBaseUrl();
    void _getDevices();
    void _getLitterboxData(int days_back);
    void _parsePets();
    String _sendRequest(const String& url, const String& payload, bool isPost = true, bool isFormUrlEncoded = false);
    void _fetchHistoricalData(JsonObject device, int days_back);
    String _getTimezoneOffset();
    static String _urlEncode(const String& str);
};

#endif