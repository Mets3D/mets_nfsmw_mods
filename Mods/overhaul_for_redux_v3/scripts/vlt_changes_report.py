"""HTML report of every value that Redux v3 or the Overhaul changes against vanilla 1.3 in the vault databases
(attributes.bin, fe_attrib.bin, gameplay.bin), from Attribulator YAML exports.

  1. unpack the three installs (see agent_docs/project.md, Tools/vlt-tools):
       Attribulator.CLI.exe unpack -i "<game>/GLOBAL" -o Exports/vlt/<name> -p MOST_WANTED -f yml
     for vanilla (1.3), redux (Redux v3) and overhaul13 (1.3 + Overhaul)
  2. python Mods/overhaul_for_redux_v3/scripts/vlt_changes_report.py        -> Exports/vlt/vlt_changes.html
     (other mod: --mod <export dir> --label "Rework v4" --out "Exports/Rework v4 + Overhaul")

Needs PyYAML (pip install pyyaml --target Tools/vlt-tools/pylib). Dark, high-contrast page on purpose (no white).
Descriptions come from the community pursuit documentation (Orsall's "MW Pursuit System Info", tested with VltEd) where it
covers a field ("doc") and from reading the field name otherwise ("guess").
"""
import argparse
import html
import json
import os
import re
import struct
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'vlt-tools', 'pylib'))
import yaml

LOADER = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)
ROOT = os.path.join(REPO, 'Exports', 'vlt') + os.sep
# the other mod is chosen on the command line (default Redux v3): --mod <export dir under Exports/vlt> --label <name> --out <dir>
_ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
_ap.add_argument('--mod', default='redux', help='export directory of the mod install under Exports/vlt')
_ap.add_argument('--label', default='Redux v3', help='name of that mod in the page')
_ap.add_argument('--out', default=ROOT, help='directory for vlt_changes.html')
ARGS = _ap.parse_args(None if __name__ == '__main__' else [])  # when imported by vlt_merge.py the defaults apply
LABEL = ARGS.label
SHORT = LABEL.split()[0]
SETS = {'v': 'vanilla', 'r': ARGS.mod, 'o': 'overhaul13'}


# ------------------------------------------------------------------ collecting
def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def norm(x):
    return f32(x) if isinstance(x, float) and not isinstance(x, bool) else x


def flat(prefix, v, out):
    if isinstance(v, dict):
        for k, x in v.items():
            flat(prefix + [str(k)], x, out)
    elif isinstance(v, list):
        for i, x in enumerate(v):
            flat(prefix + [f'[{i}]'], x, out)
        if not v:
            out['/'.join(prefix).replace('/[', '[') + ' (empty list)'] = '[]'
    else:
        out['/'.join(prefix).replace('/[', '[')] = norm(v)


def load_set(name):
    data = {}
    base = ROOT + name + '/main'
    for dp, dn, fn in os.walk(base):
        for f in fn:
            if not f.endswith('.yml'):
                continue
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, ROOT + name).replace('\\', '/')
            doc = yaml.load(open(p, encoding='utf-8'), Loader=LOADER)
            if not isinstance(doc, list):
                continue
            colls = {}
            for i, c in enumerate(doc):
                if isinstance(c, dict):
                    key = str(c.get('Name'))
                    colls[key if key not in colls else f'{key}#{i}'] = c
            data[rel] = colls
    return data


def collect():
    sets = {k: load_set(v) for k, v in SETS.items()}
    rows, added = [], []
    for rel in sorted(set().union(*[set(s) for s in sets.values()])):
        V, R, O = (sets[k].get(rel, {}) for k in 'vro')
        for name in sorted(set(V) | set(R) | set(O)):
            v, r, o = V.get(name), R.get(name), O.get(name)
            if v is None:
                src = r if r is not None else o
                out = {}
                flat([], src.get('Data', {}), out)
                added.append({'file': rel, 'coll': name, 'parent': src.get('ParentName'),
                              'by': ''.join(m for m, x in (('r', r), ('o', o)) if x is not None), 'values': len(out)})
                continue
            fv, fr, fo = {}, {}, {}
            flat([], v.get('Data', {}), fv)
            if r is not None:
                flat([], r.get('Data', {}), fr)
            if o is not None:
                flat([], o.get('Data', {}), fo)
            for k in set(fv) | set(fr) | set(fo):
                a = fv.get(k, None)
                b = fr.get(k, None) if r is not None else a
                c = fo.get(k, None) if o is not None else a
                if b != a or c != a:
                    rows.append({'file': rel, 'coll': name, 'parent': v.get('ParentName'), 'field': k, 'v': a, 'r': b, 'o': c})
    return rows, added


# ------------------------------------------------------------------ descriptions
# class: (group, title, what it is)
CLASSES = {
    'pursuitlevels': ('Police pursuit', 'Heat levels', 'One record per heat level (plus a default): how many cops of which type, roadblocks, how fast you can evade or get busted.'),
    'pursuitsupport': ('Police pursuit', 'Support units', 'The extra units the police call in (Rhinos, Cross, helicopter): how likely and how soon.'),
    'aivehicle': ('AI, traffic & police cars', 'AI vehicle behaviour', 'Speed limits, acceleration and bounty rewards of cars driven by the AI (cops, opponents, traffic).'),
    'trafficpattern': ('AI, traffic & police cars', 'Traffic patterns', 'Which traffic vehicles appear in an area, how often, and how fast they drive.'),
    'damagespecs': ('Damage', 'Damage specifications', 'Hit points and how much damage each part of a car takes (used for cops and racers).'),
    'junkman': ('Career & shop', 'Junkman packages', 'How much each Junkman (random performance part) package improves the car.'),
    'GAMEPLAY': ('Races & events', 'Race and event definitions', 'One record per event (race, pursuit, challenge ...): laps, time limits, rewards, opponents, traffic.'),
    'pvehicle': ('Vehicles', 'Vehicle definitions', 'The master record of each car: mass, upgrade parts it can use, links to its engine, tyres, sounds and AI settings.'),
    'engine': ('Vehicles', 'Engines', 'Engine power curve and limits.'),
    'transmission': ('Vehicles', 'Transmissions', 'Gear ratios and differential.'),
    'chassis': ('Vehicles', 'Chassis & suspension', 'Weight balance, suspension, aerodynamics.'),
    'tires': ('Vehicles', 'Tyres', 'Grip and tyre size.'),
    'brakes': ('Vehicles', 'Brakes', 'Braking force.'),
    'induction': ('Vehicles', 'Turbo / supercharger', 'Boost pressure and spool behaviour.'),
    'presetride': ('Vehicles', 'Preset rides', 'Which upgrade level of each part a ready-made car ("preset ride") gets.'),
    'ecar': ('Vehicles', 'Car model & presentation', 'Wheel positions, body motion, body kit offsets and showroom animation of the 3D model.'),
    'engineaudio': ('Sound & visuals', 'Engine sound', 'Which engine sound banks a car uses and how they are mixed over RPM.'),
    'camerainfo': ('Sound & visuals', 'Camera views', 'Chase / bumper / hood camera settings per car.'),
    'emitterdata': ('Sound & visuals', 'Particle emitters', 'Smoke, sparks and similar particle effects.'),
    'light_flares_cg': ('Sound & visuals', 'Light flares', 'The glow around headlights, tail lights and street lights.'),
    'timeofdaylighting': ('Sound & visuals', 'Time-of-day lighting', 'Sun, ambient light and fog colours for each time of day.'),
    'visuallook': ('Sound & visuals', 'Post-processing look', 'Bloom, saturation and similar screen effects per area.'),
    'frontend': ('Menus & career', 'Frontend (car lot, unlocks, showroom)', 'Car lot prices, what unlocks when, showroom camera angles, which parts are customisable.'),
}

