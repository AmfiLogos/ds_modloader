// Hand-written offset checks for everything ord_core.c / ord_game.c read or write outside the generated
// event table. Compiled against il2cpp.h (build 25846389) by run_tests.sh; a wrong offset fails the build.
#include "ord_il2cpp_pre.h"
#define OFF(T, f, v) static_assert(offsetof(T, f) == (v), #T "." #f)
// Interaction (value type: no object header) and its UIInput
OFF(Refactor_Control_Interaction_o, fields.InteractionType, 0x0);
OFF(Refactor_Control_Interaction_o, fields.ShowUserName, 0x4);
OFF(Refactor_Control_Interaction_o, fields.HideTargetTextInCursor, 0x5);
OFF(Refactor_Control_Interaction_o, fields.Durability, 0x8);
OFF(Refactor_Control_Interaction_o, fields.TargetText, 0x10);
OFF(Refactor_Control_Interaction_o, fields.InvalidMessage, 0x18);
OFF(Refactor_Control_Interaction_o, fields.IsPossible, 0x20);
OFF(Refactor_Control_Interaction_o, fields.UserEntity, 0x28);
OFF(Refactor_Control_Interaction_o, fields.TargetEntity, 0x30);
OFF(Refactor_Control_Interaction_o, fields.ItemEntity, 0x38);
OFF(Refactor_Control_Interaction_o, fields.Index, 0x40);
OFF(Refactor_Control_Interaction_o, fields.InteractionInputType, 0x44);
OFF(Refactor_Control_Interaction_o, fields.InteractionUIInput, 0x48);
OFF(Refactor_Control_Interaction_o, fields.TaskType, 0xD0);
static_assert(sizeof(Refactor_Control_Interaction_o) == 0xD8, "Interaction size");
OFF(Refactor_Main_InputModule_UIInput_o, fields.UIInputType, 0x0);
OFF(Refactor_Main_InputModule_UIInput_o, fields.TradeCommit, 0x8);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamIndex1, 0x10);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamIndex2, 0x14);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamIndex3, 0x18);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamString1, 0x20);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamString2, 0x28);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamGuid1, 0x30);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamGuid2, 0x40);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamGuid3, 0x50);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamGuid3.fields.value, 0x54);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamGuidList, 0x68);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamBool1, 0x70);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamBool2, 0x71);
OFF(Refactor_Main_InputModule_UIInput_o, fields.ParamBool3, 0x72);
static_assert(sizeof(Refactor_Main_InputModule_UIInput_o) == 0x88, "UIInput size");
// Nullable layouts used by the event serializer
OFF(System_Nullable_Guid__o, fields.value, 0x4);
OFF(System_Nullable_int__o, fields.value, 0x4);
OFF(System_Nullable_bool__o, fields.value, 0x1);
// trade
OFF(Refactor_Main_TradeCommitRequest_o, fields._Map_k__BackingField, 0x10);
OFF(Refactor_Main_TradeCommitRequest_o, fields._SessionId_k__BackingField, 0x14);
OFF(Refactor_Main_TradeCommitRequest_o, fields._Deliveries_k__BackingField, 0x28);
OFF(Refactor_Main_TradeCommitDelivery_o, fields._SlotId_k__BackingField, 0x0);
OFF(Refactor_Main_TradeCommitDelivery_o, fields._Destination_k__BackingField, 0x10);
OFF(Refactor_Main_TradeCommitDelivery_o, fields._TargetOwnerId_k__BackingField, 0x14);
OFF(Refactor_Main_TradeCommitDelivery_o, fields._Amount_k__BackingField, 0x24);
static_assert(sizeof(Refactor_Main_TradeCommitDelivery_o) == 0x28, "TradeCommitDelivery size");
OFF(Refactor_Main_TradeHelper_o, fields._sessionId, 0x44);
OFF(System_Collections_ObjectModel_ReadOnlyCollection_T__o, fields.list, 0x10);
// selection
OFF(Refactor_Main_Processor_SelectDataContainer_o, fields._selectGroup, 0x10);
OFF(Refactor_Main_Processor_SelectDataContainer_o, fields._selectedEntities, 0x18);
OFF(System_Collections_Generic_List_IEntity__o, fields._items, 0x10);
OFF(System_Collections_Generic_List_IEntity__o, fields._size, 0x18);
OFF(System_Collections_Generic_List_IEntity__o, fields._version, 0x1C);
OFF(System_Collections_Generic_List_Guid__o, fields._items, 0x10);
OFF(System_Collections_Generic_List_Guid__o, fields._size, 0x18);
OFF(System_Collections_Generic_List_Guid__o, fields._version, 0x1C);
OFF(System_Guid_array, m_Items, 0x20);
OFF(System_Guid_array, max_length, 0x18);
// navigation from MainFlow
OFF(Refactor_Main_MainFlow_o, fields._eventQueue, 0x48);
OFF(Refactor_Main_MainFlow_o, fields._eventHandler, 0x50);
OFF(Refactor_Main_MainFlow_o, fields._inputDispatcher, 0x68);
OFF(Refactor_Main_MainFlow_o, fields._campaignData, 0xC8);
OFF(Refactor_Main_MainFlow_o, fields._inMapDatas, 0x128);
OFF(Refactor_Main_CampaignDataContainer_o, fields.EntityDataContainer, 0x20);
OFF(Refactor_Main_EntityDataContainer_o, fields._unitContainers, 0x18);
OFF(Refactor_Main_EntityDataContainer_o, fields._itemContainers, 0x28);
OFF(Refactor_Main_EventQueue_o, fields._syncEventHandlerMap, 0x28);
OFF(Refactor_Main_EventQueue_o, fields._asyncEventHandlerMap, 0x30);
OFF(System_Collections_Generic_Dictionary_Type__Action_IEventData___o, fields._entries, 0x18);
OFF(System_Collections_Generic_Dictionary_Type__Action_IEventData___o, fields._count, 0x20);
OFF(System_Delegate_o, fields.invoke_impl, 0x18);
OFF(System_Delegate_o, fields.method, 0x28);
OFF(System_Delegate_o, fields.method_code, 0x40);
OFF(Refactor_Main_Event_EventHandler_o, fields._ItemEventHandler_k__BackingField, 0x10);
OFF(Refactor_Main_Event_EventHandler_o, fields._PlayerControlEventHandler_k__BackingField, 0x38);
OFF(Refactor_Main_Event_PlayerControlEventHandler_o, fields._entityComponent, 0x18);
OFF(Refactor_Main_Event_PlayerControlEventHandler_o, fields._modeProcessor, 0x20);
OFF(Refactor_Main_Event_PlayerControlEventHandler_o, fields._selectReader, 0x48);
OFF(Refactor_Main_Event_ItemEventHandler_o, fields._inMapDatas, 0x18);
OFF(Refactor_Main_Event_ItemEventHandler_o, fields._objectEntityGuidReader, 0x88);
OFF(Refactor_Control_DefaultMode_o, fields._command, 0x18);
OFF(Refactor_Control_DefaultCommand_o, fields._flow, 0x20);
OFF(Refactor_Control_CommandFlow_o, fields._runner, 0x10);
OFF(Refactor_Control_CommandFlow_o, fields._move, 0x18);
OFF(Refactor_Control_CommandFlow_o, fields._waypoint, 0x20);
OFF(Refactor_Control_InteractionRunner_o, fields._check, 0x10);
OFF(Refactor_Control_InteractionRunner_o, fields._action, 0x18);
OFF(Refactor_Control_InteractionRunner_o, fields._move, 0x20);
OFF(Refactor_Main_InputModule_InputDispatcher_o, fields._inputBinding, 0x28);
OFF(Refactor_Main_InputModule_InputBinding_o, fields._buffer, 0x38);
// entities, skills, selectors, observers
OFF(Refactor_UnitEntity_o, fields._Guid_k__BackingField, 0x24);
OFF(Refactor_BuildingEntity_o, fields._Guid_k__BackingField, 0x10);
OFF(Refactor_ItemEntity_o, fields._Guid_k__BackingField, 0x10);
OFF(Refactor_ThingEntity_o, fields._Guid_k__BackingField, 0x10);
OFF(Refactor_FarmEntity_o, fields._Guid_k__BackingField, 0x10);
OFF(Refactor_BlueprintEntity_o, fields._Guid_k__BackingField, 0x10);
OFF(Refactor_NullEntity_o, fields._Guid_k__BackingField, 0x10);
OFF(Refactor_Component_SkillComponent_o, fields._owner, 0x10);
OFF(Refactor_Skill_Base_o, fields._Key_k__BackingField, 0x10);
OFF(Refactor_Runtime_Map_GridData_o, fields._Position_k__BackingField, 0x10);
OFF(Refactor_Runtime_Map_GridData_o, fields._Flag_k__BackingField, 0x18);
OFF(Refactor_ObserverDriven_InputObserver_o, fields.InputType, 0x10);
// Set 2
OFF(Refactor_Main_Event_PlayerControlEventHandler_o, fields._constructionTargetControl, 0x38);
OFF(Refactor_ConstructionTargetControl_o, fields._isFreeConstructionMode, 0x10);
OFF(Refactor_ConstructionTargetControl_o, fields._isRemoveFloorCreepClearMode, 0x11);
OFF(Refactor_ConstructionTargetControl_o, fields._constructionKey, 0x18);
OFF(Refactor_ConstructionTargetControl_o, fields._constructionRotation, 0x20);
OFF(Refactor_ConstructionTargetControl_o, fields._dragStartPos, 0x24);
OFF(Refactor_Main_Event_ItemEventHandler_o, fields._expeditionPlanPartyReader, 0x68);
OFF(Refactor_Main_MainFlow_o, fields._expeditionPlanPartyContext, 0x90);
OFF(Refactor_UI_ExpeditionPlanPartyContext_o, fields._partyMembers, 0x10);
OFF(Refactor_UI_ExpeditionPlanPartyContext_o, fields._IsActive_k__BackingField, 0x18);
OFF(System_Collections_Generic_HashSet_Guid__o, fields._slots, 0x18);
OFF(System_Collections_Generic_HashSet_Guid__o, fields._lastIndex, 0x24);
OFF(System_Collections_Generic_HashSet_Slot_Guid__o, fields.value, 0x8);
static_assert(sizeof(System_Collections_Generic_HashSet_Slot_Guid__o) == 0x18, "HashSet<Guid> slot size");
// item ownership
OFF(Refactor_InventoryComponent_o, fields._itemSlots, 0x18);
static_assert(sizeof(Refactor_ItemHolder_o) == 0x20, "ItemHolder size");
OFF(Refactor_ItemHolder_o, fields.Guid, 0x0);
OFF(Refactor_Main_ItemDataContainer_o, fields._items, 0x18);
OFF(Refactor_Main_ItemDataContainer_o, fields._onMapItems, 0x20);
static_assert(sizeof(System_Collections_Generic_Dictionary_Entry_Guid__ItemHolder__o) == 0x38, "Entry<Guid,ItemHolder>");
static_assert(sizeof(System_Collections_Generic_Dictionary_Entry_Guid__object__o) == 0x20, "Entry<Guid,obj>");
OFF(Refactor_Task_SharedTaskData_o, fields._Target_k__BackingField, 0x28);
OFF(Refactor_Main_Event_PickUpItemRequested_o, fields.To, 0x20);
OFF(Refactor_Main_Event_PickUpItemRequested_o, fields.Item, 0x28);
// pick-up task filter
OFF(Refactor_Task_TaskGroup_o, fields._EntityGuid_k__BackingField, 0x20);
OFF(Refactor_Task_TaskGroup_o, fields._Tasks_k__BackingField, 0x38);
OFF(Refactor_Task_SharedTaskData_o, fields._TaskType_k__BackingField, 0x20);
int main() { return 0; }
