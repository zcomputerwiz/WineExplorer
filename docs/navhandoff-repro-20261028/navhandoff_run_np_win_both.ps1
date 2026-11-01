$ErrorActionPreference = 'Stop'
$dir  = 'D:\DeepThought\Projects\OpenClaude\ac_build'
Set-Location $dir
# Bring both freshly built PEs from WSL fast disk onto D: (WSL sees D: as /mnt/d).
wsl -d Debian --exec cp /home/debian/navhandoff/mojo_np.exe /mnt/d/DeepThought/Projects/OpenClaude/ac_build/mojo_np.exe
wsl -d Debian --exec cp /home/debian/navhandoff/mojo_np_multi.exe /mnt/d/DeepThought/Projects/OpenClaude/ac_build/mojo_np_multi.exe
Write-Output ("OS build: " + [Environment]::OSVersion.Version.ToString())

function Run-One($exe, $childlog, $tag) {
  Remove-Item "$dir\mojo_np_child.log" -ErrorAction SilentlyContinue
  Remove-Item "$dir\mojo_np_multi_child.log" -ErrorAction SilentlyContinue
  $t0 = Get-Date
  $out = & "$dir\$exe" 2>&1 | Out-String
  $code = $LASTEXITCODE
  $ms = ((Get-Date) - $t0).TotalMilliseconds
  Write-Output ("== $tag exit=" + $code + " wall_ms=" + [int]$ms + " ==")
  Write-Output "--- parent stdout ($tag) ---"
  Write-Output $out
  Write-Output "--- child log ($tag) ---"
  if (Test-Path "$dir\$childlog") { Get-Content "$dir\$childlog" } else { Write-Output '(no child log)' }
}

Run-One "mojo_np.exe"       "mojo_np_child.log"       "primary"
Run-One "mojo_np_multi.exe" "mojo_np_multi_child.log" "multi"
