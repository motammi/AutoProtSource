#define _CRT_SECURE_NO_WARNINGS

#include <SQLiteCpp/Database.h>

#include <API/ARK/Ark.h>
#include <IApiUtils.h>
#include <API/UE/Math/ColorList.h>

#include "../../json.hpp"

#include "ProtDatabase.h"
#include <fstream>
#include <iostream>
#include "AutoProt.h"

#pragma comment(lib, "ArkApi.lib")


// float TakeDamage(float Damage, FDamageEvent * DamageEvent, AController * EventInstigator, AActor * DamageCauser) { return NativeCall<float, float, FDamageEvent *, AController *, AActor *>(this, "APrimalTargetableActor.TakeDamage", Damage, DamageEvent, EventInstigator, DamageCauser); }
DECLARE_HOOK(APrimalStructure_TakeDamage, float, APrimalStructure*, float, FDamageEvent*, AController*, AActor*);

DECLARE_HOOK(AShooterGameMode_HandleNewPlayer, bool, AShooterGameMode*, AShooterPlayerController*, UPrimalPlayerData*,AShooterCharacter*, bool);

// unsigned __int64 AddNewTribe(AShooterPlayerState * PlayerOwner, FString * TribeName, FTribeGovernment * TribeGovernment) { return NativeCall<unsigned __int64, AShooterPlayerState *, FString *, FTribeGovernment *>(this, "AShooterGameMode.AddNewTribe", PlayerOwner, TribeName, TribeGovernment); }
DECLARE_HOOK(AShooterGameMode_AddNewTribe, uint64, AShooterGameMode *, AShooterPlayerState * PlayerOwner, FString * TribeName, FTribeGovernment * TribeGovernment);

DECLARE_HOOK(ServerRequestLeaveTribe_Implementation, void, AShooterPlayerState*);

// bool AddToTribe(FTribeData * MyNewTribe, bool bMergeTribe, bool bForce, bool bIsFromInvite, APlayerController * InviterPC) { return NativeCall<bool, FTribeData *, bool, bool, bool, APlayerController *>(this, "AShooterPlayerState.AddToTribe", MyNewTribe, bMergeTribe, bForce, bIsFromInvite, InviterPC); }
DECLARE_HOOK(AddToTribe, bool, AShooterPlayerState*, FTribeData * MyNewTribe, bool bMergeTribe, bool bForce, bool bIsFromInvite, APlayerController * InviterPC);

// DECLARE_HOOK FOR WHEN A PLAYER JOINS HE GETS THE MESSAGE DISPLAYED [Your structures protected for 48hours]



nlohmann::json AutoProt::config;
std::map<uint64, uint64> messages;

void replace_string_in_place(std::string& subject, const std::string& search,
	const std::string& replace) {
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos) {
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
}

std::string get_message(const std::string& key) {
	return ArkApi::Tools::ConvertToAnsiStr(ArkApi::Tools::Utf8Decode((AutoProt::config["Messages"].value(key, "Unable to find key \"" + key + "\" in config file!"))));
}

FString get_formatted_message(const std::string& key) {
	return FString(ArkApi::Tools::Utf8Decode(AutoProt::config["Messages"].value(key, "Unable to find key \"" + key + "\" in config file!")));
}


bool Hook_AddToTribe(AShooterPlayerState* player, FTribeData * MyNewTribe, bool bMergeTribe, bool bForce, bool bIsFromInvite, APlayerController * InviterPC) {
	if (bIsFromInvite) {
		int tribeId = MyNewTribe->TribeIDField();
		uint64 steamId = ArkApi::GetApiUtils().GetSteamIdFromController(player->GetOwnerController());
		ProtDatabase::SetPlayerTribe(steamId, tribeId);
	}
	return 	AddToTribe_original(player, MyNewTribe, bMergeTribe, bForce, bIsFromInvite, InviterPC);;
}

void Hook_ServerRequestLeaveTribe_Implementation(AShooterPlayerState* player) {
	ServerRequestLeaveTribe_Implementation_original(player);
	uint64 steamId = ArkApi::GetApiUtils().GetSteamIdFromController(player->GetOwnerController());
	ProtDatabase::SetPlayerTribe(steamId, 0);
}