DOC, GUESS = 'doc', 'guess'
DOC_URL = 'https://github.com/Orsall/NFSMW-Documentation-VltEd-Pursuit-Parameters/blob/main/MW%20Pursuit%20System%20Info.txt'
# (class or '*', field with [i] -> []) -> (description, source). Vector parts and Front/Rear are handled separately.
F = {
    # --- police pursuit (Orsall's documentation)
    ('pursuitlevels', 'BackupCallTimer'): ('Time before the cops call for backup.', GUESS),
    ('pursuitlevels', 'BoxinDuration'): ('Seconds the cops keep a box-in formation (the doc says this one seems to do nothing).', DOC),
    ('pursuitlevels', 'BoxinTightness'): ('How tightly cops close in around you in the box-in / rolling-block formations (barely noticeable).', DOC),
    ('pursuitlevels', 'BustSpeed'): ('Speed (km/h) below which you can be busted when a cop is within bust distance.', DOC),
    ('pursuitlevels', 'CollapseAggression'): ('How fast and aggressively cops rush in when you slow down; high values make them dumber.', DOC),
    ('pursuitlevels', 'CollapseInnerRadius'): ('Minimum distance from your car that collapsing cops take up.', DOC),
    ('pursuitlevels', 'CollapseOuterRadius'): ('Maximum distance from your car that collapsing cops take up.', DOC),
    ('pursuitlevels', 'CollapseSpeed'): ('Below this speed (km/h) cops start to "collapse" around you to bust you.', DOC),
    ('pursuitlevels', 'FullEngagementCopCount'): ('Number of cops counted in the cop counter above the bust/evade meters.', DOC),
    ('pursuitlevels', 'HeliFuelTime'): ('How long the helicopter can fly before it has to leave (seconds).', DOC),
    ('pursuitlevels', 'SearchModeHwyMPH'): ('Maximum patrol speed (mph) on highways once the cops have lost sight of you.', DOC),
    ('pursuitlevels', 'SpeedReactionTime'): ('Unclear; cops react slightly faster or slower. The doc says the effect is minimal.', DOC),
    ('pursuitlevels', 'cops/Data[]/Chance'): ('Weighted chance of this cop type being picked for the next spawn.', DOC),
    ('pursuitlevels', 'cops/Data[]/Count'): ('Maximum number of cops of this type that can spawn.', DOC),
    ('pursuitlevels', 'cops/Data[]/CopType'): ('Which police car (a pvehicle name) this entry spawns.', DOC),
    ('pursuitlevels', 'evadetimeout'): ('Cooldown time (seconds) to evade once the cops have lost you; includes the ~7 s evade timer.', DOC),
    ('pursuitlevels', 'frontLOSdistance'): ('How far ahead a cop can see you; beyond it you can evade (metres).', DOC),
    ('pursuitlevels', 'rearLOSdistance'): ('How far behind a cop can see you; beyond it you can evade (metres).', DOC),
    ('pursuitlevels', 'heliLOSdistance'): ('How far the helicopter can see you (metres).', DOC),
    ('pursuitlevels', 'roadblockhelichance'): ('Documented as unused (does nothing).', DOC),
    ('pursuitlevels', 'roadblockprobability'): ('Chance (0-100) that a roadblock spawns each time you reach a roadblock spawn point.', DOC),
    ('pursuitlevels', 'roadblockspikechance'): ('Chance that a roadblock comes with spike strips.', GUESS),
    ('pursuitsupport', 'MinimumSupportDelay'): ('Seconds into the pursuit before supports and roadblocks can spawn.', DOC),
    ('pursuitsupport', 'HeavySupportOptions/Data[]/ChanceBigSUV'): ('Chance that the heavy support is the big SUV Rhino.', GUESS),
    # --- AI vehicles (documentation)
    ('aivehicle', 'MAXIMUM_AI_SPEED'): ('Top speed of this AI car in km/h.', DOC),
    ('aivehicle', 'TopSpeedMultiplier'): ('Top speed multiplier; the doc says it cannot exceed 1 in practice and has little use.', DOC),
    ('aivehicle', 'AccelerationMultiplier'): ('Multiplies the car\'s acceleration (this one does work).', DOC),
    ('aivehicle', 'RepPointsForDestroying/Data[]'): ('Bounty (rep points) for destroying this cop type. The list index is the heat level (index 0 does nothing).', DOC),
    # --- damage
    ('damagespecs', 'HIT_POINTS'): ('Hit points of the vehicle; more means it resists direct damage longer.', DOC),
    ('damagespecs', 'HP_THRESHOLD'): ('Minimum damage a hit must deal to count at all.', DOC),
    ('damagespecs', 'DZ_*/HitPointScale'): ('Damage multiplier for this zone of the car (the name is misleading: lower = takes less damage).', DOC),
    ('damagespecs', 'DZ_*/VisualScale'): ('Multiplier for the visible damage in this zone (higher = dents more easily).', DOC),
    # --- junkman
    ('junkman', '*/Data[]/ScaleF'): ('Scale factor the package applies to its part (above 1 = stronger).', GUESS),
    # --- traffic
    ('trafficpattern', 'SpeedHighway'): ('Traffic speed on highways.', GUESS),
    ('trafficpattern', 'SpeedStreet'): ('Traffic speed on streets.', GUESS),
    ('trafficpattern', 'Vehicles/Capacity'): ('Number of vehicle entries in this traffic pattern.', GUESS),
    ('trafficpattern', 'Vehicles/Data[]/MaxInstances'): ('Maximum simultaneous cars of this type.', GUESS),
    ('trafficpattern', 'Vehicles/Data[]/Percent'): ('Share of this vehicle among traffic (percent).', GUESS),
    ('trafficpattern', 'Vehicles/Data[]/Rate'): ('How often this vehicle spawns (relative weight).', GUESS),
    ('trafficpattern', 'Vehicles/Data[]/Vehicle/ClassKey'): ('Which record type the vehicle entry points to (reference).', GUESS),
    ('trafficpattern', 'Vehicles/Data[]/Vehicle/CollectionKey'): ('Which traffic vehicle (a pvehicle name) the entry points to.', GUESS),
    # --- races and events
    ('GAMEPLAY', 'CashValue'): ('Cash reward for winning the event.', GUESS),
    ('GAMEPLAY', 'CatchUp'): ('Whether AI opponents get rubber-banding ("catch-up") in this event.', GUESS),
    ('GAMEPLAY', 'CatchUpOverride'): ('Forces catch-up on regardless of the default.', GUESS),
    ('GAMEPLAY', 'CatchUpSkill'): ('How much the opponents\' skill rises when they fall behind (rubber-banding strength).', GUESS),
    ('GAMEPLAY', 'CatchUpSpread'): ('Distance gap that triggers rubber-banding.', GUESS),
    ('GAMEPLAY', 'Children/Capacity'): ('Number of sub-events grouped under this one.', GUESS),
    ('GAMEPLAY', 'Children/Data[]/ClassKey'): ('Record type of a child event (reference).', GUESS),
    ('GAMEPLAY', 'Children/Data[]/CollectionKey'): ('Name of a child event (reference).', GUESS),
    ('GAMEPLAY', 'CopsInRace'): ('Whether the police can join the race.', GUESS),
    ('GAMEPLAY', 'ForceHeatLevel'): ('Fixes the heat level for the event.', GUESS),
    ('GAMEPLAY', 'ForceTrafficDensity'): ('Forces the traffic density for the event.', GUESS),
    ('GAMEPLAY', 'GoalEasy'): ('Target value for the easy difficulty goal.', GUESS),
    ('GAMEPLAY', 'MaxHeatLevel'): ('Highest heat level the event can reach.', GUESS),
    ('GAMEPLAY', 'NumLaps'): ('Number of laps.', GUESS),
    ('GAMEPLAY', 'Opponents/Capacity'): ('Number of AI opponents in the event.', GUESS),
    ('GAMEPLAY', 'Opponents/Data[]/ClassKey'): ('Record type of an opponent entry (reference).', GUESS),
    ('GAMEPLAY', 'Opponents/Data[]/CollectionKey'): ('Which opponent definition races in this slot (reference to another record).', GUESS),
    ('GAMEPLAY', 'PlayerCarType'): ('Restricts which car the player must use (stored as text).', GUESS),
    ('GAMEPLAY', 'Region'): ('Map region of the event.', GUESS),
    ('GAMEPLAY', 'Reputation'): ('Reputation points the event awards.', GUESS),
    ('GAMEPLAY', 'RequiredBounty'): ('Bounty needed to unlock this blacklist rival\'s race.', GUESS),
    ('GAMEPLAY', 'RequiredChallenges'): ('Number of challenge events needed to unlock it.', GUESS),
    ('GAMEPLAY', 'RequiredRacesWon'): ('Number of races won needed to unlock it.', GUESS),
    ('GAMEPLAY', 'RewardsForWinner/Data[]/CollectionKey'): ('Reward given for winning (reference).', GUESS),
    ('GAMEPLAY', 'Rotation'): ('Heading of this start-grid position (degrees).', GUESS),
    ('GAMEPLAY', 'SkillLevel'): ('AI opponents\' driving skill (0-100).', GUESS),
    ('GAMEPLAY', 'TimeBonus'): ('Time bonus in seconds (for checkpoint / timed events).', GUESS),
    ('GAMEPLAY', 'TimeLimit'): ('Time limit of the event (seconds).', GUESS),
    ('GAMEPLAY', 'TrafficLevel'): ('Traffic density in the event (percent).', GUESS),
    ('GAMEPLAY', 'TrafficPattern'): ('Which traffic pattern the event uses (reference).', GUESS),
    ('GAMEPLAY', 'UnlockRaces/Capacity'): ('Number of events this one unlocks.', GUESS),
    ('GAMEPLAY', 'UnlockRaces/Data[]/ClassKey'): ('Record type of an unlocked event (reference).', GUESS),
    ('GAMEPLAY', 'UnlockRaces/Data[]/CollectionKey'): ('An event unlocked by finishing this one (reference).', GUESS),
    # --- vehicle parts
    ('chassis', 'AERO_CG'): ('Height of the aerodynamic centre of gravity.', GUESS),
    ('chassis', 'AERO_COEFFICIENT'): ('Aerodynamic downforce coefficient.', GUESS),
    ('chassis', 'DRAG_COEFFICIENT'): ('Air drag coefficient (lower = faster top speed).', GUESS),
    ('chassis', 'FRONT_AXLE'): ('Distance from the centre of mass to the front axle (m).', GUESS),
    ('chassis', 'FRONT_WEIGHT_BIAS'): ('Share of the weight on the front axle (percent).', GUESS),
    ('chassis', 'RENDER_MOTION'): ('How much the body visibly moves on its suspension.', GUESS),
    ('chassis', 'RIDE_HEIGHT'): ('Ride height of the body.', GUESS),
    ('chassis', 'ROLL_CENTER'): ('Height of the roll centre.', GUESS),
    ('chassis', 'SHOCK_BLOWOUT'): ('Shock force at which the damper blows out.', GUESS),
    ('chassis', 'SHOCK_DIGRESSION'): ('How much damping drops at high shaft speed.', GUESS),
    ('chassis', 'SHOCK_EXT_STIFFNESS'): ('Damper stiffness when extending (rebound).', GUESS),
    ('chassis', 'SHOCK_STIFFNESS'): ('Damper stiffness when compressing.', GUESS),
    ('chassis', 'SHOCK_VALVING'): ('Damper valving (transition between slow and fast damping).', GUESS),
    ('chassis', 'SPRING_PROGRESSION'): ('How quickly the spring gets stiffer when compressed.', GUESS),
    ('chassis', 'SPRING_STIFFNESS'): ('Spring rate.', GUESS),
    ('chassis', 'SWAYBAR_STIFFNESS'): ('Anti-roll bar stiffness.', GUESS),
    ('chassis', 'TRACK_WIDTH'): ('Distance between the left and right wheels (m).', GUESS),
    ('chassis', 'TRAVEL'): ('Suspension travel.', GUESS),
    ('chassis', 'WHEEL_BASE'): ('Distance between front and rear axle (m).', GUESS),
    ('tires', 'ASPECT_RATIO'): ('Tyre sidewall height as a percent of its width.', GUESS),
    ('tires', 'DYNAMIC_GRIP'): ('Grip while sliding.', GUESS),
    ('tires', 'GRIP_SCALE'): ('Overall grip multiplier.', GUESS),
    ('tires', 'RIM_SIZE'): ('Rim diameter (inches).', GUESS),
    ('tires', 'SECTION_WIDTH'): ('Tyre width (mm).', GUESS),
    ('tires', 'STATIC_GRIP'): ('Grip while rolling without sliding.', GUESS),
    ('tires', 'STEERING'): ('Steering response of the tyres.', GUESS),
    ('tires', 'YAW_CONTROL/Data[]'): ('How strongly the car resists spinning (yaw), per speed range.', GUESS),
    ('tires', 'YAW_SPEED'): ('How fast yaw control reacts.', GUESS),
    ('brakes', 'BRAKES'): ('Braking force.', GUESS),
    ('brakes', 'BRAKE_LOCK'): ('When the wheels lock up under braking.', GUESS),
    ('brakes', 'EBRAKE'): ('Handbrake force.', GUESS),
    ('engine', 'ENGINE_BRAKING/Data[]'): ('Engine braking when off the throttle, per RPM point.', GUESS),
    ('engine', 'FLYWHEEL_MASS'): ('Flywheel mass; heavier revs up and down more slowly.', GUESS),
    ('engine', 'MAX_RPM'): ('Highest RPM.', GUESS),
    ('engine', 'RED_LINE'): ('RPM where the rev limiter starts.', GUESS),
    ('engine', 'TORQUE/Data[]'): ('Torque at one point of the RPM curve (the index is the point).', GUESS),
    ('transmission', 'CLUTCH_SLIP'): ('Clutch slip when launching.', GUESS),
    ('transmission', 'DIFFERENTIAL/Data[]'): ('Differential setting, per entry.', GUESS),
    ('transmission', 'FINAL_GEAR'): ('Final drive ratio.', GUESS),
    ('transmission', 'GEAR_RATIO/Data[]'): ('Gear ratio of one gear (the index is the gear).', GUESS),
    ('transmission', 'TORQUE_CONVERTER'): ('Torque converter strength.', GUESS),
    ('induction', 'HIGH_BOOST'): ('Boost at high RPM.', GUESS),
    ('induction', 'LOW_BOOST'): ('Boost at low RPM.', GUESS),
    ('induction', 'PSI'): ('Maximum boost pressure.', GUESS),
    ('induction', 'SPOOL'): ('How quickly the turbo builds boost.', GUESS),
    ('induction', 'SPOOL_TIME_DOWN'): ('Time for boost to fall off.', GUESS),
    ('induction', 'SPOOL_TIME_UP'): ('Time for boost to build up.', GUESS),
    ('induction', 'VACUUM'): ('Intake vacuum when off boost.', GUESS),
    ('presetride', '*'): ('Upgrade level of this part the preset ride gets.', GUESS),
    ('pvehicle', 'HandlingRating/Data[]'): ('Handling rating shown in the menus (per upgrade level).', GUESS),
    ('pvehicle', 'MASS'): ('Mass of the car (kg).', GUESS),
    ('pvehicle', 'PlayerUsable'): ('Whether the player can drive this car.', GUESS),
    ('pvehicle', 'RandomOpponent'): ('Whether it can appear as a random race opponent.', GUESS),
    ('pvehicle', 'TENSOR_SCALE'): ('Scales rotational inertia around an axis; the doc warns that it strongly changes handling.', DOC),
    ('pvehicle', '*_upgrades'): ('Number of upgrade levels this car has for this part.', GUESS),
    ('pvehicle', '*/Capacity'): ('Number of entries in this list.', GUESS),
    ('pvehicle', '*/ClassKey'): ('Record type of a linked part set (reference).', GUESS),
    ('pvehicle', '*/CollectionKey'): ('Name of the part set the car links to for this slot (reference).', GUESS),
    ('pvehicle', 'aivehicle/ClassKey'): ('Links the car to its AI behaviour record.', DOC),
    ('pvehicle', 'aivehicle/CollectionKey'): ('Links the car to its AI behaviour record.', DOC),
    ('pvehicle', 'ShiftSND/Data[]/UpgradeLevel'): ('Upgrade level at which this shift sound is used.', GUESS),
    ('pvehicle', 'TurboSND/Data[]/UpgradeLevel'): ('Upgrade level at which this turbo sound is used.', GUESS),
    ('ecar', 'BodyDive/DegPerG'): ('Visible nose-dive angle per g of braking.', GUESS),
    ('ecar', 'BodyRoll/DegPerG'): ('Visible body roll per g of cornering.', GUESS),
    ('ecar', 'BodySquat/DegPerG'): ('Visible rear squat per g of acceleration.', GUESS),
    ('ecar', 'CamberFront'): ('Front wheel camber.', GUESS),
    ('ecar', 'CamberRear'): ('Rear wheel camber.', GUESS),
    ('ecar', 'EngineRev'): ('Engine rev setting in the showroom.', GUESS),
    ('ecar', 'FECompressions/Data[]'): ('Suspension compression shown in the menus.', GUESS),
    ('ecar', 'KitWheelOffsetFront/Data[]'): ('Front wheel position offset per body kit.', GUESS),
    ('ecar', 'KitWheelOffsetRear/Data[]'): ('Rear wheel position offset per body kit.', GUESS),
    ('ecar', 'NOSEffect/ClassKey'): ('Record type of the nitrous visual effect (reference).', GUESS),
    ('ecar', 'NOSEffect/CollectionKey'): ('Which nitrous flame effect the car uses (reference).', GUESS),
    ('ecar', 'ReflectionOffset'): ('Offset of the paint reflection.', GUESS),
    ('ecar', 'RideHeight'): ('Visual ride height.', GUESS),
    ('ecar', 'TireOffsets/Data[]/*'): ('Position / size of one wheel (X forward-back, Y side, Z height, W width).', GUESS),
    ('ecar', 'TireSkidWidth/Data[]'): ('Width of the skid marks.', GUESS),
    ('ecar', 'WheelWell'): ('Wheel well setting.', GUESS),
    ('ecar', '3612087349/*'): ('Unnamed field (hash 3612087349): a link to a camera record.', GUESS),
    ('camerainfo', 'ANGLE/Data[]'): ('Camera pitch angle, one entry per camera view.', GUESS),
    ('camerainfo', 'FOV/Data[]'): ('Camera field of view per view.', GUESS),
    ('camerainfo', 'HEIGHT/Data[]'): ('Camera height per view.', GUESS),
    ('camerainfo', 'LAG/Data[]'): ('How far the camera lags behind the car per view.', GUESS),
    ('camerainfo', 'SELECTABLE/Data[]'): ('Whether the player can select this camera view.', GUESS),
    ('camerainfo', 'STIFFNESS/Data[]'): ('How stiffly the camera follows per view.', GUESS),
    ('engineaudio', 'AEMS*'): ('Engine sound mixing (AEMS bank): how the sound layers blend over RPM.', GUESS),
    ('engineaudio', 'DECEL_*'): ('Engine sound mixing when off the throttle.', GUESS),
    ('engineaudio', 'GIN*'): ('Engine sound mixing (Ginsu bank): acceleration / deceleration loops.', GUESS),
    ('engineaudio', 'Ginsu*'): ('Engine sound mixing (Ginsu bank).', GUESS),
    ('engineaudio', 'BankName*'): ('Which sound bank the car uses.', GUESS),
    ('engineaudio', 'CarID'): ('Sound ID of the car.', GUESS),
    ('engineaudio', 'Filename_*'): ('Sound file used for this engine loop.', GUESS),
    ('engineaudio', 'Master_Vol'): ('Overall engine volume.', GUESS),
    ('engineaudio', 'MaxRPM'): ('Highest RPM of the sound range.', GUESS),
    ('engineaudio', 'MinRPM'): ('Lowest RPM of the sound range.', GUESS),
    ('engineaudio', 'MaybeV8'): ('Engine type flag (V8 layout, unconfirmed).', GUESS),
    ('engineaudio', 'SweetBank/Data[]/Value'): ('Sound bank for the "sweet spot" sounds.', GUESS),
    ('engineaudio', 'Tranny'): ('Transmission sound flag.', GUESS),
    ('engineaudio', 'Vol_ShiftSweets'): ('Volume of the gear-shift sounds.', GUESS),
    ('engineaudio', 'Vol_Sputters'): ('Volume of the exhaust sputters.', GUESS),
    ('light_flares_cg', 'MaxSize'): ('Largest size of the flare.', GUESS),
    ('light_flares_cg', 'Maxscale'): ('Largest scale of the flare.', GUESS),
    ('light_flares_cg', 'MinScale'): ('Smallest scale of the flare.', GUESS),
    ('light_flares_cg', 'MinSize'): ('Smallest size of the flare.', GUESS),
    ('light_flares_cg', 'Power'): ('Brightness of the flare.', GUESS),
    ('light_flares_cg', 'ZBias'): ('Depth offset so the flare is not hidden by the surface.', GUESS),
    ('light_flares_cg', 'colour/*'): ('Colour of the flare (X, Y, Z = red, green, blue; W = alpha).', GUESS),
    ('light_flares_cg', 'flare_texture'): ('Which flare picture is used.', GUESS),
    ('timeofdaylighting', 'AmbientColour'): ('Colour of the ambient (fill) light, X Y Z = red green blue.', GUESS),
    ('timeofdaylighting', 'CarSpecScale'): ('Strength of the specular highlights on cars.', GUESS),
    ('timeofdaylighting', 'DiffuseColour'): ('Colour of the main sun light on surfaces, X Y Z = red green blue.', GUESS),
    ('timeofdaylighting', 'EnvSkyBrightness'): ('Brightness of the sky seen in car reflections.', GUESS),
    ('timeofdaylighting', 'FixedFunctionSkyColor'): ('Sky colour for the simple (fixed-function) renderer, X Y Z = red green blue.', GUESS),
    ('timeofdaylighting', 'FogDistanceScale'): ('Scales how far away the fog starts.', GUESS),
    ('timeofdaylighting', 'FogHazeColour'): ('Colour of the haze near the horizon, X Y Z = red green blue.', GUESS),
    ('timeofdaylighting', 'FogHazeColourScale'): ('Intensity of the horizon haze colour.', GUESS),
    ('timeofdaylighting', 'FogInLightScatter'): ('How strongly sunlight scatters in the fog.', GUESS),
    ('timeofdaylighting', 'FogSkyColour'): ('Colour of the fog towards the sky, X Y Z = red green blue.', GUESS),
    ('timeofdaylighting', 'FogSkyColourScale'): ('Intensity of the sky fog colour.', GUESS),
    ('timeofdaylighting', 'FogSunFalloff'): ('How the fog colour fades away from the sun direction.', GUESS),
    ('timeofdaylighting', 'SpecularColour'): ('Colour of specular highlights, X Y Z = red green blue, W = intensity.', GUESS),
    ('visuallook', 'BlackBloomIntensity'): ('Strength of the bloom glow in dark areas.', GUESS),
    ('visuallook', 'ColourBloomIntensity'): ('Strength of the coloured glow around bright areas.', GUESS),
    ('visuallook', 'ColourBloomTint'): ('Tint of the coloured bloom glow, X Y Z = red green blue.', GUESS),
    ('visuallook', 'Desaturation'): ('How much colour is drained from the picture (0 = full colour).', GUESS),
    ('visuallook', 'DetailMapIntensity'): ('Strength of the fine detail textures on surfaces.', GUESS),
    ('emitterdata', 'Drag'): ('How quickly particles slow down.', GUESS),
    ('emitterdata', 'KeyPositions'): ('Position keyframe of the emitter (X Y Z offset, W time).', GUESS),
    ('emitterdata', 'Life'): ('Lifetime of a particle (seconds).', GUESS),
    ('emitterdata', 'LifeVariance'): ('Random variation of the particle lifetime.', GUESS),
    ('emitterdata', 'MotionLive'): ('Whether particles keep moving with the emitter.', GUESS),
    ('emitterdata', 'Size'): ('Particle size (X Y Z, W).', GUESS),
    ('emitterdata', 'VelocityStart'): ('Initial velocity of new particles (X Y Z, W).', GUESS),
    ('emitterdata', 'VolumeCenter'): ('Centre of the volume the particles are emitted from.', GUESS),
    ('frontend', 'cam_anim_speed'): ('Showroom camera: speed of its animation.', GUESS),
    ('frontend', 'cam_fov'): ('Showroom camera: field of view.', GUESS),
    ('frontend', 'cam_orbit_horizontal'): ('Showroom camera: starting angle around the car.', GUESS),
    ('frontend', 'cam_orbit_vertical'): ('Showroom camera: starting height angle above the car.', GUESS),
    ('frontend', 'cam_roll_angle'): ('Showroom camera: roll (tilt) angle.', GUESS),
    ('visuallook', '*/Data[]'): ('Unnamed post-processing value (only its hash is known).', GUESS),
    ('frontend', 'Cost'): ('Price in the car lot ($).', GUESS),
    ('frontend', 'IsCustomizable'): ('Whether the car can be customised.', GUESS),
    ('frontend', 'UnlockedAt'): ('Career stage (blacklist rank) at which the car unlocks.', GUESS),
    ('frontend', 'manufacturer'): ('Manufacturer ID.', GUESS),
    ('frontend', 'region'): ('Region the car belongs to.', GUESS),
    ('frontend', 'p_induction_*'): ('Turbo/supercharger part slot offered for this car (list size, or the part reference).', GUESS),
}
HASH_NAMES = {'1362585625': 'unnamed value (hash 1362585625)', '2863451635': 'unnamed value (hash 2863451635)', '491920535': 'unnamed value (hash 491920535)',
              '2985091184': 'unnamed value (hash 2985091184)'}
