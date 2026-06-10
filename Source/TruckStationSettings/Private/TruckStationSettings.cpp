// Copyright Epic Games, Inc. All Rights Reserved.

#include "TruckStationSettings.h"

#include "UObject/UnrealType.h"
#include "Patching/NativeHookManager.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "FGInventoryComponent.h"

#define LOCTEXT_NAMESPACE "FTruckStationSettingsModule"

constexpr int32 NOT_FOUND = -1;

DEFINE_LOG_CATEGORY(TruckStationSettings);

namespace {
	enum class UnloadHookStatus {
		STATUS_RESUME,
		STATUS_CANCEL,
		STATUS_OVERRIDE
	};

	struct UnloadHookResult {
		int32 AddedItems;
		UnloadHookStatus Status;
	};

} // namespace

class TSSFGHooks {
public:
	static void RegisterHooks();
	static UnloadHookResult ProgrammableUnloadDockedInventoryOverride(const AFGBuildableDockingStation* self, UFGInventoryComponent* dockedInventory);
};

void TSSFGHooks::RegisterHooks() {
	if (!WITH_EDITOR) {
		SUBSCRIBE_METHOD(AFGBuildableDockingStation::Factory_LoadUnloadDockedInventory, [](auto& scope, const AFGBuildableDockingStation* self, UFGInventoryComponent* dockedInventory) {
			static const FString PROGRAMMABLE_TRUCK_STATION_CNAME = TEXT("Build_ProgrammableTruckStation_C");

			if (self == nullptr) {
				UE_LOG(TruckStationSettings, Error, TEXT("Got nullptr as self!"));
				scope(self, dockedInventory);
				return;
			}
			
			FString dockStationClassName = self->GetClass()->GetName();
			UE_LOG(TruckStationSettings, Verbose, TEXT("Got instance of %s"), *dockStationClassName);
			if (!dockStationClassName.Equals(PROGRAMMABLE_TRUCK_STATION_CNAME)) {
				scope(self, dockedInventory);
				return;
			}

			if (self->GetIsInLoadMode()) {
				// Load mode, use default implementation.
				scope(self, dockedInventory);
				return;
			}
			// Unload mode, patch unloading here.
			UE_LOG(TruckStationSettings, Verbose, TEXT("Patched callback"));
			UnloadHookResult hookResult = ProgrammableUnloadDockedInventoryOverride(self, dockedInventory);
			switch (hookResult.Status) {
				case UnloadHookStatus::STATUS_CANCEL:
					scope.Override(0);
					break;
				case UnloadHookStatus::STATUS_OVERRIDE:
					scope.Override(hookResult.AddedItems);
					break;
				case UnloadHookStatus::STATUS_RESUME:
				default:
					scope(self, dockedInventory);
			}
		});
	}
}

UnloadHookResult TSSFGHooks::ProgrammableUnloadDockedInventoryOverride(const AFGBuildableDockingStation* self, UFGInventoryComponent* dockedInventory) {
	UE_LOG(TruckStationSettings, Verbose, TEXT("Applying filtering of smart truck station"));
	FProperty* filterItemProp = self->GetClass()->FindPropertyByName(FName("FilterItem"));
	FArrayProperty* filterItemArrayProp = CastField<FArrayProperty>(filterItemProp);
	if (filterItemArrayProp == nullptr) {
		UE_LOG(TruckStationSettings, Error, TEXT("FilterItem set variable is not found!"));
		return { .AddedItems = 0, .Status = UnloadHookStatus::STATUS_RESUME };
	}

	FScriptArrayHelper itemsSet(filterItemArrayProp, filterItemArrayProp->ContainerPtrToValuePtr<void>(self));
	if (itemsSet.Num() == 0) {
		UE_LOG(TruckStationSettings, Verbose, TEXT("Filters are empty"));
		return { .AddedItems = 0, .Status = UnloadHookStatus::STATUS_RESUME };
	}
	int32 addedTotal = 0;
	for (int32 i = 0; i < itemsSet.Num(); ++i) {
		uint8* valuePtr = itemsSet.GetRawPtr(i);
		FObjectPropertyBase* property = CastField<FObjectPropertyBase>(filterItemArrayProp->Inner);
		if (property == nullptr) {
			UE_LOG(TruckStationSettings, Verbose, TEXT("property is empty"));
			return { .AddedItems = 0, .Status = UnloadHookStatus::STATUS_RESUME };
		}
		UObject* filterItemDesc = property->GetObjectPropertyValue(valuePtr);
		UClass* filterItemDescClass = Cast<UClass>(filterItemDesc);
		if (filterItemDescClass == nullptr) {
			UE_LOG(TruckStationSettings, Verbose, TEXT("FilterItem is empty"));
			continue;
		}

		for (int32 itemDescIndex = dockedInventory->FindFirstIndexWithItemType(filterItemDescClass, 0);
				itemDescIndex != NOT_FOUND;
				itemDescIndex = dockedInventory->FindFirstIndexWithItemType(filterItemDescClass, itemDescIndex + 1)) {
			UE_LOG(TruckStationSettings, Verbose, TEXT("Got item desc of %d"), itemDescIndex);

			FInventoryStack stack;
			if (!dockedInventory->GetStackFromIndex(itemDescIndex, stack)) {
				UE_LOG(TruckStationSettings, Error, TEXT("Invalid index requested from docked inventory!"));
				return { .AddedItems = 0, .Status = UnloadHookStatus::STATUS_CANCEL };
			}
			UE_LOG(TruckStationSettings, Verbose, TEXT("Got stack of items %d"), stack.NumItems);
			int32 addedInStack = self->GetInventory()->AddStack(stack, /* allowPartialAdd= */true);
			if (addedInStack == 0) {
				UE_LOG(TruckStationSettings, Verbose, TEXT("Can't add anymore, assuming full"));
				return { .AddedItems = addedTotal, .Status = UnloadHookStatus::STATUS_OVERRIDE };
			}
			addedTotal += addedInStack;
			UE_LOG(TruckStationSettings, Verbose, TEXT("Added %d items to Docking station inventory"), addedTotal);
			dockedInventory->RemoveFromIndex(itemDescIndex, addedInStack);
		}
	}

	return { .AddedItems = addedTotal, .Status = UnloadHookStatus::STATUS_OVERRIDE };
}

void FTruckStationSettingsModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	TSSFGHooks::RegisterHooks();
}

void FTruckStationSettingsModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTruckStationSettingsModule, TruckStationSettings)