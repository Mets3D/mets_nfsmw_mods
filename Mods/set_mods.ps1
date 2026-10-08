# Switch which .asi plugins are active in the GameTest copy.
#   all  = your original mod set only
#   cam  = original mod set + nfsmw_cam (and its texture pack)
#   none = no plugins at all
param([Parameter(Mandatory)][ValidateSet('all', 'cam', 'none')][string]$Mode)
$game = 'E:\Git\NFS-MW\GameTest'
$on = "$game\scripts"; $off = "$game\scripts_disabled"
$ours = 'nfsmw_cam.asi'
New-Item -ItemType Directory $off -Force | Out-Null
Get-ChildItem "$on\*.asi" | Move-Item -Destination $off -Force
if ($Mode -in 'all', 'cam') {
    Get-ChildItem "$off\*.asi" | Where-Object { $ours -notcontains $_.Name } | Move-Item -Destination $on -Force
}
# nfsmw_cam and its texture pack (the Options icon, rebuilt from Mods/cam/OPTIONS_CAMERA.png on every deploy).
if ($Mode -eq 'cam') {
    Copy-Item "$PSScriptRoot\build\nfsmw_cam.asi" "$on\nfsmw_cam.asi" -Force
    python "$PSScriptRoot\..\Tools\tpk_build.py" "$PSScriptRoot\build\nfsmw_cam.tpk" NFSMW_CAM "$PSScriptRoot\cam\OPTIONS_CAMERA.png"
    Copy-Item "$PSScriptRoot\build\nfsmw_cam.tpk" "$on\nfsmw_cam.tpk" -Force
} else {
    Remove-Item "$on\nfsmw_cam.tpk" -ErrorAction SilentlyContinue
}
foreach ($n in $ours) { Remove-Item "$off\$n" -ErrorAction SilentlyContinue }
"{0}: active = {1}" -f $Mode, ((Get-ChildItem "$on\*.asi").Name -join ', ')
