#ifndef PetKitApi_h
#define PetKitApi_h

#include "Arduino.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include "time.h"

// Forward declaration for MD5 helper
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
    time_t timestamp; // Unix timestamp of the event
    int weight_grams;
    int duration_seconds;
};

struct StatusRecord {
    String device_name;
    String device_type;
    time_t timestamp; // Unix timestamp of the event
    int litter_percent;
    bool box_full;
    bool sand_lack;
};

class PetKitApi {
public:
    PetKitApi(const char* username, const char* password, const char* region, const char* timezone, int led = -1);

    // --- Quality of Life Improvements ---
    
    /**
     * @brief Enables or disables detailed debug logging to the Serial monitor.
     * @param enabled Set to true to see debug output.
     */
    void setDebug(bool enabled);

    /**
     * @brief Synchronizes the ESP32's internal clock with an NTP server.
     * @param ntpServer The NTP server to use.
     * @param tzInfo The POSIX timezone string for your location.
     * @return True if time was synchronized successfully, false otherwise.
     */
    bool syncTime(const char* ntpServer = "pool.ntp.org", const char* tzInfo = "UTC0");

    // --- Core API Methods ---

    /**
     * @brief Authenticates with the Petkit servers using your credentials.
     * @return True on successful login, false otherwise.
     */
    bool login();

    /**
     * @brief Fetches all devices, pets, and historical data in a single operation.
     * @param days_back The number of days of historical data to retrieve.
     * @return True if data was fetched successfully, false otherwise.
     */
    bool fetchAllData(int days_back = 30);

    // --- Data Accessors ---

    /**
     * @brief Gets the list of registered pets.
     * @return A constant reference to a vector of Pet structs.
     */
    const std::vector<Pet>& getPets() const;

    /**
     * @brief Gets the historical litterbox usage records.
     * @return A constant reference to a vector of LitterboxRecord structs, sorted newest first.
     */
    const std::vector<LitterboxRecord>& getLitterboxRecords() const;

    /**
     * @brief Gets the historical status update records (e.g., after a clean cycle).
     * @return A constant reference to a vector of StatusRecord structs, sorted newest first.
     */
    const std::vector<StatusRecord>& getStatusRecords() const;
    
    /**
     * @brief Gets the historical litterbox usage records for a specific pet.
     * @param pet_id The ID of the pet to filter records for.
     * @return A vector of LitterboxRecord structs for the specified pet.
     */
    std::vector<LitterboxRecord> getLitterboxRecordsByPetId(int pet_id) const;

    /**
     * @brief A helper to get the single most recent status record.
     * @return The latest StatusRecord. If no records exist, an empty struct is returned.
     */
    StatusRecord getLatestStatus() const;

private:
    // Logging helpers
    void _log(const char* message);
    void _log(const String& message);

    // Member variables
    int _ledpin;
    bool _debug;
    const char* _username;
    const char* _password;
    String _region;
    const char* _timezone;
    String _session_id;
    String _base_url;

    // Internal data storage
    JsonDocument _device_doc;
    std::vector<Pet> _pets;
    std::vector<LitterboxRecord> _litterbox_records;
    std::vector<StatusRecord> _status_records;

    // Private helper methods
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

