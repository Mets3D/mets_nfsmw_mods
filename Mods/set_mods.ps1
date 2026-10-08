# Switch which .asi plugins are active in the GameTest copy.
#   all   = your original mod set only
#   hello = only nfsmw_hello (isolated; game renders no 3D without the other mods)
#   both  = original mod set + nfsmw_hello (blinking bar)
#   cam   = original mod set + nfsmw_cam (camera/tuning-screen probe, read-only)
#   none  = no plugins at all
param([Parameter(Mandatory)][ValidateSet('all', 'hello', 'both', 'cam', 'none')][string]$Mode)
$game = 'E:\Git\NFS-MW\GameTest'
$on = "$game\scripts"; $off = "$game\scripts_disabled"
$ours = 'nfsmw_hello.asi', 'nfsmw_cam.asi'
New-Item -ItemType Directory $off -Force | Out-Null
Get-ChildItem "$on\*.asi" | Move-Item -Destination $off -Force
if ($Mode -in 'all', 'both', 'cam') {
    Get-ChildItem "$off\*.asi" | Where-Object { $ours -notcontains $_.Name } | Move-Item -Destination $on -Force
}
$want = switch ($Mode) { 'hello' { 'nfsmw_hello.asi' } 'both' { 'nfsmw_hello.asi' } 'cam' { 'nfsmw_cam.asi' } default { @() } }
foreach ($n in $want) { Copy-Item "$PSScriptRoot\build\$n" "$on\$n" -Force }
# nfsmw_cam's texture pack (its Options icon), rebuilt from Mods/cam/OPTIONS_CAMERA.png on every deploy.
if ($Mode -eq 'cam') {
    python "$PSScriptRoot\..\Tools\tpk_build.py" "$PSScriptRoot\build\nfsmw_cam.tpk" NFSMW_CAM "$PSScriptRoot\cam\OPTIONS_CAMERA.png"
    Copy-Item "$PSScriptRoot\build\nfsmw_cam.tpk" "$on\nfsmw_cam.tpk" -Force
} else {
    Remove-Item "$on\nfsmw_cam.tpk" -ErrorAction SilentlyContinue
}
foreach ($n in $ours) { Remove-Item "$off\$n" -ErrorAction SilentlyContinue }
"{0}: active = {1}" -f $Mode, ((Get-ChildItem "$on\*.asi").Name -join ', ')
