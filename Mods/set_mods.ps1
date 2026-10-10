# Switch which .asi plugins are active in a copy of the game (default: the GameTest copy).
#   all  = your original mod set only
#   ours = original mod set + our mods: custom_menus (the Options menu framework), camera_settings and
#          controller_support, with their texture packs
#   none = no plugins at all
# -Game picks another install, for example the vanilla 1.3 one, and -Only which of our mods go there (folder names of
# Mods/, default the first three; dev_tools is only added when asked for): set_mods.ps1 -Mode ours -Game '...\1.3' -Only controller_support
param(
    [Parameter(Mandatory)][ValidateSet('all', 'ours', 'none')][string]$Mode,
    [string]$Game = 'E:\Git\NFS-MW\Game Versions\GameTest',
    [ValidateSet('custom_menus', 'camera_settings', 'controller_support', 'dev_tools')][string[]]$Only = @('custom_menus', 'camera_settings', 'controller_support')
)
$game = $Game
$on = "$game\scripts"; $off = "$game\scripts_disabled"
$data = "$game\scripts\mets_mods"  # settings, logs and texture packs of our mods
New-Item -ItemType Directory $data -Force | Out-Null
Remove-Item "$game\nfsmw_mods" -Recurse -ErrorAction SilentlyContinue  # where the previous layout kept them
$all = 'nfsmw_custom_menus.asi', 'nfsmw_camera_settings.asi', 'nfsmw_controller_support.asi', 'nfsmw_dev_tools.asi'
$ours = $all | Where-Object { $Only -contains ($_ -replace '^nfsmw_', '' -replace '\.asi$', '') }
# Texture packs: <name>.tpk in the data folder, built from the PNGs of a folder in Mods/ (the texture's name is the file's)
$packs = @(
    @{ Mod = 'camera_settings';    Name = 'nfsmw_camera_settings';    Pngs = 'camera_settings\OPTIONS_*.png' },     # the Options > Camera icon
    @{ Mod = 'controller_support'; Name = 'nfsmw_controller_support'; Pngs = 'controller_support\OPTIONS_*.png' }   # the Options > Rumble icon
)
# The controller button pictures are one texture per prompt of buttons\prompts.ini (Tools\button_pack.py); the plugin reads
# that file too, so it is copied next to the pack.
$buttons = "$PSScriptRoot\controller_support\buttons"
$buttonsPack = 'nfsmw_controller_buttons'
New-Item -ItemType Directory $off -Force | Out-Null
Get-ChildItem "$on\*.asi" | Move-Item -Destination $off -Force
if ($Mode -in 'all', 'ours') {
    Get-ChildItem "$off\*.asi" | Where-Object { $all -notcontains $_.Name } | Move-Item -Destination $on -Force
}
if ($Mode -eq 'ours') {
    foreach ($n in $ours) { Copy-Item "$PSScriptRoot\build\$n" "$on\$n" -Force }
    foreach ($pack in $packs) {
        $name = $pack.Name
        $images = @(Get-ChildItem "$PSScriptRoot\$($pack.Pngs)" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        if ($Only -contains $pack.Mod -and $images.Count -gt 0) {
            python "$PSScriptRoot\..\Tools\tpk_build.py" "$PSScriptRoot\build\$name.tpk" ($name.ToUpper()) $images | Select-Object -Last 1
            Copy-Item "$PSScriptRoot\build\$name.tpk" "$data\$name.tpk" -Force
        } else {
            Remove-Item "$data\$name.tpk" -ErrorAction SilentlyContinue
        }
    }
    if ($Only -contains 'controller_support') {
        python "$PSScriptRoot\..\Tools\button_pack.py" $buttons "$PSScriptRoot\build\$buttonsPack.tpk" ($buttonsPack.ToUpper()) | Select-Object -Last 1
        Copy-Item "$PSScriptRoot\build\$buttonsPack.tpk" "$data\$buttonsPack.tpk" -Force
        Copy-Item "$buttons\prompts.ini" "$data\$buttonsPack.ini" -Force
    } else {
        Remove-Item "$data\$buttonsPack.tpk", "$data\$buttonsPack.ini" -ErrorAction SilentlyContinue
    }
} else {
    foreach ($pack in $packs) { Remove-Item "$data\$($pack.Name).tpk" -ErrorAction SilentlyContinue }
    Remove-Item "$data\$buttonsPack.tpk", "$data\$buttonsPack.ini" -ErrorAction SilentlyContinue
}
foreach ($pack in $packs) { Remove-Item "$on\$($pack.Name).tpk" -ErrorAction SilentlyContinue }  # where older versions put them
foreach ($n in $all) { Remove-Item "$off\$n" -ErrorAction SilentlyContinue }
"{0}: active = {1}" -f $Mode, ((Get-ChildItem "$on\*.asi").Name -join ', ')
