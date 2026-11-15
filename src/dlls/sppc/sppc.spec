@ stub SLCallServer
@ stub SLpAuthenticateGenuineTicketResponse
@ stub SLpBeginGenuineTicketTransaction
@ stub SLpClearActivationInProgress
@ stub SLpDepositDownlevelGenuineTicket
@ stub SLpDepositTokenActivationResponse
@ stub SLpGenerateTokenActivationChallenge
@ stub SLpGetGenuineBlob
@ stub SLpGetGenuineLocal
@ stub SLpGetLicenseAcquisitionInfo
@ stub SLpGetMSPidInformation
@ stub SLpGetMachineUGUID
@ stub SLpGetTokenActivationGrantInfo
@ stub SLpIAActivateProduct
@ stub SLpIsCurrentInstalledProductKeyDefaultKey
@ stub SLpProcessVMPipeMessage
@ stub SLpSetActivationInProgress
@ stub SLpTriggerServiceWorker
@ stub SLpVLActivateProduct
@ stdcall SLClose(ptr)
@ stdcall SLConsumeRight(ptr ptr ptr wstr ptr)
@ stub SLDepositMigrationBlob
@ stub SLDepositOfflineConfirmationId
@ stub SLDepositOfflineConfirmationIdEx
@ stub SLDepositStoreToken
@ stdcall SLFireEvent(ptr wstr ptr)
@ stub SLGatherMigrationBlob
@ stub SLGatherMigrationBlobEx
@ stub SLGenerateOfflineInstallationId
@ stub SLGenerateOfflineInstallationIdEx
@ stub SLGetActiveLicenseInfo
@ stdcall SLGetApplicationInformation(ptr ptr wstr ptr ptr ptr)
@ stdcall SLGetApplicationPolicy(ptr wstr ptr ptr ptr)
@ stdcall SLGetAuthenticationResult(ptr ptr ptr)
@ stub SLGetEncryptedPIDEx
@ stub SLGetGenuineInformation
@ stdcall SLGetInstalledProductKeyIds(ptr ptr ptr ptr)
@ stdcall SLGetLicense(ptr ptr ptr ptr)
@ stdcall SLGetLicenseFileId(ptr long ptr ptr)
@ stdcall SLGetLicenseInformation(ptr ptr wstr ptr ptr ptr)
@ stdcall SLGetLicensingStatusInformation(ptr ptr ptr wstr ptr ptr)
@ stdcall SLGetPKeyId(ptr wstr ptr long ptr ptr)
@ stdcall SLGetPKeyInformation(ptr ptr wstr ptr ptr ptr)
@ stdcall SLGetPolicyInformation(ptr wstr ptr ptr ptr)
@ stdcall SLGetPolicyInformationDWORD(ptr wstr ptr)
@ stdcall SLGetProductSkuInformation(ptr ptr wstr ptr ptr ptr)
@ stdcall SLGetSLIDList(ptr long ptr long ptr ptr)
@ stdcall SLGetServiceInformation(ptr wstr ptr ptr ptr)
@ stdcall SLInstallLicense(ptr long ptr ptr)
@ stdcall SLInstallProofOfPurchase(ptr wstr ptr long ptr ptr)
@ stub SLInstallProofOfPurchaseEx
@ stub SLIsGenuineLocalEx
@ stdcall SLLoadApplicationPolicies(ptr ptr long ptr)
@ stdcall SLOpen(ptr)
@ stdcall SLPersistApplicationPolicies(ptr ptr long)
@ stub SLPersistRTSPayloadOverride
@ stub SLReArm
@ stdcall SLRegisterEvent(ptr wstr ptr ptr)
@ stub SLRegisterPlugin
@ stdcall SLSetAuthenticationData(ptr long ptr)
@ stdcall SLSetCurrentProductKey(ptr ptr ptr)
@ stub SLSetGenuineInformation
@ stdcall SLUninstallLicense(ptr ptr)
@ stdcall SLUninstallProofOfPurchase(ptr ptr)
@ stdcall SLUnloadApplicationPolicies(ptr long)
@ stdcall SLUnregisterEvent(ptr wstr ptr ptr)
@ stub SLUnregisterPlugin