float Hook_APrimalStructure_TakeDamage(APrimalStructure* _this, float Damage, FDamageEvent* DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (DamageCauser) // DamageCauser != NULL
	{
		FString descr;
		DamageCauser->GetHumanReadableName(&descr);

		if (descr.StartsWith("Cannon"))
		{
			return 0;
		}
	}
		int ownerPlayerId = _this->OwningPlayerIDField();

		if (ownerPlayerId == 0) // Owner is tribe
		{
			int structureTeamId = _this->TargetingTeamField();
			FTribeData* structure_tribe_data = static_cast<FTribeData*>(FMemory::Malloc(0x128 + 0x28));
			RtlSecureZeroMemory(structure_tribe_data, 0x128 + 0x28);
			auto tribe = ArkApi::GetApiUtils().GetShooterGameMode()->GetOrLoadTribeData(structureTeamId, structure_tribe_data);
			int targetTribeId = structure_tribe_data->TribeIDField();

			FMemory::Free(structure_tribe_data);

			if (EventInstigator && EventInstigator->IsA(APlayerController::StaticClass())) {
				AShooterPlayerController* player = (AShooterPlayerController*)EventInstigator;
				int hitter = player->TargetingTeamField();

				if (hitter == targetTribeId) {
					return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
				}

				uint64 steamId = ArkApi::GetApiUtils().GetSteamIdFromController(player);

				if (ProtDatabase::getEndTime(steamId) > std::time(0)) {
					ArkApi::GetApiUtils().SendNotification(player, { 1,0.549f,0,1 }, 1.6f, 4, nullptr, "You're under protection you can't attack people");
					return 0;
				}

				else {

					// - 50202020 hours of protection left
					uint64 endTime = ProtDatabase::getTribeEndTime(structureTeamId);
					if (endTime < std::time(0)) {
						return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
					}

					__int64 remainingtime = endTime - std::time(0);

					// If the target is not in the database
					if (remainingtime < 0) {
						return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
					}

					// the player has recevied a message already
					if (messages.count(steamId) != 0) {
						uint64 delay = AutoProt::config["MessageDelay"];
						if (messages[steamId] - std::time(0) > delay) {
							std::string message = get_message("DamageMessage");

							int hours = -1;
							int minutes = -1;

							if (message.find("%hours%")) {
								hours = remainingtime / 3600;
								replace_string_in_place(message, "%hours%", std::to_string(hours));
							}
							if (message.find("%minutes%")) {
								minutes = (remainingtime % 3600) / 60;
								replace_string_in_place(message, "%minutes%", std::to_string(minutes));
							}
							ArkApi::GetApiUtils().SendNotification(player, { 1,0.549f,0,1 }, 1.6f, 4, nullptr, *FString(message));
							messages[steamId] = std::time(0);
						}
					}
					else {
						std::string message = get_message("DamageMessage");

						int hours = -1;
						int minutes = -1;

						if (message.find("%hours%")) {
							hours = remainingtime / 3600;
							replace_string_in_place(message, "%hours%", std::to_string(hours));
						}
						if (message.find("%minutes%")) {
							minutes = (remainingtime % 3600) / 60;
							replace_string_in_place(message, "%minutes%", std::to_string(minutes));
						}
						ArkApi::GetApiUtils().SendNotification(player, { 1,0.549f,0,1 }, 1.6f, 4, nullptr, *FString(message));
						messages[steamId] = std::time(0);
					}
					return 0;
				}
			}

			if (ProtDatabase::getEndTime(targetTribeId) >= std::time(0)){
				return 0;
			}

			if (ProtDatabase::getTribeEndTime(targetTribeId) >= std::time(0)) {
				return 0;
			}
			else {
				return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
			}
		}
		else
		{
			auto playerlist = ArkApi::GetApiUtils().GetShooterGameMode()->SteamIdsField();
			uint64 targetSteamId;
			for (auto& Elem : playerlist) {
				if (Elem.Value == ownerPlayerId) targetSteamId = Elem.Key;
			}
	
			if (EventInstigator && EventInstigator->IsA(APlayerController::StaticClass())) {
				AShooterPlayerController* player = (AShooterPlayerController*)EventInstigator;
				uint64 steamId = ArkApi::GetApiUtils().GetSteamIdFromController(player);

				int hitter = player->TargetingTeamField();
				int target = _this->TargetingTeamField();

				if (hitter == target && hitter != 0) return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);

				if (ProtDatabase::getEndTime(steamId) > std::time(0)) {
					ArkApi::GetApiUtils().SendNotification(player, { 1,0.549f,0,1 }, 1.6f, 4, nullptr, *FString("You're under protection you can't attack people"));
					return 0;
				}

				if (ProtDatabase::getEndTime(targetSteamId) < std::time(0) || targetSteamId == steamId) {
					return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
				}
				else {
					// Hey its a protected player!!!
					if (EventInstigator && EventInstigator->IsA(APlayerController::StaticClass())) {
						AShooterPlayerController* player = (AShooterPlayerController*)EventInstigator;

						uint64 endTime = ProtDatabase::getEndTime(targetSteamId);
						if (endTime < std::time(0)) {
							return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
						}

						__int64 remainingtime = endTime - std::time(0);

						// If the target is not in the database
						if (remainingtime < 0) {
							return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
						}

						// the player has recevied a message already
						if (messages.count(steamId) != 0) {
							int delay = AutoProt::config["MessageDelay"];
							if (messages[steamId] - std::time(0) > delay) {
								std::string message = get_message("DamageMessage");

								int hours = -1;
								int minutes = -1;

								if (message.find("%hours%")) {
									hours = remainingtime / 3600;
									replace_string_in_place(message, "%hours%", std::to_string(hours));
								}
								if (message.find("%minutes%")) {
									minutes = (remainingtime % 3600) / 60;
									replace_string_in_place(message, "%minutes%", std::to_string(minutes));
								}
								ArkApi::GetApiUtils().SendNotification(player, { 1,0.549f,0,1 }, 1.6f, 4, nullptr, *FString(message));
								messages[steamId] = std::time(0);
							}
						}
						else {
							std::string message = get_message("DamageMessage");

							int hours = -1;
							int minutes = -1;

							if (message.find("%hours%")) {
								hours = remainingtime / 3600;
								replace_string_in_place(message, "%hours%", std::to_string(hours));
							}
							if (message.find("%minutes%")) {
								minutes = (remainingtime % 3600) / 60;
								replace_string_in_place(message, "%minutes%", std::to_string(minutes));
							}
							ArkApi::GetApiUtils().SendNotification(player, { 1,0.549f,0,1 }, 1.6f, 4, nullptr, *FString(message));
							messages[steamId] = std::time(0);
						}
					}
					return 0;
				}
			}
			if (ProtDatabase::getEndTime(targetSteamId) <= std::time(0)) {
				return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
			}
			else {
				return 0;
			}
		}
	return APrimalStructure_TakeDamage_original(_this, Damage, DamageEvent, EventInstigator, DamageCauser);
}

