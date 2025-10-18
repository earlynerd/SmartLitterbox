#include <Arduino.h>
#include <WiFi.h>
#include "PetKitApi.h"
#include <vector>


const char *ssid = "your-ssid-here";
const char *password = "your-password-here";

const char *petkit_username = "your-username-here";
const char *petkit_password = "your-petkit-login-here";
const char *petkit_region = "us";                    // e.g., "us", "eu"
const char *petkit_timezone = "America/Los_Angeles"; // e.g., "America/New_York"

const char *ntpServer = "pool.ntp.org";
const char *tzInfo = "PST8PDT,M3.2.0,M11.1.0";		//will need updating to your POSIX coded timezone. 

PetKitApi petkit(petkit_username, petkit_password, petkit_region, petkit_timezone);


time_t convertToTimestamp(const String &dateStr, const String &timeStr)
{
  // 1. Create a tm structure to hold the parts of the date and time.
  struct tm tm;

  // 2. Combine the strings and parse them using sscanf.
  String datetimeStr = dateStr + " " + timeStr;

  // sscanf is a C function to parse formatted strings.
  // It reads the values directly into the tm struct members.
  sscanf(datetimeStr.c_str(), "%d-%d-%d %d:%d:%d",
         &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
         &tm.tm_hour, &tm.tm_min, &tm.tm_sec);

  // 3. IMPORTANT: Adjust the year and month to match the tm struct's format.
  // tm_year is years since 1900
  tm.tm_year -= 1900;
  // tm_mon is months since January (0-11)
  tm.tm_mon -= 1;

  // 4. Convert the tm structure into a single timestamp number.
  return mktime(&tm);
}


void setup()
{
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  Serial.println("Synchronizing time with NTP server...");
  configTzTime(tzInfo, ntpServer, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 15000))		// 15-second timeout for NTP
  { 
    Serial.println("[Time Sync] System time synced via NTP.");
    setenv("TZ", tzInfo, 1);
    tzset();
  }

  while (now < 8 * 3600 * 2)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();

  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));

  if (petkit.login())
  {
    Serial.println("\nLogin successful! Fetching data...");
    petkit.getDevices();

    int pet_count = 0;
    Pet *pets = petkit.getPets(pet_count);
    if (pets != nullptr)
    {
      Serial.printf("Found %d pets:\n", pet_count);
      for (int i = 0; i < pet_count; i++)
      {
        Serial.printf(" - Pet ID: %d, Name: %s\n", pets[i].id, pets[i].name.c_str());
      }
    }
    // --- Fetch records for the last 30 days ---
    Serial.println("\nFetching litterbox records for the last 30 days...");
    petkit.getLitterboxWeightData(30);

    int record_count = 0;
    LitterboxRecord *records = petkit.getRecords(record_count);
    std::vector<DataPoint> petRecords[pet_count];

    if (records != nullptr)
    {
      Serial.printf("\nFound %d litterbox records:\n", record_count);
      for (int i = 0; i < pet_count; i++)
      {
        std::vector<DataPoint> thisPetData;
        int thisPetID = pets[i].id;
        String thisPetName = pets[i].name;
        for (int j = 0; j < record_count; j++)
        {
          time_t stamp = convertToTimestamp(records[j].date, records[j].time);
          if (records[j].pet_id == thisPetID)
          {
            float weight = (float)records[j].weight_grams / (float)GRAMS_PER_POUND;
            petRecords[i].push_back({(float)stamp, weight});
            Serial.print(stamp);
            Serial.print(", ");
            Serial.print(thisPetName);
            Serial.print(", ");
            Serial.println(weight, 2);
          }
        }
      }
     
    }
    else
    {
      Serial.println("No litterbox records found.");
    }
  }
  else
  {
    Serial.println("\nLogin failed. Please check the serial monitor for detailed error messages.");
  }
}

void loop()
{

  // Nothing to do here
}
