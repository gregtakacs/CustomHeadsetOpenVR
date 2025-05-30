#include "ConfigLoader.h"
#include <thread>
#include <fstream>
#include <filesystem>
#include "nlohmann/json.hpp"
#include "../Driver/DriverLog.h"
#include "Windows.h"



using json = nlohmann::json;

std::string ConfigLoader::GetConfigFolder(){
	char* appdataPath = std::getenv("APPDATA");
	std::string configPath = appdataPath == nullptr ? "./" : (std::string(appdataPath) + "/CustomHeadset/");
	return configPath;
}

void ConfigLoader::ParseConfig(){
	// acquire driverConfig.configLock for the duration of this function
	// std::lock_guard<std::mutex> lock(driverConfig.configLock);
	// get file at APPDATA/Roaming/CustomHeadset/settings.json
	std::string configPath = GetConfigFolder() + "settings.json";
	std::ifstream configFile(configPath);
	if(!configFile.is_open()){
		if(!hasLoggedConfigFileNotFound){
			hasLoggedConfigFileNotFound = true;
			DriverLog("Config file not found at %s, using default settings.", configPath.c_str());
		}
		return;
	}
	DriverLog("Loading config file from %s", configPath.c_str());
	try{
		// parse with support for comments
		json data = json::parse(configFile, nullptr, true, true);
		Config newConfig = {};
		if(data["meganeX8K"].is_object()){
			json meganeX8KData = data["meganeX8K"];
			if(meganeX8KData["enable"].is_boolean()){
				newConfig.meganeX8K.enable = meganeX8KData["enable"].get<bool>();
			}
			if(meganeX8KData["ipd"].is_number()){
				newConfig.meganeX8K.ipd = meganeX8KData["ipd"].get<double>();
			}
			if(meganeX8KData["ipdOffset"].is_number()){
				newConfig.meganeX8K.ipdOffset = meganeX8KData["ipdOffset"].get<double>();
			}
			if(meganeX8KData["blackLevel"].is_number()){
				newConfig.meganeX8K.blackLevel = meganeX8KData["blackLevel"].get<double>();
			}
			if(meganeX8KData["distortionProfile"].is_string()){
				newConfig.meganeX8K.distortionProfile = meganeX8KData["distortionProfile"].get<std::string>();
			}
		}
		if(data["watchDistortionProfiles"].is_boolean()){
			newConfig.watchDistortionProfiles = data["watchDistortionProfiles"].get<bool>();
		}
		// write to global config
		driverConfigLock.lock();
		driverConfig = newConfig;
		driverConfigLock.unlock();
	}catch(const std::exception& e){
		DriverLog("Failed to parse config file: %s", e.what());
		return;
	}
}

DistortionProfileConfig ConfigLoader::ParseDistortionConfig(std::string name){
	std::string profilePath = GetConfigFolder() + "Distortion/" + name + ".json";
	std::ifstream configFile(profilePath);
	if(!configFile.is_open()){
		DriverLog("Distortion profile not found at %s", profilePath.c_str());
		return {};
	}
	DriverLog("Loading distortion profile from %s", profilePath.c_str());
	try{
		// parse with support for comments
		json data = json::parse(configFile, nullptr, true, true);
		DistortionProfileConfig profile = {};
		profile.modifiedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::filesystem::last_write_time(profilePath).time_since_epoch()).count() / 1000000000.0;
		profile.name = name;
		if(data["description"].is_string()){
			profile.description = data["description"].get<std::string>();
		}
		if(data["type"].is_string()){
			profile.type = data["type"].get<std::string>();
		}
		if(data["distortions"].is_array()){
			profile.distortions = data["distortions"].get<std::vector<double>>();
		}
		if(data["distortionsRed"].is_array()){
			profile.distortionsRed = data["distortionsRed"].get<std::vector<double>>();
		}
		if(data["distortionsBlue"].is_array()){
			profile.distortionsBlue = data["distortionsBlue"].get<std::vector<double>>();
		}
		return profile;
	}catch(const std::exception& e){
		DriverLog("Failed to parse distortion profile: %s", e.what());
		return {};
	}
}