bool Hook_AShooterGameMode_HandleNewPlayer(AShooterGameMode* _this, AShooterPlayerController* new_player, UPrimalPlayerData* player_data, AShooterCharacter* player_character,	bool is_from_login)
{
	const uint64 steam_id = ArkApi::IApiUtils::GetSteamIdFromController((AController *) new_player);
	FString playername;
	new_player->PlayerStateField()->GetHumanReadableName(&playername);
	ProtDatabase::AddPlayerIfNotPresent(playername.ToString() ,steam_id, 0, (std::time(0) + (uint64)AutoProt::config["ProtectionTime"]*3600));
	return AShooterGameMode_HandleNewPlayer_original(_this, new_player, player_data, player_character, is_from_login);
}

uint64 Hook_AShooterGameMode_AddNewTribe(AShooterGameMode * _this, AShooterPlayerState * PlayerOwner, FString * TribeName, FTribeGovernment * TribeGovernment) {
	auto result = AShooterGameMode_AddNewTribe_original(_this, PlayerOwner, TribeName, TribeGovernment);
	ProtDatabase::SetPlayerTribe(ArkApi::GetApiUtils().GetSteamIdFromController(PlayerOwner->GetOwnerController()), result);
	return result;
}

void send_remaining_time(AShooterPlayerController* player_controller, FString*, EChatSendMode::Type) {

	if (ArkApi::IApiUtils::IsPlayerDead(player_controller))	return;

	// Query remaining protection time from database
	uint64 endTime = ProtDatabase::getEndTime(ArkApi::GetApiUtils().GetSteamIdFromController((AController *)player_controller));
	__int64 remainingTime = endTime - std::time(0);

	if (remainingTime > 0) {
		int hours = remainingTime / 3600;
		int minutes = (remainingTime % 3600) / 60;

		std::string message = get_message("CurrentProtectionTime");
		replace_string_in_place(message, "%hours%", std::to_string(hours));
		replace_string_in_place(message, "%minutes%", std::to_string(minutes));

		ArkApi::GetApiUtils().SendChatMessage(player_controller, *get_formatted_message("prefix"), *FString(message));
	}
	else {
		ArkApi::GetApiUtils().SendChatMessage(player_controller, *get_formatted_message("prefix"), "Your protection time is already over");
	}
}

void removeProtection(AShooterPlayerController* player_controller, FString*, EChatSendMode::Type) {

	if (ArkApi::IApiUtils::IsPlayerDead(player_controller))	return;

	uint64 steamId = ArkApi::GetApiUtils().GetSteamIdFromController(player_controller);
	// Query remaining protection time from database
	uint64 endTime = ProtDatabase::getEndTime(steamId);
	__int64 remainingTime = endTime - std::time(0);

	if (remainingTime > 0) {
		bool wholeTribe = ProtDatabase::getTribeId(steamId) != 0;
		ProtDatabase::SetTime(steamId, wholeTribe, 0);
		std::string message = get_message("ProtectionRemoved");
		ArkApi::GetApiUtils().SendChatMessage(player_controller, *get_formatted_message("prefix"), *FString(message));
	}
	else {
		ArkApi::GetApiUtils().SendChatMessage(player_controller, *get_formatted_message("prefix"), "Your protection time is already over");
	}
}

