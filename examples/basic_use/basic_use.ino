#include <Arduino.h>
#include <WiFi.h>
#include "PetKitApi.h"

#define GRAMS_PER_POUND 453.592f

// --- WiFi and Petkit Credentials ---
const char *ssid = "your-ssid-here";
const char *password = "your-password-here";

const char *petkit_username = "your-username-here";
const char *petkit_password = "your-petkit-login-here";
const char *petkit_region = "us"; // e.g., "us", "eu"

// --- Time Configuration ---
// Find your timezone string here: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const char *petkit_timezone = "America/Los_Angeles"; 
const char *ntpServer = "pool.ntp.org";
// This POSIX string must match your timezone. It's used for correct local time conversion.
const char *tzInfo = "PST8PDT,M3.2.0,M11.1.0"; // POSIX timezone string for America/Los_Angeles

// Initialize the PetKitApi object
PetKitApi petkit(petkit_username, petkit_password, petkit_region, petkit_timezone);

void setup() {
  Serial.begin(115200);
  Serial.println("\nPetkit Library Example");

  // --- Connect to WiFi ---
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  // Enable debug logging from the library for detailed output. quite verbose...
  //petkit.setDebug(true);

  // --- Sync Time ---
  // The library now handles NTP time synchronization. This is required before login.
  if (!petkit.syncTime(ntpServer, tzInfo)) {
    Serial.println("Time sync failed! Halting.");
    while(1) delay(1000);
  }

  // --- Login and Fetch Data ---
  if (petkit.login()) {
    Serial.println("\nLogin successful! Fetching data...");
    
    // fetchAllData() gets devices, pets, and historical records in one call
    if (petkit.fetchAllData(30)) { // Get records for the last 30 days
      
      // --- Get Pet Information ---
      const auto& pets = petkit.getPets();
      Serial.printf("\nFound %zu pets:\n", pets.size());
      for (const auto& pet : pets) {
        Serial.printf(" - Pet ID: %d, Name: %s\n", pet.id, pet.name.c_str());
      }

      // --- Get Latest Litterbox Status ---
      StatusRecord latest_status = petkit.getLatestStatus();
      if (latest_status.device_name != "") {
        char timeStr[32];
        // Use localtime() to convert the Unix timestamp to a human-readable format
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", localtime(&latest_status.timestamp));
        Serial.println("\n--- Latest Status ---");
        Serial.printf("Device: %s\n", latest_status.device_name.c_str());
        Serial.printf("Time: %s\n", timeStr);
        Serial.printf("Litter Level: %d%%\n", latest_status.litter_percent);
        Serial.printf("Waste Box Full: %s\n", latest_status.box_full ? "Yes" : "No");
        Serial.printf("Litter Low: %s\n", latest_status.sand_lack ? "Yes" : "No");
        Serial.println("---------------------");
      }

      // --- Get Historical Records ---
      // The old way iterated through all records at once.
      // The new, improved way below separates records by pet.
      Serial.println("\n--- Historical Records by Pet ---");
      for (const auto& pet : pets) {
        Serial.printf("\n--- Records for %s (ID: %d) ---\n", pet.name.c_str(), pet.id);
        
        // Use the new helper function to get a vector of records just for this pet
        std::vector<LitterboxRecord> pet_records = petkit.getLitterboxRecordsByPetId(pet.id);

        if (pet_records.empty()) {
            Serial.println("No records found for this pet in the last 30 days.");
            continue;
        }

        // Now, loop through the filtered list for this specific pet
        for (const auto& record : pet_records) {
            float weight_lbs = (float)record.weight_grams / GRAMS_PER_POUND;
            char timeStr[32];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", localtime(&record.timestamp));

            Serial.printf("[%s] Weight: %.2f lbs, Duration: %d sec\n",
              timeStr,
              weight_lbs,
              record.duration_seconds
            );
        }
      }

    } else {
      Serial.println("Failed to fetch data.");
    }
  } else {
    Serial.println("\nLogin failed. Please check credentials and serial monitor for detailed error messages.");
  }
}

void loop() {
  // Nothing to do here
}

