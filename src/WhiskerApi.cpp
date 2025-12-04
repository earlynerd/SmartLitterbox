#include "WhiskerApi.h"
#include "mbedtls/base64.h"

// Whisker / AWS Constants
const char* COGNITO_ENDPOINT = "https://cognito-idp.us-east-1.amazonaws.com/";
const char* WHISKER_CLIENT_ID = "4552ujeu3aic90nf8qn53levmn"; // Public App Client ID
const char* API_LR4_GRAPHQL = "https://lr4.iothings.site/graphql";
const char* API_PET_GRAPHQL = "https://pet-profile.iothings.site/graphql";

WhiskerApi::WhiskerApi(const char* email, const char* password, const char* timezone) 
    : _email(email), _password(password), _timezone(timezone), _debug(false) {}

WhiskerApi::~WhiskerApi()
{

}
void WhiskerApi::setDebug(bool enabled) {
    _debug = enabled;
}

void WhiskerApi::_log(const String& msg) {
    if (_debug && Serial) Serial.println("[WhiskerApi] " + msg);
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
    http.setTimeout(10000); // 10s timeout

    int httpCode = http.POST(payload);
    
    if (httpCode != 200) {
        _log("Login Failed: " + String(httpCode));
        if (httpCode > 0) _log("Response: " + http.getString());
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

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
    
    size_t len = payload.length();
    size_t olen = 0;
    
    unsigned char* decoded = (unsigned char*)malloc(len + 1);
    if (!decoded) {
        _log("Memory allocation failed for JWT decode");
        return false;
    }

    int ret = mbedtls_base64_decode(decoded, len, &olen, (unsigned char*)payload.c_str(), len);
    
    if (ret != 0) {
        free(decoded);
        return false;
    }

    String decode_output((char*)decoded, olen);
    free(decoded); 

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, decode_output);
    
    if (!error && doc["mid"]) {
        _user_id = doc["mid"].as<String>();
        return true;
    }
    return false;
}

// --- Main Data Fetch ---
bool WhiskerApi::fetchAllData(int limit) {
    if (_id_token == "") {
        if (!login()) return false;
    }
    
    _pets.clear();
    _records.clear();
    _status_records.clear(); // Clear old status info

    //Fetch Pets
    _fetchPets();

    //For each Pet, fetch their specific weight history
    for (const auto& pet : _pets) {
        _fetchPetWeightHistory(pet, limit);
    }

    //Fetch Robot Cycles and Status
    _fetchRobotsAndCycles(limit);

    //Sort all records by timestamp (descending)
    std::sort(_records.begin(), _records.end(), [](const WhiskerRecord &a, const WhiskerRecord &b) {
        return a.timestamp > b.timestamp;
    });

    return true;
}

void WhiskerApi::_fetchPets() {
    String query = "query GetPetsByUser($userId: String!) { getPetsByUser(userId: $userId) { petId name weight } }";
    String vars = "{\"userId\":\"" + _user_id + "\"}";
    
    String response = _sendGraphQL(API_PET_GRAPHQL, query, vars);
    if (response == "{}") return;

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
    String query = "query GetWeightHistory($petId: String!, $limit: Int) { getWeightHistoryByPetId(petId: $petId, limit: $limit) { weight timestamp } }";
    String vars = "{\"petId\":\"" + pet.id + "\", \"limit\":" + String(limit) + "}";

    String response = _sendGraphQL(API_PET_GRAPHQL, query, vars);
    if (response == "{}") return;

    JsonDocument doc;
    deserializeJson(doc, response);
    JsonArray history = doc["data"]["getWeightHistoryByPetId"].as<JsonArray>();

    for (JsonObject item : history) {
        WhiskerRecord r;
        r.pet_id = pet.id;
        r.pet_name = pet.name;
        r.event_type = "Pet Weight Recorded"; 
        r.device_model = "Litter-Robot 4";    
        r.weight_lbs = item["weight"].as<float>();
        
        const char* ts = item["timestamp"];
        struct tm tm = {0};
        strptime(ts, "%Y-%m-%dT%H:%M:%S", &tm);
        r.timestamp = mktime(&tm);

        _records.push_back(r);
    }
}

