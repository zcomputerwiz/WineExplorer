$vbox = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
$vm = "WineExplorer-Install-Test"
$evidence = "D:\Antigravity\WineExplorer\installer\test-evidence\F8"

if (!(Test-Path $evidence)) {
    New-Item -ItemType Directory -Force -Path $evidence | Out-Null
}

$initialInfo = & $vbox showvminfo $vm --machinereadable
if (($initialInfo -join "`n") -notmatch 'VMState="running"') {
    Write-Host "Starting $vm headless..."
    & $vbox startvm $vm --type headless
} else {
    Write-Host "$vm is already running. Monitoring..."
}

$start = Get-Date
$maxMinutes = 15

while (((Get-Date) - $start).TotalMinutes -lt $maxMinutes) {
    Start-Sleep -Seconds 15
    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    $shotName = "f8_progress_{0:D3}s.png" -f $elapsed
    $shotPath = Join-Path $evidence $shotName
    & $vbox controlvm $vm screenshotpng $shotPath
    Write-Host "[SHOT] $shotName saved"

    # Check if VM rebooted or guest ejected CD; detach DVD medium so VM boots target disk
    if ($elapsed -ge 60 -and -not $ejected) {
        $info = & $vbox showvminfo $vm --machinereadable
        if (($info -join "`n") -match 'IDE-IsEjected-0-0="on"') {
            Write-Host "Guest ejected CD, detaching DVD drive to boot disk directly..."
            & $vbox storageattach $vm --storagectl IDE --port 0 --device 0 --type dvddrive --medium none
            $ejected = $true
        }
    }

    $info = & $vbox showvminfo $vm --machinereadable
    if (($info -join "`n") -notmatch 'VMState="running"') {
        Write-Host "VM is no longer running."
        break
    }
}

Write-Host "Unattended monitoring script finished."
