// Copyright Sam Bonifacio. All Rights Reserved.

#include "SettingsManager.h"

#include "Misc/AutoSettingsConfig.h"
#include "Scalability.h"
#include "Console/ConsoleUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "AutoSettingsLogs.h"
#include "AutoSettingsError.h"
#include "ConfigUtils.h"
#include "Utility/AutoSettingsStringUtils.h"
#include "Engine/Engine.h"

USettingsManager* USettingsManager::Get()
{
	return GEngine->GetEngineSubsystem<USettingsManager>();
}

FString USettingsManager::GetValue(FName Key, bool bPreferConfigValue)
{
	if (!UConsoleUtils::IsCVarRegistered(Key))
	{
		FAutoSettingsError::LogMissingCVar("Get Value", Key);
		return FString();
	}

	FString Value = bPreferConfigValue ? Get()->GetConfigValue(Key) : UConsoleUtils::GetStringCVar(Key);

	if (Value.IsEmpty())
	{
		// Fall back if preferred method is not available
		Value = bPreferConfigValue ? UConsoleUtils::GetStringCVar(Key) : Get()->GetConfigValue(Key);
	}

	return Value;
}

void USettingsManager::RegisterIntCVarSetting(FName Name, int32 DefaultValue, const FString& Help)
{
	if (UConsoleUtils::IsCVarRegistered(Name))
	{
		return;
	}

	const FString ConfigValue = Get()->GetConfigValue(Name);
	const int32 InitValue = ConfigValue.IsEmpty() ? DefaultValue : FCString::Atoi(*ConfigValue);

	UConsoleUtils::RegisterIntCVar(Name, InitValue, Help);
}

void USettingsManager::RegisterBoolCVarSetting(FName Name, bool DefaultValue, const FString& Help)
{
	if (UConsoleUtils::IsCVarRegistered(Name))
	{
		return;
	}

	const FString ConfigValue = Get()->GetConfigValue(Name);
	const bool InitValue = ConfigValue.IsEmpty() ? DefaultValue : FAutoSettingsStringUtils::IsTruthy(ConfigValue); // Also accept int too

	UConsoleUtils::RegisterBoolCVar(Name, InitValue, Help);
}

void USettingsManager::RegisterFloatCVarSetting(FName Name, float DefaultValue, const FString& Help)
{
	if (UConsoleUtils::IsCVarRegistered(Name))
	{
		return;
	}

	const FString ConfigValue = Get()->GetConfigValue(Name);
	const float InitValue = ConfigValue.IsEmpty() ? DefaultValue : FCString::Atof(*ConfigValue);

	UConsoleUtils::RegisterFloatCVar(Name, InitValue, Help);
}

void USettingsManager::RegisterStringCVarSetting(FName Name, const FString& DefaultValue, const FString& Help)
{
	if (UConsoleUtils::IsCVarRegistered(Name))
	{
		return;
	}

	const FString ConfigValue = Get()->GetConfigValue(Name);
	const FString InitValue = ConfigValue.IsEmpty() ? DefaultValue : ConfigValue;

	UConsoleUtils::RegisterStringCVar(Name, InitValue, Help);
}

void USettingsManager::RegisterIntCVarSettingWithCallback(FName Name, int32 DefaultValue, const FString& Help, FIntCVarChangedSignature ChangedCallback, bool CallbackImmediately)
{
	RegisterIntCVarSetting(Name, DefaultValue, Help);
	UCVarChangeListenerManager::AddIntCVarCallbackStatic(Name, ChangedCallback, CallbackImmediately);
}

void USettingsManager::RegisterBoolCVarSettingWithCallback(FName Name, bool DefaultValue, const FString& Help, FBoolCVarChangedSignature ChangedCallback, bool CallbackImmediately)
{
	RegisterBoolCVarSetting(Name, DefaultValue, Help);
	UCVarChangeListenerManager::AddBoolCVarCallbackStatic(Name, ChangedCallback, CallbackImmediately);
}