void ConfigLoader::WatcherThread(){
	// watch for changes in the config file directory
	std::string configPath = GetConfigFolder();
	HANDLE hDir = CreateFileA(configPath.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if(hDir == INVALID_HANDLE_VALUE){
		DriverLog("Failed to open config directory for watching: %d", GetLastError());
		return;
	}
	while(started){
		DWORD bytesReturned;
		char buffer[1024];
		FILE_NOTIFY_INFORMATION* pNotify;
		BOOL success = ReadDirectoryChangesW(hDir, buffer, sizeof(buffer), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME, &bytesReturned, NULL, NULL);
		if(!success){
			DriverLog("Failed to read directory changes: %d", GetLastError());
			break;
		}
		pNotify = (FILE_NOTIFY_INFORMATION*)buffer;
		do{
			std::wstring fileName(pNotify->FileName, pNotify->FileNameLength / sizeof(wchar_t));
			if(fileName == L"settings.json" && (pNotify->Action == FILE_ACTION_MODIFIED || pNotify->Action == FILE_ACTION_ADDED || pNotify->Action == FILE_ACTION_RENAMED_NEW_NAME)){
				DriverLog("Config file changed, reloading...");
				ParseConfig();
			}
			pNotify = (FILE_NOTIFY_INFORMATION*)((char*)pNotify + pNotify->NextEntryOffset);
		}while(pNotify->NextEntryOffset != 0);
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}

void ConfigLoader::WatcherThreadDistortions(){
	std::string configPath = GetConfigFolder() + "Distortion/";
	HANDLE hDir = CreateFileA(configPath.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if(hDir == INVALID_HANDLE_VALUE){
		DriverLog("Failed to open distortion directory for watching: %d", GetLastError());
		return;
	}
	while(started){
		DWORD bytesReturned;
		char buffer[1024];
		FILE_NOTIFY_INFORMATION* pNotify;
		BOOL success = ReadDirectoryChangesW(hDir, buffer, sizeof(buffer), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME, &bytesReturned, NULL, NULL);
		if(!success){
			DriverLog("Failed to read directory changes: %d", GetLastError());
			break;
		}
		pNotify = (FILE_NOTIFY_INFORMATION*)buffer;
		do{
			std::wstring fileName(pNotify->FileName, pNotify->FileNameLength / sizeof(wchar_t));
			if(fileName.find(L".json") != std::wstring::npos && (pNotify->Action == FILE_ACTION_MODIFIED || pNotify->Action == FILE_ACTION_ADDED || pNotify->Action == FILE_ACTION_RENAMED_NEW_NAME)){
				DriverLog("Distortion profile changed, reloading...");
				ParseConfig();
			}
		}while(pNotify->NextEntryOffset != 0);
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}
				
// only define settings that most users will change and are unlikely to have their default changed
// settings not defined here will easily be able to have their defaults changed in the future for everyone
std::string defaultConfig = R"({
	"meganeX8K": {
		"enable": true,
		"ipd": 63.0,
		"ipdOffset": 0.0,
		"distortionProfile": "MeganeX8K Default"
	}
})";


void ConfigLoader::Start(){
	if(started){
		return;
	}
	started = true;
	
	try{
		// create directory
		std::filesystem::create_directories(GetConfigFolder());
		
		// create default config if it doesn't exist
		std::string configPath = GetConfigFolder() + "settings.json";
		if(!std::filesystem::exists(configPath)){
			std::ofstream configFile(configPath);
			configFile << defaultConfig;
			configFile.close();
		}
	}catch(const std::exception& e){
		DriverLog("Failed to create settings.json %s", e.what());
	}
	
	// load config for the first time
	ParseConfig();
	
	try{	
		// start watcher thread
		std::thread watcher(&ConfigLoader::WatcherThread, this);
		// detach the thread to run forever
		watcher.detach();
		
		// create distortion profiles directory and watch if configured
		std::filesystem::create_directories(GetConfigFolder() + "Distortion/");
		if(driverConfig.watchDistortionProfiles){
			std::thread distortionWatcher(&ConfigLoader::WatcherThreadDistortions, this);
			distortionWatcher.detach();
		}
	}catch(const std::exception& e){
		DriverLog("Failed to start config watcher: %s", e.what());
	}
}


ConfigLoader driverConfigLoader = {};