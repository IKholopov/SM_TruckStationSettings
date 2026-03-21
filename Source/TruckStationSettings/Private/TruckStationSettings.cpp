// Copyright Epic Games, Inc. All Rights Reserved.

#include "TruckStationSettings.h"
#include "Patching/NativeHookManager.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "FGInventoryComponent.h"

#define LOCTEXT_NAMESPACE "FTruckStationSettingsModule"

constexpr int32 NOT_FOUND = -1;
DEFINE_LOG_CATEGORY(TruckStationSettings);

class TSSFGHooks {
public:
	static void RegisterHooks();
};

void TSSFGHooks::RegisterHooks() {
	if (!WITH_EDITOR) {
		SUBSCRIBE_METHOD(AFGBuildableDockingStation::Factory_UnloadDockedInventory, [](auto& scope, AFGBuildableDockingStation* self, UFGInventoryComponent* dockedInventory) {
			static const FString PROGRAMMABLE_TRUCK_STATION_CNAME = TEXT("Build_ProgrammableTruckStation_C");

			if (self == nullptr) {
				UE_LOG(TruckStationSettings, Error, TEXT("Got nullptr as self!"));
				scope(self, dockedInventory);
				return;
			}
			
			FString dockStationClassName = self->GetClass()->GetName();
			UE_LOG(TruckStationSettings, Verbose, TEXT("Got instance of %s"), *dockStationClassName);
			if (dockStationClassName.Equals(PROGRAMMABLE_TRUCK_STATION_CNAME)) {
				UE_LOG(TruckStationSettings, Verbose, TEXT("Applying filtering of smart truck station"));
				FProperty* filterItemProp = self->GetClass()->FindPropertyByName(FName("FilterItem"));
				FClassProperty* filterItemClassProp = CastField<FClassProperty>(filterItemProp);
				if (filterItemClassProp == nullptr) {
					UE_LOG(TruckStationSettings, Error, TEXT("FilterItem variable is not found!"));
					scope(self, dockedInventory);
					return;
				}

				const TObjectPtr<UObject>& filterItemDesc = filterItemClassProp->GetPropertyValue_InContainer(self);
				if (filterItemDesc.IsNull()) {
					UE_LOG(TruckStationSettings, Verbose, TEXT("FilterItem is empty"));
					scope(self, dockedInventory);
					return;
				}
				UClass* filterItemDescClass = Cast<UClass>(filterItemDesc.Get());
				int32 itemDescIndex = dockedInventory->FindFirstIndexWithItemType(filterItemDescClass, 0);
				UE_LOG(TruckStationSettings, Verbose, TEXT("Got item desc of %d"), itemDescIndex);
				if (itemDescIndex == NOT_FOUND) {
					scope.Cancel();
					self->LoadUnloadVehicleComplete();
					return;
				}

				FInventoryStack stack;
				if (!dockedInventory->GetStackFromIndex(itemDescIndex, stack)) {
					UE_LOG(TruckStationSettings, Error, TEXT("Invalid index requested from docked inventory!"));
					scope.Cancel();
					return;
				}
				UE_LOG(TruckStationSettings, Verbose, TEXT("Got stack of items %d"), stack.NumItems);
				int32 added = self->GetInventory()->AddStack(stack, /* allowPartialAdd= */true);
				UE_LOG(TruckStationSettings, Verbose, TEXT("Added %d items to Docking station inventory"), added);
				dockedInventory->RemoveFromIndex(itemDescIndex, added);
				scope.Cancel();
				if (added == 0) {
					self->LoadUnloadVehicleComplete();
				}
				return;
			}

			scope(self, dockedInventory);
		});
		SUBSCRIBE_METHOD(AFGBuildableDockingStation::LoadUnloadVehicleComplete, [](auto& scope, AFGBuildableDockingStation* self) {
			UE_LOG(TruckStationSettings, Verbose, TEXT("LoadUnloadVehicleComplete"));
			scope(self);
		});
	}
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