SUFFIX = {'Front': 'front axle', 'Rear': 'rear axle'}


def describe(cls, field):
    """-> (description, source). field has [i] indexes."""
    g = re.sub(r'\[\d+\]', '[]', field)
    keys = [g]
    parts = g.split('/')
    if parts[-1] in SUFFIX or parts[-1] in ('X', 'Y', 'Z', 'W'):
        keys.append('/'.join(parts[:-1]))
    keys += [re.sub(r'^DZ_[A-Z]+/', 'DZ_*/', g), re.sub(r'^[a-z_]+/Data\[\]/', '*/Data[]/', g)]
    for k in keys:
        if (cls, k) in F:
            text, src = F[(cls, k)]
            extra = ''
            if parts[-1] in SUFFIX:
                extra = f' ({SUFFIX[parts[-1]]})'
            elif parts[-1] in ('X', 'Y', 'Z', 'W') and 'X, Y, Z' not in text and 'X forward' not in text:
                extra = f' (component {parts[-1]})'
            if re.search(r'(AEMS|DECEL_|GIN|Ginsu|BankName|Filename_)', k):
                extra += ' - ' + re.sub(r'(?<=[a-z])(?=[A-Z])|_', ' ', parts[-1] if parts[-1] != '[]' else parts[-2]).strip()
            return text + extra, src
    # wildcard prefix / suffix entries
    for (c, k), (text, src) in F.items():
        if c == cls and '*' in k and re.fullmatch(re.escape(k).replace(r'\*', '.*'), g):
            return text, src
    for k in parts:
        if k in HASH_NAMES:
            return HASH_NAMES[k], GUESS
    pretty = re.sub(r'(?<=[a-z])(?=[A-Z])|_', ' ', parts[-1] if parts[-1] not in SUFFIX and not re.fullmatch(r'[XYZW]|\[\]', parts[-1]) else '/'.join(parts[-2:])).strip().lower()
    return f'(no description yet) {pretty}', GUESS