void WhiskerApi::_fetchRobotsAndCycles(int limit) {
    //Fetch status fields (litterLevel, DFI, etc)
    String query = "query GetLR4($userId: String!) { getLitterRobot4ByUser(userId: $userId) { serial name litterLevel DFILevelPercent isDFIFull robotStatus } }";
    String vars = "{\"userId\":\"" + _user_id + "\"}";
    String response = _sendGraphQL(API_LR4_GRAPHQL, query, vars);
    if (response == "{}") return;

    JsonDocument doc;
    deserializeJson(doc, response);
    JsonArray robots = doc["data"]["getLitterRobot4ByUser"].as<JsonArray>();

    for (JsonObject robot : robots) {
        String serial = robot["serial"].as<String>();
        
        //CAPTURE CURRENT STATUS ---
        WhiskerStatus status;
        status.device_serial = serial;
        status.device_model = "Litter-Robot 4";
        status.timestamp = time(nullptr);
        status.robot_status = robot["robotStatus"].as<String>();
        
        // Waste Level (DFI)
        status.waste_level_percent = robot["DFILevelPercent"].as<int>();
        status.is_drawer_full = robot["isDFIFull"].as<bool>();

        // Litter Level Calculation (Raw mm to %)
        // Based on logic: 100 - (raw_mm - 440) / 0.6
        // 440mm = Full, ~500mm = Empty
        int rawLitter = robot["litterLevel"].as<int>();
        if (rawLitter > 0) {
            float calc = 100.0 - ((float)(rawLitter - 440) / 0.6);
            if (calc < 0) calc = 0;
            if (calc > 100) calc = 100;
            status.litter_level_percent = (int)round(calc);
        } else {
            status.litter_level_percent = 0; // Unknown/Error
        }

        _status_records.push_back(status);
        _log("Status fetched for " + status.device_serial + ": Litter " + String(status.litter_level_percent) + "%");

        // --- FETCH HISTORY ---
        String actQuery = "query GetActivity($serial: String!, $limit: Int) { getLitterRobot4Activity(serial: $serial, limit: $limit) { timestamp value actionValue } }";
        String actVars = "{\"serial\":\"" + serial + "\", \"limit\":" + String(limit) + "}";
        
        String actResp = _sendGraphQL(API_LR4_GRAPHQL, actQuery, actVars);
        if (actResp == "{}") continue;

        JsonDocument actDoc;
        deserializeJson(actDoc, actResp);
        JsonArray activities = actDoc["data"]["getLitterRobot4Activity"].as<JsonArray>();

        for (JsonObject act : activities) {
            String val = act["value"].as<String>();
            if (val == "catWeight") continue;

            WhiskerRecord r;
            r.device_serial = serial;
            r.device_model = "Litter-Robot 4";
            r.pet_id = ""; 
            r.pet_name = "";
            
            if (val == "robotCycleStatusIdle") r.event_type = "Clean Cycle Complete";
            else if (val == "DFIFullFlagOn") r.event_type = "Drawer Full";
            else r.event_type = val;

            const char* ts = act["timestamp"];
            struct tm tm = {0};
            strptime(ts, "%Y-%m-%d %H:%M:%S", &tm);
            r.timestamp = mktime(&tm);

            _records.push_back(r);
        }
    }
}

// Auto-retry on 401 Unauthorized
String WhiskerApi::_sendRequest(const char* url, const char* method, const String& payload, const char* contentType) {
    if (WiFi.status() != WL_CONNECTED) return "{}";

    HTTPClient http;
    http.setTimeout(15000); 
    http.setReuse(false); 

    http.begin(url);
    http.addHeader("Content-Type", contentType);
    if (_id_token.length() > 0) {
        http.addHeader("Authorization", "Bearer " + _id_token);
    }

    int httpCode = 0;
    if (String(method) == "POST") httpCode = http.POST(payload);
    else httpCode = http.GET();

    // Check for Token Expiry (401)
    if (httpCode == 401) {
        _log("Token expired. Attempting re-login...");
        http.end(); 
        
        if (login()) {
            _log("Re-login successful. Retrying request...");
            http.begin(url);
            http.addHeader("Content-Type", contentType);
            http.addHeader("Authorization", "Bearer " + _id_token);
            
            if (String(method) == "POST") httpCode = http.POST(payload);
            else httpCode = http.GET();
        } else {
            _log("Re-login failed.");
            return "{}";
        }
    }

    if (httpCode > 0) {
        String res = http.getString();
        http.end();
        return res;
    } else {
        _log("Request failed: " + http.errorToString(httpCode));
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