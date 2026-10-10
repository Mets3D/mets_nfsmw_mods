# Switch which .asi plugins are active in the GameTest copy.
#   all  = your original mod set only
#   ours = original mod set + our mods: nfsmw_custom_menus (the Options menu framework), nfsmw_camera_settings and nfsmw_controller_support, with
#          their texture packs
#   none = no plugins at all
param([Parameter(Mandatory)][ValidateSet('all', 'ours', 'none')][string]$Mode)
$game = 'E:\Git\NFS-MW\GameTest'
$on = "$game\scripts"; $off = "$game\scripts_disabled"
$data = "$game\scripts\mets_mods"  # settings, logs and texture packs of our mods
New-Item -ItemType Directory $data -Force | Out-Null
Remove-Item "$game\nfsmw_mods" -Recurse -ErrorAction SilentlyContinue  # where the previous layout kept them
$ours = 'nfsmw_custom_menus.asi', 'nfsmw_camera_settings.asi', 'nfsmw_controller_support.asi'
$packs = 'nfsmw_camera_settings', 'nfsmw_controller_support'  # each mod's Options icons: <mod>.tpk, built from Mods/<mod folder>/OPTIONS_*.png
New-Item -ItemType Directory $off -Force | Out-Null
Get-ChildItem "$on\*.asi" | Move-Item -Destination $off -Force
if ($Mode -in 'all', 'ours') {
    Get-ChildItem "$off\*.asi" | Where-Object { $ours -notcontains $_.Name } | Move-Item -Destination $on -Force
}
if ($Mode -eq 'ours') {
    foreach ($n in $ours) { Copy-Item "$PSScriptRoot\build\$n" "$on\$n" -Force }
    foreach ($pack in $packs) {
        $folder = $pack -replace '^nfsmw_', ''
        $icons = @(Get-ChildItem "$PSScriptRoot\$folder\OPTIONS_*.png" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        if ($icons.Count -gt 0) {
            python "$PSScriptRoot\..\Tools\tpk_build.py" "$PSScriptRoot\build\$pack.tpk" ($pack.ToUpper()) $icons
            Copy-Item "$PSScriptRoot\build\$pack.tpk" "$data\$pack.tpk" -Force
        } else {
            Remove-Item "$data\$pack.tpk" -ErrorAction SilentlyContinue
        }
    }
} else {
    foreach ($pack in $packs) { Remove-Item "$data\$pack.tpk" -ErrorAction SilentlyContinue }
}
foreach ($pack in $packs) { Remove-Item "$on\$pack.tpk" -ErrorAction SilentlyContinue }  # where older versions put them
foreach ($n in $ours) { Remove-Item "$off\$n" -ErrorAction SilentlyContinue }
"{0}: active = {1}" -f $Mode, ((Get-ChildItem "$on\*.asi").Name -join ', ')