# ------------------------------------------------------------------ html
def fmt(v):
    if v is None:
        return None
    if isinstance(v, bool):
        return 'yes' if v else 'no'
    if isinstance(v, float):
        s = f'{v:.6g}'
        return s
    s = str(v)
    if any(ord(c) < 32 or ord(c) > 126 for c in s):
        return repr(s)[:40]
    return s[:60]


def badge(src):
    if src == DOC:
        return f'<a class="badge doc" href="{DOC_URL}" target="_blank" rel="noopener" title="Source: MW Pursuit System Info by Orsal (opens GitHub)">doc</a>'
    return f'<span class="badge">{src}</span>'


def cls_of(rel):
    p = rel.split('/')
    if p[1] == 'attributes':
        return p[3][:-4]
    return 'frontend' if p[1] == 'fe_attrib' else 'GAMEPLAY'


def coll_label(r):
    rel = r['file'].split('/')
    if rel[1] == 'gameplay':
        return r['coll']
    return r['coll']


CSS = """
:root{--bg:#15171b;--panel:#1d2026;--line:#323843;--text:#d9d6cf;--dim:#9aa0aa;--o:#e8b04a;--r:#58b8b0;--h:#262a32;--accent:#8fb4ff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:17px/1.5 'Segoe UI',system-ui,sans-serif;padding:20px 24px 80px}
h1{font-size:28px;margin:0 0 6px}h2{font-size:22px;margin:30px 0 8px;border-bottom:1px solid var(--line);padding-bottom:6px}
p{margin:6px 0;max-width:80em;color:var(--text)}.dim{color:var(--dim)}
.bar{position:sticky;top:0;background:var(--bg);padding:10px 0;z-index:5;border-bottom:1px solid var(--line);display:flex;gap:14px;flex-wrap:wrap;align-items:center}
input[type=search]{background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:6px;padding:8px 12px;font-size:17px;width:26em;max-width:100%}
label{cursor:pointer;user-select:none}
details{background:var(--panel);border:1px solid var(--line);border-radius:8px;margin:10px 0;padding:0}
details>summary{cursor:pointer;padding:10px 14px;font-weight:600;font-size:18px;list-style:none}
details>summary::before{content:'▸ ';color:var(--accent)}details[open]>summary::before{content:'▾ '}
details .what{padding:0 14px 8px;color:var(--dim);margin:0}
table{border-collapse:collapse;width:100%;font-size:16px}
th{position:sticky;top:58px;background:var(--h);text-align:left;padding:7px 10px;border-bottom:1px solid var(--line);font-weight:600}
td{padding:6px 10px;border-bottom:1px solid #353c49;vertical-align:top}
tr.coll td{background:var(--h);font-weight:600;color:var(--accent)}
tr.row td{background:var(--panel)}
tr.row.alt td{background:#323a4a}
html:not(.js) tbody.cg tr.row:nth-child(odd) td{background:#323a4a}
tr.row:hover td{background:#46587f;border-bottom-color:#7288b8}
tr.row:hover td:first-child{box-shadow:inset 4px 0 0 var(--accent)}
td.f{font-family:Consolas,monospace;font-size:15px;word-break:break-all}
th.v,td.v{text-align:center;width:9.5em}
td.v{font-family:Consolas,monospace;white-space:nowrap}
.o{color:var(--o)}.r{color:var(--r)}.none{color:#5c636e}
a{color:var(--accent)}a.badge.doc{color:var(--accent);border-color:var(--accent);text-decoration:none}a.badge.doc:hover{background:#2c3650}
.badge{display:inline-block;font-size:12px;border:1px solid var(--line);border-radius:10px;padding:0 7px;margin-left:6px;color:var(--dim)}
.cnt{color:var(--dim);font-weight:400;margin-left:8px}
.legend span{margin-right:18px}.hide{display:none}
.names{padding:0 14px 12px;color:var(--dim);font-family:Consolas,monospace;font-size:14px;line-height:1.7}
.summary td,.summary th{position:static}
"""

