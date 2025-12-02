#ifndef SmartLitterbox_h
#define SmartLitterbox_h

#include <Arduino.h>
#include <vector>

// --- Unified Data Structures ---

struct SL_Pet {
    String id;          // Unified to String (Petkit int IDs will be converted)
    String name;
    float weight_lbs;
};

struct SL_Record {
    String pet_name;
    time_t timestamp;
    float weight_lbs;
    float duration_seconds; // 0 for Whisker (usually)
    String action;          // "Clean", "Visit", etc.
    String source_device;   // "Litter-Robot 4" or "Petkit Pura X"
};

// --- Abstract Base Class ---

class SmartLitterbox {
public:
    virtual ~SmartLitterbox() {}

    // Authentication & Setup
    virtual bool login() = 0;
    virtual bool syncTime(const char* ntpServer, const char* tzInfo) = 0;

    // Data Fetching
    // 'param' is interpreted as 'days_back' for Petkit or 'limit' for Whisker
    virtual bool fetchAllData(int param = 10) = 0; 

    // Data Accessors (Polymorphic)
    // These generate the unified structures on the fly from the internal data
    virtual std::vector<SL_Pet> getUnifiedPets() const = 0;
    virtual std::vector<SL_Record> getUnifiedRecords() const = 0;
    
    // Debugging
    virtual void setDebug(bool enabled) = 0;
};

#endif