void USettingsManager::RegisterFloatCVarSettingWithCallback(FName Name, float DefaultValue, const FString& Help, FFloatCVarChangedSignature ChangedCallback, bool CallbackImmediately)
{
	RegisterFloatCVarSetting(Name, DefaultValue, Help);
	UCVarChangeListenerManager::AddFloatCVarCallbackStatic(Name, ChangedCallback, CallbackImmediately);
}

void USettingsManager::RegisterStringCVarSettingWithCallback(FName Name, const FString& DefaultValue, const FString& Help, FStringCVarChangedSignature ChangedCallback, bool CallbackImmediately)
{
	RegisterStringCVarSetting(Name, DefaultValue, Help);
	UCVarChangeListenerManager::AddStringCVarCallbackStatic(Name, ChangedCallback, CallbackImmediately);
}

void USettingsManager::AutoDetectSettingsStatic(int32 WorkScale, float CPUMultiplier, float GPUMultiplier)
{
	Get()->AutoDetectSettings(WorkScale, CPUMultiplier, GPUMultiplier);
}

void USettingsManager::SaveSettingStatic(FAutoSettingData SettingData)
{
	Get()->SaveSetting(SettingData, true);
}

void USettingsManager::ApplySettingStatic(FAutoSettingData SettingData)
{
	Get()->ApplySetting(SettingData);
}

USettingsManager::USettingsManager()
{
	const FString IniName = GetDefault<UAutoSettingsConfig>()->SettingsIniName;

	// Load Ini
	FConfigCacheIni::LoadGlobalIniFile(IniFilename, *IniName);

	if (!IniName.Contains("User"))
	{
		// Hack: As of UE 5.4, the engine will refuse to save config files without the string "User" in the name, unless they are explicitly flagged in the SectionsToSave section
		// We can use the bCanSaveAllSections flag to force the engine to save every section which restores the original functionality
		// This flag is only loaded with the config cache in the first place, so we need to first set it manually and then flush and reload the config
		// But we can't just set it in the ini file, because the engine will refuse to save it if it's not in the SectionsToSave section
		// So we have to add the SectionsToSave section to the SectionsToSave section first so that we can save the bCanSaveAllSections flag before reloading the config cache
		GConfig->SetString(TEXT("SectionsToSave"), TEXT("Section"), TEXT("SectionsToSave"), IniFilename);
		GConfig->SetString(TEXT("SectionsToSave"), TEXT("bCanSaveAllSections"), TEXT("true"), IniFilename);
		GConfig->Flush(false, IniFilename);
		FConfigCacheIni::LoadGlobalIniFile(IniFilename, *IniName, nullptr, true);
	}
}

void USettingsManager::Init()
{
	ApplySettingsFromConfig();
}

FString USettingsManager::GetConfigValue(FName Key) const
{
	return GetConfigValue(Key, GetSection());
}

bool USettingsManager::HasConfigValue(FName Key) const
{
	const FConfigSection* Section = GetSection();
	return Section && Section->Find(Key) != nullptr;
}

FString USettingsManager::GetConfigValue(FName Key, const FConfigSection* Section) const
{
	if (Section)
	{
		const FConfigValue* ConfigValue = Section->Find(Key);
		if (ConfigValue)
		{
			return ConfigValue->GetValue();
		}
	}

	return FString();
}

void USettingsManager::SaveSetting(FAutoSettingData SettingData, bool bApplySetting)
{
	if (!SettingData.Key.IsNone() && !SettingData.Value.IsEmpty())
	{
		if (bApplySetting)
		{
			ApplySetting(SettingData);
		}

		const FString Previous = GetConfigValue(*SettingData.Key.ToString());

		UE_LOG(LogAutoSettings, Log, TEXT("Saving setting %s with value: %s, previous: %s"), *SettingData.Key.ToString(), *SettingData.Value, *Previous);

		SetConfigValue(SettingData.Key, SettingData.Value);
		OnSettingSaved.Broadcast(SettingData);
	}
}