JS = """
document.documentElement.classList.add('js');
const q=document.getElementById('q'),so=document.getElementById('so'),sr=document.getElementById('sr'),sb=document.getElementById('sb'),msg=document.getElementById('msg');
function apply(){
  const t=q.value.trim().toLowerCase();
  document.querySelectorAll('tbody.cg').forEach(g=>{
    let any=false;
    g.querySelectorAll('tr.row').forEach(tr=>{
      const who=tr.dataset.who;
      const okWho=(who==='o'&&so.checked)||(who==='r'&&sr.checked);
      const ok=okWho&&(!t||tr.dataset.s.includes(t));
      tr.classList.toggle('hide',!ok); if(ok)any=true;
    });
    g.classList.toggle('hide',!any||(sb.checked&&!g.dataset.both));
  });
  msg.textContent=sb.checked?(document.querySelectorAll('tbody.cg:not(.hide)').length?'Showing the records that both mods change. No single value is changed by both (see the line under the title).':'No record is changed by both mods with these filters.'):'';
  document.querySelectorAll('tbody.cg').forEach(g=>{
    let n=0;
    g.querySelectorAll('tr.row').forEach(tr=>{
      if(tr.classList.contains('hide')){tr.classList.remove('alt');return;}
      tr.classList.toggle('alt',n++%2===1);
    });
  });
  document.querySelectorAll('details.cls').forEach(d=>{
    const vis=d.querySelectorAll('tbody.cg:not(.hide)').length;
    d.classList.toggle('hide',vis===0&&(t||!so.checked||!sr.checked));
    if(t&&vis>0)d.open=true;
  });
}
[q,so,sr,sb].forEach(e=>e.addEventListener('input',apply));
apply();
"""


