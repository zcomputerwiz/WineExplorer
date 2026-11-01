/*
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned failures;

static void check(const char *id, BOOL ok)
{
    printf("ASSERT %s %s\n", id, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

static void api_error(const char *api)
{
    printf("ERROR %s error=%lu\n", api, GetLastError());
    ++failures;
}

static void *token_info(HANDLE token, TOKEN_INFORMATION_CLASS type)
{
    DWORD size = 0;
    void *buffer;

    GetTokenInformation(token, type, NULL, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
    {
        api_error("GetTokenInformation(size)");
        return NULL;
    }
    if (!(buffer = malloc(size)))
    {
        printf("ERROR malloc size=%lu\n", size);
        ++failures;
        return NULL;
    }
    if (!GetTokenInformation(token, type, buffer, size, &size))
    {
        api_error("GetTokenInformation(data)");
        free(buffer);
        return NULL;
    }
    return buffer;
}

static PSECURITY_DESCRIPTOR object_sd(HANDLE token)
{
    SECURITY_INFORMATION info = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION;
    DWORD size = 0;
    PSECURITY_DESCRIPTOR sd;

    GetKernelObjectSecurity(token, info, NULL, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
    {
        api_error("GetKernelObjectSecurity(size)");
        return NULL;
    }
    if (!(sd = malloc(size)))
    {
        printf("ERROR malloc descriptor size=%lu\n", size);
        ++failures;
        return NULL;
    }
    if (!GetKernelObjectSecurity(token, info, sd, size, &size))
    {
        api_error("GetKernelObjectSecurity(data)");
        free(sd);
        return NULL;
    }
    return sd;
}

static BOOL contains_sid(PACL acl, PSID sid)
{
    DWORD i;
    if (!acl) return FALSE;
    for (i = 0; i < acl->AceCount; ++i)
    {
        ACCESS_ALLOWED_ACE *ace;
        if (!GetAce(acl, i, (void **)&ace))
        {
            api_error("GetAce");
            return FALSE;
        }
        if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
            EqualSid(&ace->SidStart, sid)) return TRUE;
    }
    return FALSE;
}

static PSECURITY_DESCRIPTOR make_sd(const char *user, const char *marker, BOOL low)
{
    char text[512];
    PSECURITY_DESCRIPTOR sd = NULL;
    int n = snprintf(text, sizeof(text), "D:(A;;GA;;;%s)(A;;GR;;;%s)%s",
                     user, marker, low ? "S:(ML;;NW;;;LW)" : "");
    if (n < 0 || (size_t)n >= sizeof(text))
    {
        printf("ERROR descriptor string overflow\n");
        ++failures;
        return NULL;
    }
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(text, SDDL_REVISION_1, &sd, NULL))
        api_error("ConvertStringSecurityDescriptorToSecurityDescriptor");
    return sd;
}

static BOOL inspect(HANDLE token, const char *name, PSID source_marker, PSID supplied_marker,
                    PSID default_marker, BOOL source_expected, BOOL supplied_expected,
                    DWORD expected_integrity, DWORD expected_object_integrity,
                    TOKEN_TYPE expected_type, BOOL inherited, PSID process_user)
{
    PSECURITY_DESCRIPTOR sd = object_sd(token);
    TOKEN_DEFAULT_DACL *default_dacl = token_info(token, TokenDefaultDacl);
    TOKEN_MANDATORY_LABEL *integrity = token_info(token, TokenIntegrityLevel);
    TOKEN_TYPE *type = token_info(token, TokenType);
    PACL dacl, sacl;
    PSID owner, group;
    BOOL present, defaulted;
    DWORD flags, i, object_integrity = 0;
    char id[160];
    BOOL ok = FALSE;

    if (!sd || !default_dacl || !integrity || !type) goto done;
    if (!GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) ||
        !GetSecurityDescriptorSacl(sd, &present, &sacl, &defaulted) ||
        !GetSecurityDescriptorOwner(sd, &owner, &defaulted) ||
        !GetSecurityDescriptorGroup(sd, &group, &defaulted) ||
        !GetHandleInformation(token, &flags))
    {
        api_error("InspectDescriptorOrHandle");
        goto done;
    }
    if (sacl)
        for (i = 0; i < sacl->AceCount; ++i)
        {
            SYSTEM_MANDATORY_LABEL_ACE *ace;
            PSID sid;
            if (!GetAce(sacl, i, (void **)&ace))
            {
                api_error("GetAce(label)");
                goto done;
            }
            if (ace->Header.AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;
            sid = &ace->SidStart;
            object_integrity = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
        }
    printf("CASE %s type=%u source_object_marker=%u supplied_object_marker=%u "
           "default_marker_in_object=%u default_marker_in_token_state=%u "
           "token_integrity=0x%lx object_integrity=0x%lx owner_is_process_user=%u "
           "owner_present=%u group_present=%u inherit=%u\n",
           name, (unsigned)*type, contains_sid(dacl, source_marker),
           contains_sid(dacl, supplied_marker), contains_sid(dacl, default_marker),
           contains_sid(default_dacl->DefaultDacl, default_marker),
           *GetSidSubAuthority(integrity->Label.Sid, *GetSidSubAuthorityCount(integrity->Label.Sid) - 1),
           object_integrity, owner && EqualSid(owner, process_user), owner != NULL,
           group != NULL, !!(flags & HANDLE_FLAG_INHERIT));
#define VERIFY(suffix, expr) do { snprintf(id, sizeof(id), "%s.%s", name, suffix); check(id, (expr)); } while (0)
    VERIFY("source_object_marker", contains_sid(dacl, source_marker) == source_expected);
    VERIFY("supplied_object_marker", contains_sid(dacl, supplied_marker) == supplied_expected);
    VERIFY("default_marker_not_object", !contains_sid(dacl, default_marker));
    VERIFY("default_marker_token_state", contains_sid(default_dacl->DefaultDacl, default_marker));
    VERIFY("object_marker_not_token_state", !contains_sid(default_dacl->DefaultDacl, source_marker));
    VERIFY("token_integrity", *GetSidSubAuthority(integrity->Label.Sid,
           *GetSidSubAuthorityCount(integrity->Label.Sid) - 1) == expected_integrity);
    VERIFY("object_integrity", object_integrity == expected_object_integrity);
    VERIFY("token_type", *type == expected_type);
    VERIFY("owner_group_present", owner && group);
    VERIFY("handle_inheritance", !!(flags & HANDLE_FLAG_INHERIT) == inherited);
#undef VERIFY
    ok = TRUE;
done:
    free(sd);
    free(default_dacl);
    free(integrity);
    free(type);
    return ok;
}

int main(void)
{
    HANDLE process = NULL, source = NULL, duplicate;
    TOKEN_USER *user = NULL;
    TOKEN_DEFAULT_DACL *process_default = NULL;
    TOKEN_MANDATORY_LABEL *integrity = NULL;
    LPSTR user_text = NULL;
    PSID source_marker = NULL, supplied_marker = NULL, default_marker = NULL;
    PSECURITY_DESCRIPTOR source_sd = NULL, supplied_sd = NULL, default_sd = NULL, low_sd = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, FALSE};
    TOKEN_DEFAULT_DACL changed_default;
    BOOL present, defaulted;
    DWORD source_integrity;
    unsigned t, c;
    const char *cases[] = {"null_sa", "null_descriptor", "explicit_descriptor", "explicit_low_label"};

    printf("TOKEN_DUPLICATE_SD_PROBE bits=%u\n", (unsigned)(sizeof(void *) * 8));
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process))
    {
        api_error("OpenProcessToken");
        goto done;
    }
    user = token_info(process, TokenUser);
    process_default = token_info(process, TokenDefaultDacl);
    if (!user || !process_default) goto done;
    if (!ConvertSidToStringSidA(user->User.Sid, &user_text) ||
        !ConvertStringSidToSidA("S-1-5-6", &source_marker) ||
        !ConvertStringSidToSidA("S-1-5-7", &supplied_marker) ||
        !ConvertStringSidToSidA("S-1-5-8", &default_marker))
    {
        api_error("ConvertSid");
        goto done;
    }
    check("setup.creator_default_has_no_source_marker", !contains_sid(process_default->DefaultDacl, source_marker));
    check("setup.creator_default_has_no_supplied_marker", !contains_sid(process_default->DefaultDacl, supplied_marker));
    check("setup.creator_default_has_no_default_marker", !contains_sid(process_default->DefaultDacl, default_marker));
    if (failures) goto done;
    source_sd = make_sd(user_text, "S-1-5-6", FALSE);
    supplied_sd = make_sd(user_text, "S-1-5-7", FALSE);
    default_sd = make_sd(user_text, "S-1-5-8", FALSE);
    low_sd = make_sd(user_text, "S-1-5-7", TRUE);
    if (!source_sd || !supplied_sd || !default_sd || !low_sd) goto done;
    sa.lpSecurityDescriptor = source_sd;
    if (!DuplicateTokenEx(process, TOKEN_ALL_ACCESS, &sa, SecurityImpersonation, TokenPrimary, &source))
    {
        api_error("DuplicateTokenEx(source)");
        goto done;
    }
    if (!GetSecurityDescriptorDacl(default_sd, &present, &changed_default.DefaultDacl, &defaulted) ||
        !SetTokenInformation(source, TokenDefaultDacl, &changed_default, sizeof(changed_default)))
    {
        api_error("SetDisposableTokenDefaultDacl");
        goto done;
    }
    integrity = token_info(source, TokenIntegrityLevel);
    if (!integrity) goto done;
    source_integrity = *GetSidSubAuthority(integrity->Label.Sid,
                                         *GetSidSubAuthorityCount(integrity->Label.Sid) - 1);
    check("setup.source_integrity_not_low", source_integrity != SECURITY_MANDATORY_LOW_RID);
    if (!inspect(source, "source", source_marker, supplied_marker, default_marker,
                 TRUE, FALSE, source_integrity, source_integrity, TokenPrimary,
                 FALSE, user->User.Sid) || failures) goto done;
    for (t = 0; t < 2; ++t)
        for (c = 0; c < 4; ++c)
        {
            TOKEN_TYPE type = t ? TokenImpersonation : TokenPrimary;
            char name[96];
            sa.lpSecurityDescriptor = c < 2 ? NULL : c == 2 ? supplied_sd : low_sd;
            sa.bInheritHandle = c == 1;
            snprintf(name, sizeof(name), "%s.%s", t ? "impersonation" : "primary", cases[c]);
            if (!DuplicateTokenEx(source, TOKEN_ALL_ACCESS, c ? &sa : NULL,
                                  SecurityImpersonation, type, &duplicate))
            {
                printf("CASE %s ", name);
                api_error("DuplicateTokenEx");
                continue;
            }
            inspect(duplicate, name, source_marker, supplied_marker, default_marker,
                    FALSE, c >= 2, source_integrity,
                    c == 3 ? SECURITY_MANDATORY_LOW_RID : source_integrity,
                    type, c == 1, user->User.Sid);
            if (c == 3)
            {
                HANDLE next;
                snprintf(name, sizeof(name), "%s.null_after_explicit_low",
                         t ? "impersonation" : "primary");
                if (!DuplicateTokenEx(duplicate, TOKEN_ALL_ACCESS, NULL,
                                      SecurityImpersonation, type, &next))
                {
                    printf("CASE %s ", name);
                    api_error("DuplicateTokenEx(after_explicit_low)");
                }
                else
                {
                    inspect(next, name, source_marker, supplied_marker, default_marker,
                            FALSE, FALSE, source_integrity, source_integrity,
                            type, FALSE, user->User.Sid);
                    CloseHandle(next);
                }
            }
            CloseHandle(duplicate);
        }
done:
    if (source) CloseHandle(source);
    if (process) CloseHandle(process);
    free(user);
    free(process_default);
    free(integrity);
    LocalFree(user_text);
    LocalFree(source_marker);
    LocalFree(supplied_marker);
    LocalFree(default_marker);
    LocalFree(source_sd);
    LocalFree(supplied_sd);
    LocalFree(default_sd);
    LocalFree(low_sd);
    printf("SUMMARY failures=%u overall=%s\n", failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
