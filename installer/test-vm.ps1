<#
.SYNOPSIS
  Manage and drive the WineExplorer-Install-Test VirtualBox VM from PowerShell.

.EXAMPLE
  .\installer\test-vm.ps1 -Action create -Firmware bios -IsoPath installer\wineexplorer.iso
  .\installer\test-vm.ps1 -Action start -Gui
  .\installer\test-vm.ps1 -Action shot -OutFile build\bios_boot.png
  .\installer\test-vm.ps1 -Action key -Key enter
  .\installer\test-vm.ps1 -Action destroy
#>

param(
    [Parameter(Position=0)]
    [ValidateSet("create", "start", "stop", "destroy", "shot", "key", "type")]
    [string]$Action = "shot",

    [string]$Firmware = "bios",
    [string]$IsoPath = "",
    [string]$OutFile = "",
    [string]$Key = "enter",
    [string]$Text = "",
    [string]$DiskPath = "",
    [switch]$Gui
)

if (-not $PSScriptRoot) {
    $PSScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
}
if (-not $IsoPath) {
    $IsoPath = Join-Path $PSScriptRoot "wineexplorer.iso"
}
if (-not $OutFile) {
    $OutFile = Join-Path $PSScriptRoot "..\build\test_vm_shot.png"
}

$VBM = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
$VM_NAME = "WineExplorer-Install-Test"
$DISK_PATH = if ($DiskPath) { $DiskPath } else { Join-Path $PSScriptRoot "test_disk_responsive.vdi" }

$ScanCodes = @{
    "enter"     = @("1c", "9c")
    "esc"       = @("01", "81")
    "f3"        = @("3d", "bd")
    "f8"        = @("42", "c2")
    "c"         = @("2e", "ae")
    "d"         = @("20", "a0")
    "r"         = @("13", "93")
    "tab"       = @("0f", "8f")
    "up"        = @("e0", "48", "e0", "c8")
    "down"      = @("e0", "50", "e0", "d0")
    "left"      = @("e0", "4b", "e0", "cb")
    "right"     = @("e0", "4d", "e0", "cd")
    "space"     = @("39", "b9")
    "pageup"    = @("e0", "49", "e0", "c9")
    "pagedown"  = @("e0", "51", "e0", "d1")
}

function Destroy-VM {
    Write-Host "Destroying $VM_NAME..."
    & $VBM controlvm $VM_NAME poweroff 2>$null
    Start-Sleep -Seconds 1
    & $VBM unregistervm $VM_NAME --delete 2>$null
    if (Test-Path $DISK_PATH) {
        Remove-Item $DISK_PATH -Force -ErrorAction SilentlyContinue
    }
}

switch ($Action) {
    "destroy" {
        Destroy-VM
    }

    "create" {
        Destroy-VM

        if (-not (Test-Path $IsoPath)) {
            Write-Error "ISO not found: $IsoPath"
            exit 1
        }

        Write-Host "Creating $VM_NAME (firmware: $Firmware)..."
        & $VBM createvm --name $VM_NAME --ostype "Debian_64" --register
        & $VBM modifyvm $VM_NAME --memory 4096 --cpus 2 --vram 64
        & $VBM modifyvm $VM_NAME --graphicscontroller "vmsvga"
        & $VBM modifyvm $VM_NAME --nic1 "nat"
        $serialLog = "$PSScriptRoot\..\build\test_vm_serial.log"
        $buildDir = Split-Path $serialLog -Parent
        if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir -Force | Out-Null }
        if (Test-Path $serialLog) { Remove-Item $serialLog -Force -ErrorAction SilentlyContinue }
        & $VBM modifyvm $VM_NAME --uart1 0x3f8 4 --uartmode1 file $serialLog

        if ($Firmware.ToLower() -eq "efi") {
            & $VBM modifyvm $VM_NAME --firmware "efi"
        } else {
            & $VBM modifyvm $VM_NAME --firmware "bios"
        }

        Write-Host "Creating 20 GB test disk at $DISK_PATH..."
        & $VBM createhd --filename $DISK_PATH --size 20480

        & $VBM storagectl $VM_NAME --name "SATA" --add sata --controller IntelAhci
        & $VBM storageattach $VM_NAME --storagectl "SATA" --port 0 --device 0 --type hdd --medium $DISK_PATH

        & $VBM storagectl $VM_NAME --name "IDE" --add ide
        & $VBM storageattach $VM_NAME --storagectl "IDE" --port 0 --device 0 --type dvddrive --medium (Resolve-Path $IsoPath).Path

        & $VBM modifyvm $VM_NAME --boot1 dvd --boot2 disk --boot3 none
        Write-Host "$VM_NAME created successfully."
    }

    "start" {
        $type = if ($Gui) { "gui" } else { "headless" }
        Write-Host "Starting $VM_NAME ($type)..."
        & $VBM startvm $VM_NAME --type $type
    }

    "stop" {
        Write-Host "Stopping $VM_NAME..."
        & $VBM controlvm $VM_NAME poweroff
    }

    "shot" {
        $outDir = Split-Path -Parent $OutFile
        if ($outDir -and -not (Test-Path $outDir)) {
            New-Item -ItemType Directory -Path $outDir -Force | Out-Null
        }
        & $VBM controlvm $VM_NAME screenshotpng $OutFile
        Write-Host "Screenshot saved: $OutFile"
    }

    "key" {
        $k = $Key.ToLower()
        if (-not $ScanCodes.ContainsKey($k)) {
            Write-Error "Unknown key: $k"
            exit 1
        }
        $codes = $ScanCodes[$k]
        if ($codes.Count -ge 2) {
            $half = [int]($codes.Count / 2)
            $make = $codes[0..($half-1)]
            $break = $codes[$half..($codes.Count-1)]
            & $VBM controlvm $VM_NAME keyboardputscancode $make
            Start-Sleep -Milliseconds 100
            & $VBM controlvm $VM_NAME keyboardputscancode $break
        } else {
            & $VBM controlvm $VM_NAME keyboardputscancode $codes
        }
    }

    "type" {
        & $VBM controlvm $VM_NAME keyboardputstring $Text
    }
}