def build(rows, added):
    by_cls = {}
    for r in rows:
        by_cls.setdefault(cls_of(r['file']), []).append(r)
    groups = {}
    for c, rs in by_cls.items():
        g, title, what = CLASSES.get(c, ('Other', c, ''))
        groups.setdefault(g, []).append((c, title, what, rs))
    order = ['Police pursuit', 'AI, traffic & police cars', 'Damage', 'Career & shop', 'Races & events', 'Vehicles', 'Sound & visuals', 'Menus & career', 'Other']
    n_o = sum(1 for r in rows if r['o'] != r['v'])
    n_r = sum(1 for r in rows if r['r'] != r['v'])
    both = [r for r in rows if r['o'] != r['v'] and r['r'] != r['v']]
    rec = {}
    for r in rows:
        e = rec.setdefault((r['file'], r['coll']), [False, False])
        e[0] |= r['o'] != r['v']
        e[1] |= r['r'] != r['v']
    n_both_records = sum(1 for e in rec.values() if e[0] and e[1])
    out = [f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
           f'<title>Vault changes: Overhaul and {LABEL}</title><style>{CSS}</style></head><body>']
    import datetime
    out.append(f'<h1>What the Overhaul and {LABEL} change</h1>')
    out.append(f'<p class="dim">Page generated {datetime.datetime.now():%Y-%m-%d %H:%M} (rows are striped and highlighted under the mouse; if you do not see that, reload with Ctrl+F5).</p>')
    out.append('<p class="dim">Compared against vanilla 1.3: the vault databases attributes.bin, fe_attrib.bin and gameplay.bin, read with Attribulator. '
               'Floats are compared as 32-bit numbers, so formatting noise is not shown.</p>')
    out.append(f'<p class="legend"><span class="o">■ Overhaul changes it: {n_o} values</span><span class="r">■ {LABEL} changes it: {n_r} values</span>'
               f'<span>Same value changed by both (conflicts): <b>{len(both)}</b></span><span>Records both mods touch: <b>{n_both_records}</b></span><span class="dim">{len(added)} collections that {SHORT} (or the Overhaul) only adds are listed at the end</span></p>')
    out.append(f'<p class="dim">Description column: <a class="badge doc" href="{DOC_URL}" target="_blank" rel="noopener">doc</a> means it comes from '
               f'<a href="{DOC_URL}" target="_blank" rel="noopener">"MW Pursuit System Info" on GitHub</a> (Orsal / eggdog1050, with rng_guy and LunicAura; tested with VltEd; '
               'it covers the pursuit, AI vehicle, damage and PVehicle fields). <span class="badge">guess</span> is my reading of the field name, not verified.</p>')
    out.append('<div class="bar"><input id="q" type="search" placeholder="Filter: technical name, description, car or event ..."> '
               f'<label class="o"><input id="so" type="checkbox" checked> Overhaul</label> <label class="r"><input id="sr" type="checkbox" checked> {LABEL}</label> '
               '<label><input id="sb" type="checkbox"> Only records that <b>both</b> mods change</label> <span id="msg" class="dim"></span></div>')
    # overview table
    out.append(f'<h2>Overview</h2><table class="summary"><tr><th>Group</th><th>Records</th><th class="v">Overhaul</th><th class="v">{LABEL}</th></tr>')
    for g in order:
        for c, title, what, rs in sorted(groups.get(g, []), key=lambda x: x[1]):
            o = sum(1 for r in rs if r['o'] != r['v'])
            rr = sum(1 for r in rs if r['r'] != r['v'])
            out.append(f'<tr><td>{html.escape(g)}</td><td>{html.escape(title)} <span class="dim">({c})</span></td><td class="v o">{o or ""}</td><td class="v r">{rr or ""}</td></tr>')
    out.append('</table>')
    for g in order:
        if g not in groups:
            continue
        out.append(f'<h2>{html.escape(g)}</h2>')
        for c, title, what, rs in sorted(groups[g], key=lambda x: x[1]):
            o = sum(1 for r in rs if r['o'] != r['v'])
            rr = sum(1 for r in rs if r['r'] != r['v'])
            cnt = ' '.join(x for x in (f'<span class="o">Overhaul {o}</span>' if o else '', f'<span class="r">{SHORT} {rr}</span>' if rr else '') if x)
            out.append(f'<details class="cls"><summary>{html.escape(title)} <span class="dim">({c})</span><span class="cnt">{cnt}</span></summary>')
            if what:
                out.append(f'<p class="what">{html.escape(what)}</p>')
            out.append(f'<table><tr><th>Technical name</th><th>What it is</th><th class="v">Vanilla</th><th class="v">Overhaul</th><th class="v">{LABEL}</th></tr>')
            colls = {}
            for r in rs:
                colls.setdefault((r['file'], r['coll']), []).append(r)
            for (file, name), crs in sorted(colls.items(), key=lambda x: (x[0][0], x[0][1])):
                par = crs[0]['parent']
                event = ''
                if cls_of(file) == 'GAMEPLAY':
                    event = file.split('/')[2]
                label = f'{event + " / " if event and event not in name else ""}{name}'
                if par:
                    label += f' <span class="dim">· based on {html.escape(str(par))}</span>'
                both_rec = any(x['o'] != x['v'] for x in crs) and any(x['r'] != x['v'] for x in crs)
                out.append(f'<tbody class="cg"{" data-both=1" if both_rec else ""}><tr class="coll"><td colspan="5">{label}{" <span class=badge>both mods change this record</span>" if both_rec else ""}</td></tr>')
                for r in sorted(crs, key=lambda x: [int(t) if t.isdigit() else t for t in re.split(r'(\d+)', x['field'])]):
                    desc, src = describe(c, r['field'])
                    co, cr = r['o'] != r['v'], r['r'] != r['v']
                    who = 'o' if co else 'r'
                    vv = fmt(r['v'])
                    cells = []
                    for key, changed, cl in (('v', True, ''), ('o', co, 'o'), ('r', cr, 'r')):
                        val = fmt(r[key])
                        if key == 'v':
                            cells.append(f'<td class="v">{html.escape(val) if val is not None else "<span class=none>not set</span>"}</td>')
                        elif changed:
                            cells.append(f'<td class="v {cl}">{html.escape(val) if val is not None else "removed"}</td>')
                        else:
                            cells.append('<td class="v none">·</td>')
                    s = f'{c} {name} {r["field"]} {desc} {event}'.lower()
                    out.append(f'<tr class="row" data-who="{who}" data-s="{html.escape(s, quote=True)}"><td class="f">{html.escape(r["field"])}</td>'
                               f'<td>{html.escape(desc)}{badge(src)}</td>{"".join(cells)}</tr>')
                out.append('</tbody>')
            out.append('</table></details>')
    # added collections
    out.append('<h2>Collections that only exist in a mod (added, not changed)</h2>')
    out.append('<p class="dim">Mostly the mod\'s extra cars and their parts. Each one carries all of its own values, so they are listed by name only.</p>')
    ad = {}
    for a in added:
        key = (cls_of(a['file']), a['by'])
        ad.setdefault(key, []).append(a)
    for (c, by), lst in sorted(ad.items(), key=lambda x: -len(x[1])):
        g, title, what, = CLASSES.get(c, ('Other', c, ''))
        who = {'r': f'<span class="r">{LABEL}</span>', 'o': '<span class="o">Overhaul</span>', 'ro': 'both'}.get(by, by)
        total = sum(a['values'] for a in lst)
        out.append(f'<details><summary>{html.escape(title)} <span class="dim">({c})</span> · added by {who}<span class="cnt">{len(lst)} collections, {total} values</span></summary>')
        names = ', '.join(html.escape(a['coll'] if cls_of(a['file']) != 'GAMEPLAY' else a['file'].split('/')[2] + '/' + a['coll']) for a in sorted(lst, key=lambda x: x['coll']))
        out.append(f'<div class="names">{names}</div></details>')
    out.append(f'<script>{JS}</script></body></html>')
    return '\n'.join(out)


if __name__ == '__main__':
    rows, added = collect()
    page = build(rows, added)
    os.makedirs(ARGS.out, exist_ok=True)
    path = os.path.join(ARGS.out, 'vlt_changes.html')
    open(path, 'w', encoding='utf-8').write(page)
    nodesc = sum(1 for r in rows if describe(cls_of(r['file']), r['field'])[0].startswith('(no description yet)'))
    print(f'{len(rows)} changed values, {len(added)} added collections, {len(page) // 1024} KB, {nodesc} rows without a real description -> {os.path.normpath(path)}')
    missing = {}
    for r in rows:
        d = describe(cls_of(r['file']), r['field'])[0]
        if d.startswith('(no description yet)'):
            missing[(cls_of(r['file']), re.sub(r'\[\d+\]', '[]', r['field']))] = missing.get((cls_of(r['file']), re.sub(r'\[\d+\]', '[]', r['field'])), 0) + 1
    for k, n in sorted(missing.items(), key=lambda x: -x[1])[:40]:
        print('  missing:', k, n)
