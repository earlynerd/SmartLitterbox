#include "WhiskerApi.h"
#include "mbedtls/base64.h"

// Whisker / AWS Constants
const char* COGNITO_ENDPOINT = "https://cognito-idp.us-east-1.amazonaws.com/";
const char* WHISKER_CLIENT_ID = "4552ujeu3aic90nf8qn53levmn"; // Public App Client ID
const char* API_LR4_GRAPHQL = "https://lr4.iothings.site/graphql";
const char* API_PET_GRAPHQL = "https://pet-profile.iothings.site/graphql";

WhiskerApi::WhiskerApi(const char* email, const char* password, const char* timezone) 
    : _email(email), _password(password), _timezone(timezone), _debug(false) {}

void WhiskerApi::setDebug(bool enabled) {
    _debug = enabled;
}

void WhiskerApi::_log(const String& msg) {
    if (_debug && Serial) Serial.println("[WhiskerApi] " + msg);
}

// --- Time Sync (Crucial for SSL) ---
bool WhiskerApi::syncTime(const char* ntpServer, const char* tzInfo) {
    _log("Syncing time...");
    configTzTime(tzInfo, ntpServer);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) {
        _log("Time sync failed.");
        return false;
    }
    _log("Time synced.");
    return true;
}

// --- Authentication ---
bool WhiskerApi::login() {
    if (WiFi.status() != WL_CONNECTED) {
        _log("WiFi not connected.");
        return false;
    }

    _log("Authenticating with AWS Cognito...");

    // Basic USER_PASSWORD_AUTH flow
    JsonDocument doc;
    doc["ClientId"] = WHISKER_CLIENT_ID;
    doc["AuthFlow"] = "USER_PASSWORD_AUTH";
    JsonObject authParams = doc["AuthParameters"].to<JsonObject>();
    authParams["USERNAME"] = _email;
    authParams["PASSWORD"] = _password;

    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    http.begin(COGNITO_ENDPOINT);
    http.addHeader("Content-Type", "application/x-amz-json-1.1");
    http.addHeader("X-Amz-Target", "AWSCognitoIdentityProviderService.InitiateAuth");

    int httpCode = http.POST(payload);
    String response = http.getString();
    http.end();

    if (httpCode != 200) {
        _log("Login Failed: " + String(httpCode));
        _log("Response: " + response);
        return false;
    }

    JsonDocument respDoc;
    deserializeJson(respDoc, response);

    if (respDoc["AuthenticationResult"]) {
        _id_token = respDoc["AuthenticationResult"]["IdToken"].as<String>();
        _access_token = respDoc["AuthenticationResult"]["AccessToken"].as<String>();
        
        // Extract User ID (mid) from JWT
        if (_parseJwtForUserId(_id_token)) {
            _log("Login Successful. User ID: " + _user_id);
            return true;
        }
    }
    
    _log("Failed to parse tokens.");
    return false;
}

// Helper to decode JWT and get the "mid" (Member ID)
bool WhiskerApi::_parseJwtForUserId(const String& token) {
    int firstDot = token.indexOf('.');
    int secondDot = token.indexOf('.', firstDot + 1);
    if (firstDot == -1 || secondDot == -1) return false;

    String payload = token.substring(firstDot + 1, secondDot);
    unsigned char decoded[payload.length()];
    size_t olen;
    mbedtls_base64_decode(decoded, payload.length(), &olen, (unsigned char*)payload.c_str(), payload.length() );
    //String decoded = base64::decode(payload);
    String decode_output(decoded, olen);
    JsonDocument doc;
    deserializeJson(doc, decode_output);
    
    if (doc["mid"]) {
        _user_id = doc["mid"].as<String>();
        return true;
    }
    return false;
}

// --- Main Data Fetch ---
bool WhiskerApi::fetchAllData(int limit) {
    if (_id_token == "") {
        _log("Not logged in.");
        return false;
    }
    
    _pets.clear();
    _records.clear();

    // 1. Fetch Pets
    _fetchPets();

    // 2. For each Pet, fetch their specific weight history (SmartScale data)
    // This answers your question: This is how we get data "separated per pet"
    for (const auto& pet : _pets) {
        _fetchPetWeightHistory(pet, limit);
    }

    // 3. Fetch Robot Cycles (Generic data like "Clean Cycle Completed")
    _fetchRobotsAndCycles(limit);

    // 4. Sort all records by timestamp (descending)
    std::sort(_records.begin(), _records.end(), [](const WhiskerRecord &a, const WhiskerRecord &b) {
        return a.timestamp > b.timestamp;
    });

    return true;
}

void WhiskerApi::_fetchPets() {
    // GraphQL to get pets
    String query = "query GetPetsByUser($userId: String!) { getPetsByUser(userId: $userId) { petId name weight } }";
    String vars = "{\"userId\":\"" + _user_id + "\"}";
    
    String response = _sendGraphQL(API_PET_GRAPHQL, query, vars);
    
    JsonDocument doc;
    deserializeJson(doc, response);
    JsonArray arr = doc["data"]["getPetsByUser"].as<JsonArray>();

    for (JsonObject obj : arr) {
        WhiskerPet p;
        p.id = obj["petId"].as<String>();
        p.name = obj["name"].as<String>();
        p.weight_lbs = obj["weight"].as<float>();
        _pets.push_back(p);
        _log("Found Pet: " + p.name);
    }
}

