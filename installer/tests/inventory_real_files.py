import os

def check_dir(d):
    real_files = []
    if not os.path.exists(d):
        return real_files
    for root, dirs, files in os.walk(d):
        for f in files:
            p = os.path.join(root, f)
            if not os.path.islink(p):
                rel = os.path.relpath(p, d)
                real_files.append(rel)
    return sorted(real_files)

s32 = check_dir("/tmp/wine_branch_pfx/drive_c/windows/system32")
s64 = check_dir("/tmp/wine_branch_pfx/drive_c/windows/syswow64")

print(f"system32 real files count: {len(s32)}")
print(f"syswow64 real files count: {len(s64)}")

# Categorize
def categorize(files):
    cats = {
        "NLS tables (locale/codepage data)": [],
        "WinMD metadata (Windows Runtime metadata)": [],
        "Etc network/config (hosts, services, networks, protocols)": [],
        "Color profiles (sRGB/ICM profiles)": [],
        "Management Console MSC": [],
        "DOS/16-bit/statically created binaries": [],
        "Other": []
    }
    for f in files:
        if f.endswith(".nls"):
            cats["NLS tables (locale/codepage data)"].append(f)
        elif f.endswith(".winmd"):
            cats["WinMD metadata (Windows Runtime metadata)"].append(f)
        elif "drivers/etc" in f or "drivers\\etc" in f:
            cats["Etc network/config (hosts, services, networks, protocols)"].append(f)
        elif f.endswith(".icm"):
            cats["Color profiles (sRGB/ICM profiles)"].append(f)
        elif f.endswith(".msc"):
            cats["Management Console MSC"].append(f)
        elif f in ["ddhelp.exe", "dosx.exe", "dsound.vxd"]:
            cats["DOS/16-bit/statically created binaries"].append(f)
        else:
            cats["Other"].append(f)
    return cats

print("\n--- system32 breakdown ---")
c32 = categorize(s32)
for k, v in c32.items():
    print(f"{k}: {len(v)}")
    if k == "Other" and v:
        print("  Other files:", v)

print("\n--- syswow64 breakdown ---")
c64 = categorize(s64)
for k, v in c64.items():
    print(f"{k}: {len(v)}")
    if k == "Other" and v:
        print("  Other files:", v)