void USettingsManager::SetConfigValue(FName Key, FString Value)
{
	if (!Key.IsNone() && !Value.IsEmpty())
	{
		// Remove the existing value and compact the map, so that the new value gets added to the end of the config section rather than inserted back in it's original place
		// This is important because config are applied in the order they are saved when the engine is started, and we want to preserve the order that the user applied
		// in case there are any CVars that set other CVars, like the scalability ones
		GConfig->RemoveKey(*GetSectionName(), *Key.ToString(), IniFilename);
		const FConfigSection* Section = GConfig->GetSection(*GetSectionName(), true, IniFilename);
		const_cast<FConfigSection*>(Section)->CompactStable();
		GConfig->SetString(*GetSectionName(), *Key.ToString(), *Value, IniFilename);
		GConfig->Flush(false, IniFilename);
	}
}

void USettingsManager::ApplySetting(FAutoSettingData SettingData)
{
	if (!UConsoleUtils::IsCVarRegistered(SettingData.Key))
	{
		FAutoSettingsError::LogMissingCVar("Apply Setting", SettingData.Key);
		return;
	}

	const FString Previous = UConsoleUtils::GetStringCVar(SettingData.Key);

	UE_LOG(LogAutoSettings, Log, TEXT("Applying setting %s with value: %s, prevous: %s"), *SettingData.Key.ToString(), *SettingData.Value, *Previous);
	UConsoleUtils::SetStringCVar(SettingData.Key, SettingData.Value);
}

FString USettingsManager::GetSectionName()
{
	return GetDefault<UAutoSettingsConfig>()->SettingsSectionName;
}

void USettingsManager::ApplySettingsFromConfig()
{
	// Load the ini file
	GConfig->LoadFile(IniFilename);

	const FConfigSection* Section = GetSection();

	int32 SettingsLoaded = 0;

	UE_LOG(LogAutoSettings, Log, TEXT("Applying initial settings from config"));

	if (Section)
	{
		TArray<FName> Keys;
		Section->GetKeys(Keys);

		for (const FName Key : Keys)
		{
			const FString Value = GetConfigValue(Key, Section);
			if (UConsoleUtils::IsCVarRegistered(Key))
			{
				UE_LOG(LogAutoSettings, Log, TEXT("Applying initial setting %s with value: %s"), *Key.ToString(), *Value);
				UConsoleUtils::SetStringCVar(Key, Value);
				SettingsLoaded++;
			}
		}
	}

	UE_LOG(LogAutoSettings, Log, TEXT("Applied %i settings from config"), SettingsLoaded);
}

void USettingsManager::AutoDetectSettings(int32 WorkScale, float CPUMultiplier, float GPUMultiplier)
{
	const Scalability::FQualityLevels State = Scalability::BenchmarkQualityLevels(WorkScale, CPUMultiplier, GPUMultiplier);
	SetQualityLevels(State, true);

	// Save new scalability values to config
	// These are all the values that the Unreal scalability benchmark changes
	SaveSetting(FAutoSettingData("sg.ResolutionQuality", FString::FromInt(State.ResolutionQuality)), false);
	SaveSetting(FAutoSettingData("sg.ViewDistanceQuality", FString::FromInt(State.ViewDistanceQuality)), false);
	SaveSetting(FAutoSettingData("sg.AntiAliasingQuality", FString::FromInt(State.AntiAliasingQuality)), false);
	SaveSetting(FAutoSettingData("sg.ShadowQuality", FString::FromInt(State.ShadowQuality)), false);
	SaveSetting(FAutoSettingData("sg.PostProcessQuality", FString::FromInt(State.PostProcessQuality)), false);
	SaveSetting(FAutoSettingData("sg.TextureQuality", FString::FromInt(State.TextureQuality)), false);
	SaveSetting(FAutoSettingData("sg.EffectsQuality", FString::FromInt(State.EffectsQuality)), false);
	SaveSetting(FAutoSettingData("sg.FoliageQuality", FString::FromInt(State.FoliageQuality)), false);
}

const FConfigSection* USettingsManager::GetSection() const
{
	return GConfig->GetSection(*GetSectionName(), true, IniFilename);
}