void WhiskerApi::_fetchPetWeightHistory(const WhiskerPet& pet, int limit) {
    // GraphQL to get specific history for a pet
    String query = "query GetWeightHistory($petId: String!, $limit: Int) { getWeightHistoryByPetId(petId: $petId, limit: $limit) { weight timestamp } }";
    String vars = "{\"petId\":\"" + pet.id + "\", \"limit\":" + String(limit) + "}";

    String response = _sendGraphQL(API_PET_GRAPHQL, query, vars);

    JsonDocument doc;
    deserializeJson(doc, response);
    JsonArray history = doc["data"]["getWeightHistoryByPetId"].as<JsonArray>();

    for (JsonObject item : history) {
        WhiskerRecord r;
        r.pet_id = pet.id;
        r.pet_name = pet.name;
        r.event_type = "Pet Weight Recorded"; // This event implies a visit
        r.device_model = "Litter-Robot 4";    // Only LR4 supports this
        r.weight_lbs = item["weight"].as<float>();
        
        // Parse ISO8601 Timestamp (e.g., 2024-04-17T12:35:42.000Z)
        const char* ts = item["timestamp"];
        struct tm tm;
        strptime(ts, "%Y-%m-%dT%H:%M:%S", &tm);
        r.timestamp = mktime(&tm);

        _records.push_back(r);
    }
}

void WhiskerApi::_fetchRobotsAndCycles(int limit) {
    // 1. Get LR4 Serial Numbers
    String query = "query GetLR4($userId: String!) { getLitterRobot4ByUser(userId: $userId) { serial name } }";
    String vars = "{\"userId\":\"" + _user_id + "\"}";
    String response = _sendGraphQL(API_LR4_GRAPHQL, query, vars);

    JsonDocument doc;
    deserializeJson(doc, response);
    JsonArray robots = doc["data"]["getLitterRobot4ByUser"].as<JsonArray>();

    // 2. For each robot, fetch generic activity
    for (JsonObject robot : robots) {
        String serial = robot["serial"].as<String>();
        String name = robot["name"].as<String>();

        String actQuery = "query GetActivity($serial: String!, $limit: Int) { getLitterRobot4Activity(serial: $serial, limit: $limit) { timestamp value actionValue } }";
        String actVars = "{\"serial\":\"" + serial + "\", \"limit\":" + String(limit) + "}";
        
        String actResp = _sendGraphQL(API_LR4_GRAPHQL, actQuery, actVars);
        JsonDocument actDoc;
        deserializeJson(actDoc, actResp);
        JsonArray activities = actDoc["data"]["getLitterRobot4Activity"].as<JsonArray>();

        for (JsonObject act : activities) {
            String val = act["value"].as<String>();
            
            // We skip "catWeight" here because we already fetched the clean, 
            // separated data from _fetchPetWeightHistory.
            // We only want machine events here.
            if (val == "catWeight") continue;

            WhiskerRecord r;
            r.device_serial = serial;
            r.device_model = "Litter-Robot 4";
            r.pet_id = ""; // Generic event
            r.pet_name = "";
            
            // Map common statuses
            if (val == "robotCycleStatusIdle") r.event_type = "Clean Cycle Complete";
            else if (val == "DFIFullFlagOn") r.event_type = "Drawer Full";
            else r.event_type = val;

            const char* ts = act["timestamp"];
            struct tm tm;
            strptime(ts, "%Y-%m-%d %H:%M:%S", &tm);
            r.timestamp = mktime(&tm);

            _records.push_back(r);
        }
    }
}

String WhiskerApi::_sendRequest(const char* url, const char* method, const String& payload, const char* contentType) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", contentType);
    if (_id_token.length() > 0) {
        http.addHeader("Authorization", "Bearer " + _id_token);
    }

    int httpCode = 0;
    if (String(method) == "POST") httpCode = http.POST(payload);
    else httpCode = http.GET();

    if (httpCode > 0) {
        String res = http.getString();
        http.end();
        return res;
    }
    http.end();
    return "{}";
}

String WhiskerApi::_sendGraphQL(const char* url, const String& query, const String& variables) {
    JsonDocument doc;
    doc["query"] = query;
    if (variables != "") {
        JsonDocument varDoc;
        deserializeJson(varDoc, variables);
        doc["variables"] = varDoc;
    }
    String payload;
    serializeJson(doc, payload);
    return _sendRequest(url, "POST", payload);
}

const std::vector<WhiskerPet>& WhiskerApi::getPets() const { return _pets; }
const std::vector<WhiskerRecord>& WhiskerApi::getRecords() const { return _records; }

std::vector<WhiskerRecord> WhiskerApi::getRecordsByPetId(String pet_id) const {
    std::vector<WhiskerRecord> filtered;
    for (const auto& rec : _records) {
        if (rec.pet_id == pet_id) {
            filtered.push_back(rec);
        }
    }
    return filtered;
}