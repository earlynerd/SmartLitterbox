#ifndef SmartLitterbox_h
#define SmartLitterbox_h

#include <Arduino.h>
#include <vector>

// --- Unified Data Structures ---

struct SL_Pet {
    String id;
    String name;
    float weight_lbs;
};

struct SL_Record {
    String pet_name;
    int PetId;
    time_t timestamp;
    float weight_lbs;
    float duration_seconds;
    String action;
    String source_device;
};

enum ApiType
{
    PETKIT,
    WHISKER
};
// New Unified Status Struct
struct SL_Status {
    ApiType api_type;
    String device_name;
    String device_type;
    time_t timestamp;
    int litter_level_percent;   // 0-100
    int waste_level_percent;    // 0-100 (DFI for Whisker)
    bool is_drawer_full;
    bool is_error_state;        // Generic error flag
    String status_text;         // e.g., "Ready", "Cleaning", "Cat Detected"
};

// --- Abstract Base Class ---

class SmartLitterbox {
public:
    virtual ~SmartLitterbox() {}

    // Authentication
    virtual bool login() = 0;

    // Data Fetching
    virtual bool fetchAllData(int param = 10) = 0; 

    // Unified Accessors (Must be implemented by children)
    virtual std::vector<SL_Pet> getUnifiedPets() const = 0;
    virtual std::vector<SL_Record> getUnifiedRecords() const = 0;
    
    // Unified Status Accessor
    virtual SL_Status getUnifiedStatus() const = 0;
    
    // Get a specific pet by ID
    SL_Pet getPetById(String id) const {
        std::vector<SL_Pet> pets = getUnifiedPets();
        for (const auto& p : pets) {
            if (p.id == id) return p;
        }
        return SL_Pet{"", "", 0.0}; // Return empty if not found
    }

    // Get a specific pet by Name
    SL_Pet getPetByName(String name) const {
        std::vector<SL_Pet> pets = getUnifiedPets();
        for (const auto& p : pets) {
            if (p.name == name) return p;
        }
        return SL_Pet{"", "", 0.0}; // Return empty if not found
    }

    // Get Records for a specific pet ID
    std::vector<SL_Record> getRecordsByPetId(String petNameOrId, bool isId = true) const {
        std::vector<SL_Record> allRecords = getUnifiedRecords();
        std::vector<SL_Record> filtered;
        
        String targetName = "";
        
        if (isId) {
            SL_Pet p = getPetById(petNameOrId);
            if (p.id != "") targetName = p.name;
        } else {
            targetName = petNameOrId;
        }

        if (targetName == "") return filtered; // Pet not found

        for (const auto& r : allRecords) {
            if (r.pet_name == targetName) {
                filtered.push_back(r);
            }
        }
        return filtered;
    }

    virtual void setDebug(bool enabled) = 0;
};

#endif