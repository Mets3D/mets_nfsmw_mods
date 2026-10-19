## What this is and why
The [NFS MW Overhaul mod](https://www.nexusmods.com/needforspeedmostwanted2005/mods/26) is an awesome mod to make NFS: Most Wanted a genuinely challenging game (I found its balancing very good, played through it without issues), but it is no longer maintained by its original author.  
The [Redux v3 mod pack](https://www.youtube.com/watch?v=9nnmeXzm1oQ) is a lovely visual mod pack for the game.

Normally, if you try to install the Overhaul mod on top of Redux v3, the game will simply not work. That's where I come in. [NFSMW Overhaul for Redux v3.zip](NFSMW%20Overhaul%20for%20Redux%20v3.zip) is a build of the Overhaul mod intended to be installed on top of the Redux v3 mod pack.

# Changes

### Removal of ExtraOptions
The Nexus version of Overhaul bundles a copy of the [ExtraOptions](https://github.com/ExOptsTeam/NFSMWExOpts) mod. This is not included here for various reasons:
- I see no reason to bundle it in the first place.
    - The bundled config sets the traffic density to max on all presets, but these presets are only used in Custom Races, because in the campaign, the mod sets the traffic density to max on a per-race basis anyways afaict. So, I think this just resulted in taking away the customizability of traffic density in custom races, which is pointless.
- The bundled version was outdated, leading to issues according to a pinned comment on the Overhaul's Nexus page.
- The mod packs which these compatibility versions target already bundle their own version of ExtraOptions.

### Removal of Junkman nerfs
The Overhaul mod slightly nerfed the Junkman parts, which I just found unnecessary, especially because the nerfs were so small as to seem arbitrary and insignificant. These parts are so rare and so hard to come by, it feels anti-fun to nerf them.

### How conflicts were handled
Redux v3 had no true conflicts with the Overhaul mod. Inside the .bin files are just data tables, and although both mods modified said data tables, they didn't modify the same row in even a single case. This is a clean merge, which I expect to work without issues.

# What about the Rework?
[NFS MW Rework](https://www.patreon.com/cw/MWRework), unlike Redux v3, is not just a visual mod pack, but a total and complete rework of the gameplay. Unlike Redux v3, it has conflicts, it has almost exclusively conflicts, with the Overhaul mod. There is just no universe where these two mods can be merged in a meaningful way, so don't ask, and don't even try. From what I've been told, the Rework is also meant to be "much harder than vanilla", whatever that means.

<details>
<summary>AI tech mumbo jumbo</summary>

## How this was achieved
Inside the game's `GLOBAL` folder, `attributes.bin`, `FE_ATTRIB.bin` and `gameplay.bin` (plus `gameplay.lzc`, which is a copy of `gameplay.bin` in a wrapper) are databases of tuning values: car parts, police behaviour, race definitions and so on. Both mods ship their own edited copies of these files, so whichever you install last silently erases the other mod's changes. Here, the merge goes record by record instead. [Attribulator](https://github.com/NFSTools/Attribulator) converts the databases to readable YAML and back, and the two Python scripts in `scripts/` do the comparing and merging in between.

### What you need
- **Python 3** with PyYAML (`pip install pyyaml`).
- **Attribulator**, built from [this fork](https://github.com/Meno-Ajeno/Attribulator/tree/allow-arrays-larger-than-maxcount). The Overhaul has some lists that are longer than the database's declared maximum (for example 10 vehicles in a traffic pattern that is declared for 6), and stock Attribulator refuses to pack them. My fork adds `pack --allow-oversized-arrays` (this is [PR #21](https://github.com/NFSTools/Attribulator/pull/21), so you can use upstream once it is merged). Build it with `dotnet build Attribulator.sln -c Release` (it is x86), copy each plugin's build output into `Attribulator.CLI/bin/x86/Release/netcoreapp3.1/plugins/<PluginName>/`, and put `NativeLibrary.dll` (x86, from Attribulator's own release download) next to `Attribulator.CLI.exe`. If you only have .NET 5 or newer installed, set the environment variable `DOTNET_ROLL_FORWARD=LatestMajor`.
- The three game data sets: **vanilla 1.3**, **Redux v3** and **the Overhaul** (the `GLOBAL` folder from the Overhaul download).

The scripts find everything relative to the repository root, and the output goes into `Exports/` there.

### Step by step
1. **Unpack** the `GLOBAL` folder of each of the three sets, using exactly these output names (the scripts look for them):

   ```
   Attribulator.CLI.exe unpack -i "<vanilla 1.3>/GLOBAL" -o Exports/vlt/vanilla    -p MOST_WANTED -f yml
   Attribulator.CLI.exe unpack -i "<Redux v3>/GLOBAL"    -o Exports/vlt/redux      -p MOST_WANTED -f yml
   Attribulator.CLI.exe unpack -i "<Overhaul>/GLOBAL"    -o Exports/vlt/overhaul13 -p MOST_WANTED -f yml
   ```
   Each takes a few seconds and writes about 600 small YAML files (one per database class or race).

2. **(Optional) See what each mod changes.** `python scripts/vlt_changes_report.py` compares the Redux and Overhaul exports with vanilla and writes `Exports/vlt/vlt_changes.html`: a dark, filterable table of every changed value with its technical name, a plain-English description, and the vanilla, Overhaul and Redux values side by side. It also has a filter for values that both mods change. This is how I found that the two mods never change the same value. The same script can compare another mod pack: `--mod <export folder name> --label "Name" --out <folder>`.

3. **Merge.** `python scripts/vlt_merge.py` writes the merged YAML to `Exports/ReduxV3 + Overhaul/yaml` and a summary to `merge_report.txt` next to it. It starts from Redux's files and brings in what the Overhaul changed:
   - a record that only the Overhaul changed or added replaces (or is added to) Redux's version of it;
   - a record that both changed gets the Overhaul's changed fields inserted into Redux's version, and the script stops with an error if they would collide;
   - everything else stays exactly as in Redux.

   Text is copied rather than re-written, so Attribulator's formatting is not disturbed. At the end the script compares every value of the result with what the two mods say it should be. By default the `junkman` class is left as in Redux (that is how the Junkman nerfs were removed); `--exclude <class> ...` changes that list. It deletes and recreates `Exports/ReduxV3 + Overhaul/yaml` each time, so don't keep your own edits in there.

4. **Pack** the merged YAML back into databases:
   ```
   Attribulator.CLI.exe pack -i "Exports/ReduxV3 + Overhaul/yaml" -o "Exports/ReduxV3 + Overhaul/packed" -p MOST_WANTED --allow-oversized-arrays
   ```
   This creates `main/attributes.bin`, `main/fe_attrib.bin`, `main/gameplay.bin` and `main/gameplay.lzc`.

5. **Install**: copy those four files into the `GLOBAL` folder of the game (`fe_attrib.bin` is called `FE_ATTRIB.bin` there). They are the same four files in this mod's `GLOBAL` folder. Everything else in `GLOBAL` stays as Redux v3 has it.

### How well it was checked
- I played the merged files in-game (a toll booth race) and the game ran fine.
- Unpacking the packed result again gives back the same data in 10249 of 10254 records. In the other five, four empty references come back as empty text instead of null, and one Redux-only text value with a non-ASCII character comes back with a `?`.
- The packed files are about 3% bigger than the original mods' files (`gameplay.bin` is 2.16 MB against 2.10 MB). Packing is also not byte-for-byte repeatable between runs, so don't be surprised if your files differ from mine by a byte or two.

</details>