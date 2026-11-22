/*
 * mi.dll: shared declarations
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

#ifndef MI_PRIV_H
#define MI_PRIV_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wbemcli.h>
#include "MI.h"
void mi_trace(const char *fmt, ...);
IWbemServices *mi_connect_ns(const WCHAR *ns, HRESULT *hr);
const WCHAR *mi_instance_class_name(const MI_Instance *self);
extern const MI_ParameterSetFT mi_tbl_MI_ParameterSetFT;
extern const MI_QualifierSetFT mi_tbl_MI_QualifierSetFT;
extern const MI_ApplicationFT mi_tbl_MI_ApplicationFT;
extern const MI_SessionFT mi_tbl_MI_SessionFT;
extern const MI_OperationFT mi_tbl_MI_OperationFT;
extern const MI_HostedProviderFT mi_tbl_MI_HostedProviderFT;
extern const MI_SerializerFT mi_tbl_MI_SerializerFT;
extern const MI_DeserializerFT mi_tbl_MI_DeserializerFT;
extern const MI_SubscriptionDeliveryOptionsFT mi_tbl_MI_SubscriptionDeliveryOptionsFT;
extern const MI_DestinationOptionsFT mi_tbl_MI_DestinationOptionsFT;
extern const MI_OperationOptionsFT mi_tbl_MI_OperationOptionsFT;
extern const MI_UtilitiesFT mi_tbl_MI_UtilitiesFT;
extern const MI_InstanceFT mi_tbl_MI_InstanceFT;
extern const MI_ClassFT mi_tbl_MI_ClassFT;
#endif