void set_protection(APlayerController* player_controller, FString* message, bool) {

	AShooterPlayerController* shooter_controller = static_cast<AShooterPlayerController*>(player_controller);

	TArray<FString> parsed;
	message->ParseIntoArray(parsed, L" ", true);

	if (parsed.IsValidIndex(2)) {
		if (parsed[1].ToString().find_first_not_of("0123456789") != std::string::npos || parsed[2].ToString().find_first_not_of("0123456789") != std::string::npos) {
			ArkApi::GetApiUtils().SendChatMessage(shooter_controller, *get_formatted_message("prefix"), *FString("Please use SetProt <steamId> <hours>"));
			return;
		}
	
		uint64 steamId = std::stoull(parsed[1].ToString(), 0, 10);
		uint64 time = std::stoull(parsed[2].ToString(), 0, 10);
		if (ProtDatabase::PlayerExists(steamId)) {
			bool wholeTribe = ProtDatabase::getTribeId(steamId) != 0;

			ProtDatabase::SetTime(steamId, wholeTribe, time);
			ArkApi::GetApiUtils().SendChatMessage(shooter_controller, *get_formatted_message("prefix"), *FString("Success!"));
		}
		else {
			ArkApi::GetApiUtils().SendChatMessage(shooter_controller, *get_formatted_message("prefix"), *FString("Error while setting protection Time - please watch the console!"));
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, "Tried to set protection time for a non existing player!");
		}
	}
	else {
		ArkApi::GetApiUtils().SendChatMessage(shooter_controller, *get_formatted_message("prefix"), *FString("Please use SetProt <steamId> <hours>"));
	}
}

void load_config()
{
	const std::string config_path = ArkApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/AutoProt/config.json";
	std::ifstream file(config_path);
	if (!file.is_open())
		throw std::runtime_error("Can't open config.json");

	file >> AutoProt::config;
	file.close();
}

void Load()
{
	Log::Get().Init("AutoProt");
	load_config();
	ProtDatabase::InitDatabase();

	ArkApi::GetHooks().SetHook("APrimalStructure.TakeDamage", &Hook_APrimalStructure_TakeDamage,
		&APrimalStructure_TakeDamage_original);
	ArkApi::GetHooks().SetHook("AShooterGameMode.HandleNewPlayer_Implementation", &Hook_AShooterGameMode_HandleNewPlayer, &AShooterGameMode_HandleNewPlayer_original);

	ArkApi::GetHooks().SetHook("AShooterGameMode.AddNewTribe", &Hook_AShooterGameMode_AddNewTribe, &AShooterGameMode_AddNewTribe_original);

	ArkApi::GetHooks().SetHook("AShooterPlayerState.ServerRequestLeaveTribe_Implementation", &Hook_ServerRequestLeaveTribe_Implementation, &ServerRequestLeaveTribe_Implementation_original);
	ArkApi::GetHooks().SetHook("AShooterPlayerState.AddToTribe", &Hook_AddToTribe, &AddToTribe_original);

	ArkApi::GetCommands().AddChatCommand("/AutoProt", &send_remaining_time);
	ArkApi::GetCommands().AddChatCommand("/RemoveProt", &removeProtection);
	ArkApi::GetCommands().AddConsoleCommand("SetProt", &set_protection);
}

void Unload() {
	ArkApi::GetCommands().RemoveChatCommand("/AutoProt");
	ArkApi::GetCommands().RemoveChatCommand("/RemoveProt");
	ArkApi::GetCommands().RemoveConsoleCommand("SetProt");

	ArkApi::GetHooks().DisableHook("APrimalStructure.TakeDamage", &Hook_APrimalStructure_TakeDamage);
	ArkApi::GetHooks().DisableHook("AShooterGameMode.HandleNewPlayer_Implementation", &Hook_AShooterGameMode_HandleNewPlayer);
	ArkApi::GetHooks().DisableHook("AShooterGameMode.AddNewTribe", &Hook_AShooterGameMode_AddNewTribe);
	ArkApi::GetHooks().DisableHook("AShooterPlayerState.ServerRequestLeaveTribe_Implementation", &Hook_ServerRequestLeaveTribe_Implementation);
	ArkApi::GetHooks().DisableHook("AShooterPlayerState.AddToTribe", &Hook_AddToTribe);

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		Load();
		break;
	case DLL_PROCESS_DETACH:
		Unload();
		break;
	}
	return TRUE;
}