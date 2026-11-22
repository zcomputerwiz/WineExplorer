/*
 * mi.dll: the function tables, made by gen_tables.py from MI.h (OMI, MIT licence); do not edit
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */


#include "mi_priv.h"

MI_Result MI_CALL impl_MI_ApplicationFT_Close(_Inout_ MI_Application *application);
MI_Result MI_CALL impl_MI_ApplicationFT_NewSession(_In_ MI_Application *application, _In_opt_z_ const MI_Char *protocol, _In_opt_z_ const MI_Char *destination, _In_opt_ MI_DestinationOptions *options, _In_opt_ MI_SessionCallbacks *callbacks, _Outptr_opt_result_maybenull_ MI_Instance **extendedError, _Out_ MI_Session *session);
static MI_Result MI_CALL stub_MI_ApplicationFT_NewHostedProvider(_In_ MI_Application *application, _In_z_ const MI_Char *namespaceName, _In_z_ const MI_Char *providerName, _In_ MI_MainFunction mi_Main, _Outptr_opt_result_maybenull_ MI_Instance **extendedError, _Out_ MI_HostedProvider *provider) { mi_trace("MI_ApplicationFT_NewHostedProvider"); if (extendedError) *extendedError = 0; return MI_RESULT_NOT_SUPPORTED; }
MI_Result MI_CALL impl_MI_ApplicationFT_NewInstance(_In_ MI_Application *application, _In_z_ const MI_Char *className, _In_opt_ const MI_ClassDecl *classRTTI, _Outptr_ MI_Instance **instance);
MI_Result MI_CALL impl_MI_ApplicationFT_NewDestinationOptions(_In_ MI_Application *application, _Out_ MI_DestinationOptions *options);
MI_Result MI_CALL impl_MI_ApplicationFT_NewOperationOptions(_In_ MI_Application *application, _In_ MI_Boolean customOptionsMustUnderstand, _Out_ MI_OperationOptions *options);
static MI_Result MI_CALL stub_MI_ApplicationFT_NewSubscriptionDeliveryOptions(_In_ MI_Application *application, _In_ MI_SubscriptionDeliveryType deliveryType, _Out_ MI_SubscriptionDeliveryOptions *deliveryOptions) { mi_trace("MI_ApplicationFT_NewSubscriptionDeliveryOptions"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_ApplicationFT_NewSerializer(_Inout_ MI_Application *application, MI_Uint32 flags, _In_z_ MI_Char *format, _Out_ MI_Serializer *serializer) { mi_trace("MI_ApplicationFT_NewSerializer"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_ApplicationFT_NewDeserializer(_Inout_ MI_Application *application, MI_Uint32 flags, _In_z_ MI_Char *format, _Out_ MI_Deserializer *deserializer) { mi_trace("MI_ApplicationFT_NewDeserializer"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_ApplicationFT_NewInstanceFromClass(_In_ MI_Application *application, _In_z_ const MI_Char *className, _In_opt_ const MI_Class *classObject, _Outptr_ MI_Instance **instance) { mi_trace("MI_ApplicationFT_NewInstanceFromClass"); if (instance) *instance = 0; return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_ApplicationFT_NewClass(_In_ MI_Application *application, _In_ const MI_ClassDecl* classDecl, _In_opt_z_ const MI_Char *namespaceName, _In_opt_z_ const MI_Char *serverName, _Outptr_ MI_Class** classObject) { mi_trace("MI_ApplicationFT_NewClass"); if (classObject) *classObject = 0; return MI_RESULT_NOT_SUPPORTED; }
MI_Result MI_CALL impl_MI_SessionFT_Close(_Inout_ MI_Session *session, _In_opt_ void *completionContext, _In_opt_ void (MI_CALL *completionCallback)(_In_opt_ void *completionContext));
MI_Result MI_CALL impl_MI_SessionFT_GetApplication(_In_ MI_Session *session, _Out_ MI_Application *application);
void MI_CALL impl_MI_SessionFT_GetInstance(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_ const MI_Instance *inboundInstance, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_ModifyInstance(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_ const MI_Instance *inboundInstance, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_CreateInstance(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_ const MI_Instance *inboundInstance, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_DeleteInstance(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_ const MI_Instance *inboundInstance, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_Invoke(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_opt_z_ const MI_Char *className, _In_z_ const MI_Char *methodName, _In_opt_ const MI_Instance *inboundInstance, _In_opt_ const MI_Instance *inboundProperties, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_EnumerateInstances(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_opt_z_ const MI_Char *className, MI_Boolean keysOnly, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_QueryInstances(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_opt_z_ const MI_Char *queryDialect, _In_opt_z_ const MI_Char *queryExpression, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_AssociatorInstances(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_ const MI_Instance *instanceKeys, _In_opt_z_ const MI_Char *assocClass, _In_opt_z_ const MI_Char *resultClass, _In_opt_z_ const MI_Char *role, _In_opt_z_ const MI_Char *resultRole, MI_Boolean keysOnly, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_ReferenceInstances(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_ const MI_Instance *instanceKeys, _In_opt_z_ const MI_Char *resultClass, _In_opt_z_ const MI_Char *role, MI_Boolean keysOnly, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_Subscribe(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_opt_z_ const MI_Char *queryDialect, _In_opt_z_ const MI_Char *queryExpression, _In_opt_ const MI_SubscriptionDeliveryOptions *deliverOptions, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_GetClass(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_opt_z_ const MI_Char *className, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_EnumerateClasses(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationOptions *options, _In_opt_z_ const MI_Char *namespaceName, _In_opt_z_ const MI_Char *className, MI_Boolean classNamesOnly, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
void MI_CALL impl_MI_SessionFT_TestConnection(_In_ MI_Session *session, MI_Uint32 flags, _In_opt_ MI_OperationCallbacks *callbacks, _Out_ MI_Operation *operation);
MI_Result MI_CALL impl_MI_OperationFT_Close(_Inout_ MI_Operation *operation);
MI_Result MI_CALL impl_MI_OperationFT_Cancel(_Inout_ MI_Operation *operation, MI_CancellationReason reason);
MI_Result MI_CALL impl_MI_OperationFT_GetSession(_In_ MI_Operation *operation, _Out_ MI_Session *session);
MI_Result MI_CALL impl_MI_OperationFT_GetInstance(_In_ MI_Operation *operation, _Outptr_result_maybenull_ const MI_Instance **instance, _Out_opt_ MI_Boolean *moreResults, _Out_opt_ MI_Result *result, _Outptr_opt_result_maybenull_z_ const MI_Char **errorMessage, _Outptr_opt_result_maybenull_ const MI_Instance **completionDetails);
static MI_Result MI_CALL stub_MI_OperationFT_GetIndication(_In_ MI_Operation *operation, _Outptr_result_maybenull_ const MI_Instance **instance, _Outptr_opt_result_maybenull_z_ const MI_Char **bookmark, _Outptr_opt_result_maybenull_z_ const MI_Char **machineID, _Out_opt_ MI_Boolean *moreResults, _Out_opt_ MI_Result *result, _Outptr_opt_result_maybenull_z_ const MI_Char **errorMessage, _Outptr_opt_result_maybenull_ const MI_Instance **completionDetails) { mi_trace("MI_OperationFT_GetIndication"); if (instance) *instance = 0; if (bookmark) *bookmark = 0; if (machineID) *machineID = 0; if (errorMessage) *errorMessage = 0; if (completionDetails) *completionDetails = 0; return MI_RESULT_NOT_SUPPORTED; }
MI_Result MI_CALL impl_MI_OperationFT_GetClass(_In_ MI_Operation *operation, _Outptr_result_maybenull_ const MI_Class **classResult, _Out_opt_ MI_Boolean *moreResults, _Out_opt_ MI_Result *result, _Outptr_opt_result_maybenull_z_ const MI_Char **errorMessage, _Outptr_opt_result_maybenull_ const MI_Instance **completionDetails);
static MI_Result MI_CALL stub_MI_HostedProviderFT_Close(_Inout_ MI_HostedProvider *hostedProvider) { mi_trace("MI_HostedProviderFT_Close"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_HostedProviderFT_GetApplication(_In_ MI_HostedProvider *hostedProvider, _Out_ MI_Application *application) { mi_trace("MI_HostedProviderFT_GetApplication"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_SerializerFT_Close(_Inout_ MI_Serializer *serializer) { mi_trace("MI_SerializerFT_Close"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_SerializerFT_SerializeClass(_Inout_ MI_Serializer *serializer, MI_Uint32 flags, _In_ const MI_Class *classObject, _Out_writes_bytes_(clientBufferLength) MI_Uint8 *clientBuffer, MI_Uint32 clientBufferLength, _Inout_ MI_Uint32 *clientBufferNeeded) { mi_trace("MI_SerializerFT_SerializeClass"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_SerializerFT_SerializeInstance(_Inout_ MI_Serializer *serializer, MI_Uint32 flags, _In_ const MI_Instance *instanceObject, _Out_writes_bytes_(clientBufferLength) MI_Uint8 *clientBuffer, MI_Uint32 clientBufferLength, _Inout_ MI_Uint32 *clientBufferNeeded) { mi_trace("MI_SerializerFT_SerializeInstance"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_DeserializerFT_Close(_Inout_ MI_Deserializer *deserializer) { mi_trace("MI_DeserializerFT_Close"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_DeserializerFT_DeserializeClass(_Inout_ MI_Deserializer *deserializer, MI_Uint32 flags, _In_reads_(serializedBufferLength) MI_Uint8 *serializedBuffer, MI_Uint32 serializedBufferLength, _In_opt_ MI_Class *parentClass, _In_opt_z_ const MI_Char *serverName, _In_opt_z_ const MI_Char *namespaceName, _In_opt_ MI_Deserializer_ClassObjectNeeded classObjectNeeded, _In_opt_ void *classObjectNeededContext, _Out_opt_ MI_Uint32 *serializedBufferRead, _Outptr_opt_result_maybenull_ MI_Class **classObject, _Outptr_opt_result_maybenull_ MI_Instance **cimErrorDetails) { mi_trace("MI_DeserializerFT_DeserializeClass"); if (classObject) *classObject = 0; if (cimErrorDetails) *cimErrorDetails = 0; return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_DeserializerFT_Class_GetClassName(_Inout_ MI_Deserializer *deserializer, _In_reads_(serializedBufferLength) MI_Uint8 *serializedBuffer, MI_Uint32 serializedBufferLength, _Out_writes_opt_(*classNameLength) MI_Char *className, _Inout_ MI_Uint32 *classNameLength, _Outptr_opt_result_maybenull_ MI_Instance **cimErrorDetails) { mi_trace("MI_DeserializerFT_Class_GetClassName"); if (cimErrorDetails) *cimErrorDetails = 0; return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_DeserializerFT_Class_GetParentClassName(_Inout_ MI_Deserializer *deserializer, _In_reads_(serializedBufferLength) MI_Uint8 *serializedBuffer, MI_Uint32 serializedBufferLength, _Out_writes_opt_(*parentClassNameLength) MI_Char *parentClassName, _Inout_ MI_Uint32 *parentClassNameLength, _Outptr_opt_result_maybenull_ MI_Instance **cimErrorDetails) { mi_trace("MI_DeserializerFT_Class_GetParentClassName"); if (cimErrorDetails) *cimErrorDetails = 0; return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_DeserializerFT_DeserializeInstance(_Inout_ MI_Deserializer *deserializer, MI_Uint32 flags, _In_reads_(serializedBufferLength) MI_Uint8 *serializedBuffer, MI_Uint32 serializedBufferLength, _In_reads_opt_(numberClassObjects) MI_Class **classObjects, MI_Uint32 numberClassObjects, _In_opt_ MI_Deserializer_ClassObjectNeeded classObjectNeeded, _In_opt_ void *classObjectNeededContext, _Out_opt_ MI_Uint32 *serializedBufferRead, _Outptr_opt_result_maybenull_ MI_Instance **instanceObject, _Outptr_opt_result_maybenull_ MI_Instance **cimErrorDetails) { mi_trace("MI_DeserializerFT_DeserializeInstance"); if (instanceObject) *instanceObject = 0; if (cimErrorDetails) *cimErrorDetails = 0; return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_DeserializerFT_Instance_GetClassName(_Inout_ MI_Deserializer *deserializer, _In_reads_(serializedBufferLength) MI_Uint8 *serializedBuffer, MI_Uint32 serializedBufferLength, _Out_writes_opt_(*classNameLength) MI_Char *className, _Inout_ MI_Uint32 *classNameLength, _Outptr_opt_result_maybenull_ MI_Instance **cimErrorDetails) { mi_trace("MI_DeserializerFT_Instance_GetClassName"); if (cimErrorDetails) *cimErrorDetails = 0; return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_SetString(_Inout_ MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _In_z_ const MI_Char *value, MI_Uint32 flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_SetString"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_SetNumber(_Inout_ MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _In_ MI_Uint32 value, MI_Uint32 flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_SetNumber"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_SetDateTime(_Inout_ MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _In_ const MI_Datetime *value, MI_Uint32 flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_SetDateTime"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_SetInterval(_Inout_ MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _In_ const MI_Interval *value, MI_Uint32 flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_SetInterval"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_AddCredentials(_Inout_ MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _In_ const MI_UserCredentials *credentials, MI_Uint32 flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_AddCredentials"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_Delete(_Inout_ MI_SubscriptionDeliveryOptions* self) { mi_trace("MI_SubscriptionDeliveryOptionsFT_Delete"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetString(_In_ const MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _Outptr_result_z_ const MI_Char **value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetString"); if (value) *value = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetNumber(_In_ const MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Uint32 *value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetNumber"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetDateTime(_In_ const MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Datetime *value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetDateTime"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetInterval(_In_ const MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Interval *value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetInterval"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetOptionCount(_In_ const MI_SubscriptionDeliveryOptions *options, _Out_opt_ MI_Uint32 *count) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetOptionCount"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetOptionAt(_In_ const MI_SubscriptionDeliveryOptions *options, MI_Uint32 index, _Outptr_result_z_ const MI_Char **optionName, _Out_ MI_Value *value, _Out_ MI_Type *type, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetOptionAt"); if (optionName) *optionName = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetOption(_In_ const MI_SubscriptionDeliveryOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Value *value, _Out_ MI_Type *type, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetOption"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetCredentialsCount(_In_ const MI_SubscriptionDeliveryOptions *options, _Out_ MI_Uint32 *count) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetCredentialsCount"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetCredentialsAt(_In_ const MI_SubscriptionDeliveryOptions *options, MI_Uint32 index, _Outptr_result_z_ const MI_Char **optionName, _Out_ MI_UserCredentials *credentials, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetCredentialsAt"); if (optionName) *optionName = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_GetCredentialsPasswordAt(_In_ const MI_SubscriptionDeliveryOptions *options, MI_Uint32 index, _Outptr_result_z_ const MI_Char **optionName, _Out_writes_to_opt_(bufferLength, *passwordLength) MI_Char *password, _In_ MI_Uint32 bufferLength, _Out_ MI_Uint32 *passwordLength, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_SubscriptionDeliveryOptionsFT_GetCredentialsPasswordAt"); if (optionName) *optionName = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_SubscriptionDeliveryOptionsFT_Clone(_In_ const MI_SubscriptionDeliveryOptions* self, _Out_ MI_SubscriptionDeliveryOptions* newSubscriptionDeliveryOptions) { mi_trace("MI_SubscriptionDeliveryOptionsFT_Clone"); return MI_RESULT_OK; }
static void MI_CALL stub_MI_DestinationOptionsFT_Delete(_Inout_ MI_DestinationOptions *options) { mi_trace("MI_DestinationOptionsFT_Delete");  }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_SetString(_Inout_ MI_DestinationOptions *options, _In_z_ const MI_Char *optionName, _In_z_ const MI_Char *value, MI_Uint32 flags) { mi_trace("MI_DestinationOptionsFT_SetString"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_SetNumber(_Inout_ MI_DestinationOptions *options, _In_z_ const MI_Char *optionName, _In_ MI_Uint32 value, MI_Uint32 flags) { mi_trace("MI_DestinationOptionsFT_SetNumber"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_AddCredentials(_Inout_ MI_DestinationOptions *options, _In_z_ const MI_Char *optionName, _In_ const MI_UserCredentials *credentials, MI_Uint32 flags) { mi_trace("MI_DestinationOptionsFT_AddCredentials"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetString(_In_ const MI_DestinationOptions *options, _In_z_ const MI_Char *optionName, _Outptr_result_z_ const MI_Char **value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_DestinationOptionsFT_GetString"); if (value) *value = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetNumber(_In_ const MI_DestinationOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Uint32 *value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_DestinationOptionsFT_GetNumber"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetOptionCount(_In_ const MI_DestinationOptions *options, _Out_ MI_Uint32 *count) { mi_trace("MI_DestinationOptionsFT_GetOptionCount"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetOptionAt(_In_ const MI_DestinationOptions *options, MI_Uint32 index, _Outptr_result_z_ const MI_Char **optionName, _Out_ MI_Value *value, _Out_ MI_Type *type, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_DestinationOptionsFT_GetOptionAt"); if (optionName) *optionName = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetOption(_In_ const MI_DestinationOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Value *value, _Out_ MI_Type *type, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_DestinationOptionsFT_GetOption"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetCredentialsCount(_In_ const MI_DestinationOptions *options, _Out_ MI_Uint32 *count) { mi_trace("MI_DestinationOptionsFT_GetCredentialsCount"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetCredentialsAt(_In_ const MI_DestinationOptions *options, MI_Uint32 index, _Outptr_result_z_ const MI_Char **optionName, _Out_ MI_UserCredentials *credentials, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_DestinationOptionsFT_GetCredentialsAt"); if (optionName) *optionName = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetCredentialsPasswordAt(_In_ const MI_DestinationOptions *options, MI_Uint32 index, _Outptr_result_z_ const MI_Char **optionName, _Out_writes_to_opt_(bufferLength, *passwordLength) MI_Char *password, _In_ MI_Uint32 bufferLength, _Out_ MI_Uint32 *passwordLength, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_DestinationOptionsFT_GetCredentialsPasswordAt"); if (optionName) *optionName = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_Clone(_In_ const MI_DestinationOptions* self, _Out_ MI_DestinationOptions* newDestinationOptions) { mi_trace("MI_DestinationOptionsFT_Clone"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_SetInterval(_Inout_ MI_DestinationOptions *options, _In_z_ const MI_Char *optionName, _In_ const MI_Interval *value, MI_Uint32 flags) { mi_trace("MI_DestinationOptionsFT_SetInterval"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_DestinationOptionsFT_GetInterval(_In_ const MI_DestinationOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Interval *value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_DestinationOptionsFT_GetInterval"); return MI_RESULT_NOT_FOUND; }
static void MI_CALL stub_MI_OperationOptionsFT_Delete(_Inout_ MI_OperationOptions *options) { mi_trace("MI_OperationOptionsFT_Delete");  }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_SetString(_Inout_ MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _In_z_ const MI_Char *value, MI_Uint32 flags) { mi_trace("MI_OperationOptionsFT_SetString"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_SetNumber(_Inout_ MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _In_ MI_Uint32 value, MI_Uint32 flags) { mi_trace("MI_OperationOptionsFT_SetNumber"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_SetCustomOption(_Inout_ MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _In_ MI_Type valueType, _In_ const MI_Value *value, MI_Boolean mustComply, MI_Uint32 flags) { mi_trace("MI_OperationOptionsFT_SetCustomOption"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_GetString(_In_ const MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _Outptr_result_z_ const MI_Char **value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_OperationOptionsFT_GetString"); if (value) *value = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_GetNumber(_In_ const MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Uint32 *value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_OperationOptionsFT_GetNumber"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_GetOptionCount(_In_ const MI_OperationOptions *options, _Out_ MI_Uint32 *count) { mi_trace("MI_OperationOptionsFT_GetOptionCount"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_GetOptionAt(_In_ const MI_OperationOptions *options, MI_Uint32 index, _Outptr_result_z_ const MI_Char **optionName, _Out_ MI_Value *value, _Out_ MI_Type *type, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_OperationOptionsFT_GetOptionAt"); if (optionName) *optionName = 0; return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_GetOption(_In_ const MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Value *value, _Out_ MI_Type *type, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_OperationOptionsFT_GetOption"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_GetEnabledChannels(_In_ const MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _Out_writes_to_opt_(bufferLength, *channelCount) MI_Uint32 *channels, _In_ MI_Uint32 bufferLength, _Out_ MI_Uint32 *channelCount, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_OperationOptionsFT_GetEnabledChannels"); return MI_RESULT_NOT_FOUND; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_Clone(_In_ const MI_OperationOptions* self, _Out_ MI_OperationOptions* newOperationOptions) { mi_trace("MI_OperationOptionsFT_Clone"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_SetInterval(_Inout_ MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _In_ const MI_Interval *value, MI_Uint32 flags) { mi_trace("MI_OperationOptionsFT_SetInterval"); return MI_RESULT_OK; }
static MI_Result MI_CALL stub_MI_OperationOptionsFT_GetInterval(_In_ const MI_OperationOptions *options, _In_z_ const MI_Char *optionName, _Out_ MI_Interval *value, _Out_opt_ MI_Uint32 *index, _Out_opt_ MI_Uint32 *flags) { mi_trace("MI_OperationOptionsFT_GetInterval"); return MI_RESULT_NOT_FOUND; }
static MI_ErrorCategory MI_CALL stub_MI_UtilitiesFT_MapErrorToMiErrorCategory(_In_z_ const MI_Char *errorType, MI_Uint32 error) { mi_trace("MI_UtilitiesFT_MapErrorToMiErrorCategory"); return 0; }
static MI_Result MI_CALL stub_MI_UtilitiesFT_CimErrorFromErrorCode(MI_Uint32 error, _In_z_ const MI_Char *errorType, _In_z_ const MI_Char* errorMessage, _Outptr_ MI_Instance **cimError) { mi_trace("MI_UtilitiesFT_CimErrorFromErrorCode"); if (cimError) *cimError = 0; return MI_RESULT_NOT_SUPPORTED; }
MI_Result MI_CALL impl_MI_InstanceFT_Clone(_In_ const MI_Instance* self, _Outptr_ MI_Instance** newInstance);
MI_Result MI_CALL impl_MI_InstanceFT_Destruct(_Inout_ MI_Instance* self);
MI_Result MI_CALL impl_MI_InstanceFT_Delete(_Inout_ MI_Instance* self);
static MI_Result MI_CALL stub_MI_InstanceFT_IsA(_In_ const MI_Instance* self, _In_ const MI_ClassDecl* classDecl, _Out_ MI_Boolean* flag) { mi_trace("MI_InstanceFT_IsA"); return MI_RESULT_NOT_SUPPORTED; }
MI_Result MI_CALL impl_MI_InstanceFT_GetClassName(_In_ const MI_Instance* self, _Outptr_result_maybenull_z_ const MI_Char** className);
MI_Result MI_CALL impl_MI_InstanceFT_SetNameSpace(_Inout_ MI_Instance* self, _In_z_ const MI_Char* nameSpace);
MI_Result MI_CALL impl_MI_InstanceFT_GetNameSpace(_In_ const MI_Instance* self, _Outptr_result_maybenull_z_ const MI_Char** nameSpace);
MI_Result MI_CALL impl_MI_InstanceFT_GetElementCount(_In_ const MI_Instance* self, _Out_ MI_Uint32* count);
MI_Result MI_CALL impl_MI_InstanceFT_AddElement(_Inout_ MI_Instance* self, _In_z_ const MI_Char* name, _In_opt_ const MI_Value* value, MI_Type type, MI_Uint32 flags);
MI_Result MI_CALL impl_MI_InstanceFT_SetElement(_Inout_ MI_Instance* self, _In_z_ const MI_Char* name, _In_opt_ const MI_Value* value, MI_Type type, MI_Uint32 flags);
MI_Result MI_CALL impl_MI_InstanceFT_SetElementAt(_Inout_ MI_Instance* self, MI_Uint32 index, _In_reads_opt_(_Inexpressible_("varies depending on type")) const MI_Value* value, MI_Type type, MI_Uint32 flags);
MI_Result MI_CALL impl_MI_InstanceFT_GetElement(_In_ const MI_Instance* self, _In_z_ const MI_Char* name, _Out_opt_ MI_Value* value, _Out_opt_ MI_Type* type, _Out_opt_ MI_Uint32* flags, _Out_opt_ MI_Uint32* index);
MI_Result MI_CALL impl_MI_InstanceFT_GetElementAt(_In_ const MI_Instance* self, MI_Uint32 index, _Outptr_result_maybenull_z_ const MI_Char** name, _Out_opt_ MI_Value* value, _Out_opt_ MI_Type* type, _Out_opt_ MI_Uint32* flags);
MI_Result MI_CALL impl_MI_InstanceFT_ClearElement(_Inout_ MI_Instance* self, _In_z_ const MI_Char* name);
MI_Result MI_CALL impl_MI_InstanceFT_ClearElementAt(_Inout_ MI_Instance* self, MI_Uint32 index);
MI_Result MI_CALL impl_MI_InstanceFT_GetServerName(_In_ const MI_Instance* self, _Outptr_result_maybenull_z_ const MI_Char** name);
MI_Result MI_CALL impl_MI_InstanceFT_SetServerName(_Inout_ MI_Instance* self, _In_z_ const MI_Char* name);
MI_Result MI_CALL impl_MI_InstanceFT_GetClass(_In_ const MI_Instance* self, _Outptr_ MI_Class** instanceClass);
MI_Result MI_CALL impl_MI_ClassFT_GetClassName(_In_ const MI_Class* self, _Outptr_result_maybenull_z_ const MI_Char** className);
MI_Result MI_CALL impl_MI_ClassFT_GetNameSpace(_In_ const MI_Class* self, _Outptr_result_maybenull_z_ const MI_Char** nameSpace);
MI_Result MI_CALL impl_MI_ClassFT_GetServerName(_In_ const MI_Class* self, _Outptr_result_maybenull_z_ const MI_Char** serverName);
MI_Result MI_CALL impl_MI_ClassFT_GetElementCount(_In_ const MI_Class* self, _Out_ MI_Uint32* count);
MI_Result MI_CALL impl_MI_ClassFT_GetElement(_In_ const MI_Class* self, _In_z_ const MI_Char* name, _Out_opt_ MI_Value* value, _Out_opt_ MI_Boolean* valueExists, _Out_opt_ MI_Type* type, _Outptr_opt_result_maybenull_z_ MI_Char **referenceClass, _Out_opt_ MI_QualifierSet *qualifierSet, _Out_opt_ MI_Uint32* flags, _Out_opt_ MI_Uint32* index);
MI_Result MI_CALL impl_MI_ClassFT_GetElementAt(_In_ const MI_Class* self, MI_Uint32 index, _Outptr_opt_result_maybenull_z_ const MI_Char** name, _Out_opt_ MI_Value* value, _Out_opt_ MI_Boolean* valueExists, _Out_opt_ MI_Type* type, _Outptr_opt_result_maybenull_z_ MI_Char **referenceClass, _Out_opt_ MI_QualifierSet *qualifierSet, _Out_opt_ MI_Uint32* flags);
MI_Result MI_CALL impl_MI_ClassFT_GetClassQualifierSet(_In_ const MI_Class* self, _Out_opt_ MI_QualifierSet *qualifierSet);
MI_Result MI_CALL impl_MI_ClassFT_GetMethodCount(_In_ const MI_Class* self, _Out_ MI_Uint32* count);
MI_Result MI_CALL impl_MI_ClassFT_GetMethodAt(_In_ const MI_Class *self, MI_Uint32 index, _Outptr_result_z_ const MI_Char **name, _Out_opt_ MI_QualifierSet *qualifierSet, _Out_opt_ MI_ParameterSet *parameterSet);
MI_Result MI_CALL impl_MI_ClassFT_GetMethod(_In_ const MI_Class *self, _In_z_ const MI_Char *name, _Out_opt_ MI_QualifierSet *qualifierSet, _Out_opt_ MI_ParameterSet *parameterSet, _Out_opt_ MI_Uint32 *index);
MI_Result MI_CALL impl_MI_ClassFT_GetParentClassName(_In_ const MI_Class *self, _Outptr_result_maybenull_z_ const MI_Char **name);
MI_Result MI_CALL impl_MI_ClassFT_GetParentClass(_In_ const MI_Class *self, _Outptr_ MI_Class **parentClass);
MI_Result MI_CALL impl_MI_ClassFT_Delete(_Inout_ MI_Class* self);
MI_Result MI_CALL impl_MI_ClassFT_Clone(_In_ const MI_Class* self, _Outptr_ MI_Class** newClass);
MI_Result MI_CALL impl_MI_ParameterSetFT_GetMethodReturnType(_In_ const MI_ParameterSet *self, _Out_ MI_Type *returnType, _Out_ MI_QualifierSet *qualifierSet);
MI_Result MI_CALL impl_MI_ParameterSetFT_GetParameterCount(_In_ const MI_ParameterSet *self, _Out_ MI_Uint32 *count);
MI_Result MI_CALL impl_MI_ParameterSetFT_GetParameterAt(_In_ const MI_ParameterSet *self, MI_Uint32 index, _Outptr_result_z_ const MI_Char **name, MI_Type *parameterType, _Outptr_opt_result_maybenull_z_ MI_Char **referenceClass, _Out_ MI_QualifierSet *qualifierSet);
MI_Result MI_CALL impl_MI_ParameterSetFT_GetParameter(_In_ const MI_ParameterSet *self, _In_z_ const MI_Char *name, _Out_ MI_Type *parameterType, _Outptr_opt_result_maybenull_z_ MI_Char **referenceClass, _Out_ MI_QualifierSet *qualifierSet, _Out_ MI_Uint32 *index);
static MI_Result MI_CALL stub_MI_PropertySetFT_GetElementCount(_In_ const MI_PropertySet* self, _Out_ MI_Uint32* count) { mi_trace("MI_PropertySetFT_GetElementCount"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_PropertySetFT_ContainsElement(_In_ const MI_PropertySet* self, _In_z_ const MI_Char* name, _Out_ MI_Boolean* flag) { mi_trace("MI_PropertySetFT_ContainsElement"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_PropertySetFT_AddElement(_Inout_ MI_PropertySet* self, _In_z_ const MI_Char* name) { mi_trace("MI_PropertySetFT_AddElement"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_PropertySetFT_GetElementAt(_In_ const MI_PropertySet* self, MI_Uint32 index, _Outptr_result_z_ const MI_Char** name) { mi_trace("MI_PropertySetFT_GetElementAt"); if (name) *name = 0; return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_PropertySetFT_Clear(_Inout_ MI_PropertySet* self) { mi_trace("MI_PropertySetFT_Clear"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_PropertySetFT_Destruct(_Inout_ MI_PropertySet* self) { mi_trace("MI_PropertySetFT_Destruct"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_PropertySetFT_Delete(_Inout_ MI_PropertySet* self) { mi_trace("MI_PropertySetFT_Delete"); return MI_RESULT_NOT_SUPPORTED; }
static MI_Result MI_CALL stub_MI_PropertySetFT_Clone(_In_ const MI_PropertySet* self, _Outptr_ MI_PropertySet** newPropertySet) { mi_trace("MI_PropertySetFT_Clone"); if (newPropertySet) *newPropertySet = 0; return MI_RESULT_NOT_SUPPORTED; }
MI_Result MI_CALL impl_MI_QualifierSetFT_GetQualifierCount(_In_ const MI_QualifierSet *self, _Out_ MI_Uint32 *count);
MI_Result MI_CALL impl_MI_QualifierSetFT_GetQualifierAt(_In_ const MI_QualifierSet *self, MI_Uint32 index, _Outptr_result_z_ const MI_Char **name, _Out_ MI_Type *qualifierType, _Out_ MI_Uint32 *qualifierFlags, _Out_ MI_Value *qualifierValue);
MI_Result MI_CALL impl_MI_QualifierSetFT_GetQualifier(_In_ const MI_QualifierSet *self, _In_z_ const MI_Char *name, _Out_ MI_Type *qualifierType, _Out_ MI_Uint32 *qualifierFlags, _Out_ MI_Value *qualifierValue, _Out_ MI_Uint32 *index);

const MI_ApplicationFT mi_tbl_MI_ApplicationFT = {
    impl_MI_ApplicationFT_Close,
    impl_MI_ApplicationFT_NewSession,
    stub_MI_ApplicationFT_NewHostedProvider,
    impl_MI_ApplicationFT_NewInstance,
    impl_MI_ApplicationFT_NewDestinationOptions,
    impl_MI_ApplicationFT_NewOperationOptions,
    stub_MI_ApplicationFT_NewSubscriptionDeliveryOptions,
    stub_MI_ApplicationFT_NewSerializer,
    stub_MI_ApplicationFT_NewDeserializer,
    stub_MI_ApplicationFT_NewInstanceFromClass,
    stub_MI_ApplicationFT_NewClass
};
const MI_SessionFT mi_tbl_MI_SessionFT = {
    impl_MI_SessionFT_Close,
    impl_MI_SessionFT_GetApplication,
    impl_MI_SessionFT_GetInstance,
    impl_MI_SessionFT_ModifyInstance,
    impl_MI_SessionFT_CreateInstance,
    impl_MI_SessionFT_DeleteInstance,
    impl_MI_SessionFT_Invoke,
    impl_MI_SessionFT_EnumerateInstances,
    impl_MI_SessionFT_QueryInstances,
    impl_MI_SessionFT_AssociatorInstances,
    impl_MI_SessionFT_ReferenceInstances,
    impl_MI_SessionFT_Subscribe,
    impl_MI_SessionFT_GetClass,
    impl_MI_SessionFT_EnumerateClasses,
    impl_MI_SessionFT_TestConnection
};
const MI_OperationFT mi_tbl_MI_OperationFT = {
    impl_MI_OperationFT_Close,
    impl_MI_OperationFT_Cancel,
    impl_MI_OperationFT_GetSession,
    impl_MI_OperationFT_GetInstance,
    stub_MI_OperationFT_GetIndication,
    impl_MI_OperationFT_GetClass
};
const MI_HostedProviderFT mi_tbl_MI_HostedProviderFT = {
    stub_MI_HostedProviderFT_Close,
    stub_MI_HostedProviderFT_GetApplication
};
const MI_SerializerFT mi_tbl_MI_SerializerFT = {
    stub_MI_SerializerFT_Close,
    stub_MI_SerializerFT_SerializeClass,
    stub_MI_SerializerFT_SerializeInstance
};
const MI_DeserializerFT mi_tbl_MI_DeserializerFT = {
    stub_MI_DeserializerFT_Close,
    stub_MI_DeserializerFT_DeserializeClass,
    stub_MI_DeserializerFT_Class_GetClassName,
    stub_MI_DeserializerFT_Class_GetParentClassName,
    stub_MI_DeserializerFT_DeserializeInstance,
    stub_MI_DeserializerFT_Instance_GetClassName
};
const MI_SubscriptionDeliveryOptionsFT mi_tbl_MI_SubscriptionDeliveryOptionsFT = {
    stub_MI_SubscriptionDeliveryOptionsFT_SetString,
    stub_MI_SubscriptionDeliveryOptionsFT_SetNumber,
    stub_MI_SubscriptionDeliveryOptionsFT_SetDateTime,
    stub_MI_SubscriptionDeliveryOptionsFT_SetInterval,
    stub_MI_SubscriptionDeliveryOptionsFT_AddCredentials,
    stub_MI_SubscriptionDeliveryOptionsFT_Delete,
    stub_MI_SubscriptionDeliveryOptionsFT_GetString,
    stub_MI_SubscriptionDeliveryOptionsFT_GetNumber,
    stub_MI_SubscriptionDeliveryOptionsFT_GetDateTime,
    stub_MI_SubscriptionDeliveryOptionsFT_GetInterval,
    stub_MI_SubscriptionDeliveryOptionsFT_GetOptionCount,
    stub_MI_SubscriptionDeliveryOptionsFT_GetOptionAt,
    stub_MI_SubscriptionDeliveryOptionsFT_GetOption,
    stub_MI_SubscriptionDeliveryOptionsFT_GetCredentialsCount,
    stub_MI_SubscriptionDeliveryOptionsFT_GetCredentialsAt,
    stub_MI_SubscriptionDeliveryOptionsFT_GetCredentialsPasswordAt,
    stub_MI_SubscriptionDeliveryOptionsFT_Clone
};
const MI_DestinationOptionsFT mi_tbl_MI_DestinationOptionsFT = {
    stub_MI_DestinationOptionsFT_Delete,
    stub_MI_DestinationOptionsFT_SetString,
    stub_MI_DestinationOptionsFT_SetNumber,
    stub_MI_DestinationOptionsFT_AddCredentials,
    stub_MI_DestinationOptionsFT_GetString,
    stub_MI_DestinationOptionsFT_GetNumber,
    stub_MI_DestinationOptionsFT_GetOptionCount,
    stub_MI_DestinationOptionsFT_GetOptionAt,
    stub_MI_DestinationOptionsFT_GetOption,
    stub_MI_DestinationOptionsFT_GetCredentialsCount,
    stub_MI_DestinationOptionsFT_GetCredentialsAt,
    stub_MI_DestinationOptionsFT_GetCredentialsPasswordAt,
    stub_MI_DestinationOptionsFT_Clone,
    stub_MI_DestinationOptionsFT_SetInterval,
    stub_MI_DestinationOptionsFT_GetInterval
};
const MI_OperationOptionsFT mi_tbl_MI_OperationOptionsFT = {
    stub_MI_OperationOptionsFT_Delete,
    stub_MI_OperationOptionsFT_SetString,
    stub_MI_OperationOptionsFT_SetNumber,
    stub_MI_OperationOptionsFT_SetCustomOption,
    stub_MI_OperationOptionsFT_GetString,
    stub_MI_OperationOptionsFT_GetNumber,
    stub_MI_OperationOptionsFT_GetOptionCount,
    stub_MI_OperationOptionsFT_GetOptionAt,
    stub_MI_OperationOptionsFT_GetOption,
    stub_MI_OperationOptionsFT_GetEnabledChannels,
    stub_MI_OperationOptionsFT_Clone,
    stub_MI_OperationOptionsFT_SetInterval,
    stub_MI_OperationOptionsFT_GetInterval
};
const MI_UtilitiesFT mi_tbl_MI_UtilitiesFT = {
    stub_MI_UtilitiesFT_MapErrorToMiErrorCategory,
    stub_MI_UtilitiesFT_CimErrorFromErrorCode
};
const MI_InstanceFT mi_tbl_MI_InstanceFT = {
    impl_MI_InstanceFT_Clone,
    impl_MI_InstanceFT_Destruct,
    impl_MI_InstanceFT_Delete,
    stub_MI_InstanceFT_IsA,
    impl_MI_InstanceFT_GetClassName,
    impl_MI_InstanceFT_SetNameSpace,
    impl_MI_InstanceFT_GetNameSpace,
    impl_MI_InstanceFT_GetElementCount,
    impl_MI_InstanceFT_AddElement,
    impl_MI_InstanceFT_SetElement,
    impl_MI_InstanceFT_SetElementAt,
    impl_MI_InstanceFT_GetElement,
    impl_MI_InstanceFT_GetElementAt,
    impl_MI_InstanceFT_ClearElement,
    impl_MI_InstanceFT_ClearElementAt,
    impl_MI_InstanceFT_GetServerName,
    impl_MI_InstanceFT_SetServerName,
    impl_MI_InstanceFT_GetClass
};
const MI_ClassFT mi_tbl_MI_ClassFT = {
    impl_MI_ClassFT_GetClassName,
    impl_MI_ClassFT_GetNameSpace,
    impl_MI_ClassFT_GetServerName,
    impl_MI_ClassFT_GetElementCount,
    impl_MI_ClassFT_GetElement,
    impl_MI_ClassFT_GetElementAt,
    impl_MI_ClassFT_GetClassQualifierSet,
    impl_MI_ClassFT_GetMethodCount,
    impl_MI_ClassFT_GetMethodAt,
    impl_MI_ClassFT_GetMethod,
    impl_MI_ClassFT_GetParentClassName,
    impl_MI_ClassFT_GetParentClass,
    impl_MI_ClassFT_Delete,
    impl_MI_ClassFT_Clone
};
const MI_ParameterSetFT mi_tbl_MI_ParameterSetFT = {
    impl_MI_ParameterSetFT_GetMethodReturnType,
    impl_MI_ParameterSetFT_GetParameterCount,
    impl_MI_ParameterSetFT_GetParameterAt,
    impl_MI_ParameterSetFT_GetParameter
};
const MI_PropertySetFT mi_tbl_MI_PropertySetFT = {
    stub_MI_PropertySetFT_GetElementCount,
    stub_MI_PropertySetFT_ContainsElement,
    stub_MI_PropertySetFT_AddElement,
    stub_MI_PropertySetFT_GetElementAt,
    stub_MI_PropertySetFT_Clear,
    stub_MI_PropertySetFT_Destruct,
    stub_MI_PropertySetFT_Delete,
    stub_MI_PropertySetFT_Clone
};
const MI_QualifierSetFT mi_tbl_MI_QualifierSetFT = {
    impl_MI_QualifierSetFT_GetQualifierCount,
    impl_MI_QualifierSetFT_GetQualifierAt,
    impl_MI_QualifierSetFT_GetQualifier
};
