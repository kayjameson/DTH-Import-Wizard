# DTH Import Wizard

An Unreal Engine 5.8 editor plugin that imports a **DazToHue** character export (the files written by DazToHue's Houdini Export node) into a project in one pass. It sets up the character as DazToHue's own documentation and importer do, and adds the steps they leave to you: materials and textures, add-on pose assets, the modular control rig and a set of export checks.

> DazToHue is a separate product by its own author. This plugin doesn't include or change any DazToHue content; it needs the DazToHue Unreal content pack in your project and uses it as is.

---

## Contents

- [Requirements](#requirements)
- [Installation](#installation)
- [Project settings](#project-settings)
- [Using the wizard](#using-the-wizard)
- [What an import does](#what-an-import-does)
- [Materials and textures](#materials-and-textures)
- [Material merge check](#material-merge-check)
- [Importing again](#importing-again)
- [Scripting](#scripting)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)
- [Building from source](#building-from-source)

---

## Requirements

- **Unreal Engine 5.8**, Windows (Win64), editor only.
- The **DazToHue Unreal content pack** added to the project (by default under `/Game/DazToHue`).
- For exports that target the UE5 Mannequin: the **UE5 Mannequin** skeleton in the project.
- Engine plugins **Interchange**, **Control Rig** and **Control Rig Modules** (enabled automatically with this plugin). DQS characters also need what DazToHue's DQS deformer graph needs (Deformer Graph).
- A **Genesis 9** character exported with DazToHue's Houdini Export node: a folder with a `DTH_<Character>.dth` manifest, `Skeletal Meshes/`, and optionally `Pose Assets/` and `Animation Curves/`.
- Optional, for the material merge check: Daz Studio's own export for the character (the `.fbx` DazToHue's Houdini Import node reads).

## Installation

1. Download the release zip for your engine version.
2. Unzip it into your project's `Plugins` folder, so you have `<Project>/Plugins/DTHImportWizard/DTHImportWizard.uplugin`.
3. Start the editor. If it asks to rebuild modules, the zip doesn't match your engine version.
4. Open the wizard from **Tools > DazToHue > DTH Import Wizard**.

## Project settings

Under **Project Settings > Plugins > DTH Import Wizard**, also shown on the wizard's first page. Stored per user, per project.

| Setting | Default | Meaning |
|---|---|---|
| DazToHue Content Folder | `/Game/DazToHue` | Where the DazToHue content pack lives in this project. |
| Character Folder | `/Game/DTH_Characters` | New characters are imported into `<Character Folder>/<Character>`. |
| UE5 Mannequin Skeleton | `/Game/Characters/Mannequins/Meshes/SK_Mannequin` | The skeleton used for exports that target the UE5 Mannequin. |

Exports that target DazToHue's native skeleton use DazToHue's Genesis 9 archetype skeleton from the content pack.

## Using the wizard

Choose the export folder with the folder button at the top (the folder that holds `DTH_<Character>.dth`, or the folder above it). The wizard reads the manifest and walks you through these pages:

1. **Project Settings:** the settings above.
2. **Meshes & Rig:**
   - a summary of the export (character, figure, target skeleton, skinning method);
   - the destination content folder;
   - the meshes to import (manifest meshes are ticked; FBX files not listed in the manifest are shown unticked);
   - **Reimport existing meshes:** replace meshes that already exist in the destination instead of reusing them;
   - **Check Material Merges:** runs the [material merge check](#material-merge-check) on its own.
3. **Materials:** the material each slot will get, editable. See [Materials and textures](#materials-and-textures).
4. **Grooms:** not implemented yet.
5. **Import:** runs the import and lists every step's result, warning and error, followed by any step that has to be done by hand.

## What an import does

In order:

1. **Material merge check** against Daz's own export (warnings only, never blocks).
2. **Materials and textures** for every slot of the meshes being imported, built before the meshes so their slots pick them up by name.
3. **Meshes** through Interchange onto the target skeleton, with the settings DazToHue's importer uses:
   - morph targets on;
   - no animations, physics asset or imported textures;
   - materials found in the destination instead of created.

   A mesh that ends up on the wrong skeleton stops the import. Built materials are then assigned to every mesh slot of the same name.
4. **Post-process animation blueprint:** a per-character copy of DazToHue's blueprint for the figure, target skeleton and skinning method, at `Blueprints/ABP_<Character>`, compiled. DazToHue's own blueprint is never changed.
5. **Mesh setup**, on every imported mesh:
   - the post-process blueprint;
   - for DQS exports, DazToHue's DQS deformer graph;
   - for linear exports, Recompute Tangents on the head or face section (needs **Support Compute Skin Cache** in Project Settings).
6. **Curves:** every curve in the export's `Animation Curves` files is registered on every mesh.
7. **Retargeting** (UE5 Mannequin exports): the translation retargeting modes DazToHue sets on the Mannequin skeleton.
8. **Pose assets:** one per exported category at `PoseAssets/PA_<Character><Category>`, built from the exported animation and pose names, and made additive on its rest pose. **Add-on categories** (for example GEN and FBM) are evaluated only once their pose asset is in the character's post-process blueprint. The wizard adds a Pose Blend node for each one just before Output Pose and recompiles. If that fails, it's listed as a manual step.
9. **Control rig:** a modular control rig, `MCR_<Character>`, on the first imported mesh:
   - DazToHue's Body and Face modules, and its GP9 or DK9 module when the mesh has those bones;
   - connected to the mesh's sockets and bones;
   - DazToHue's control shape library;
   - its hierarchy checked against the mesh's rest pose;
   - set as the mesh's default animating rig.
10. **Save** of everything the import created or changed.

## Materials and textures

Each material slot gets a material instance named after the slot, in `<Character>/Materials`. It sits under a per-character copy of a DazToHue master material for its family. Eye, eye occlusion and tear slots use a per-character copy of DazToHue's eye materials directly. Textures are imported into `<Character>/Textures`.

### Defaults

The wizard builds a plan for every slot from the export:

- **Parent family:** the manifest's material type (Skin, Cloth, Metal, Plastic, Glass, Eye, EyeOcclusion, Tear, Hair). DazToHue types most clothing as Skin, so a Skin slot that isn't named after a body part (head, body, arms, legs, nails, mouth, face, torso, genitalia...) is set to Cloth.
- **Textures:** the files the manifest lists for each channel, on the right parameter for the parent. Families name their parameters differently: for example, Cloth uses `Normal Texture` where Skin uses `Base Normal Texture`. Each texture's matching "Use ..." switch is turned on.
- **Extra maps:** maps the manifest doesn't list, such as SSS, specular, ambient occlusion and height, are picked up from the same texture set by file name. A file that differs from a listed one in a single name token (`..._d_1001` → `..._sss_1001`) is taken as that set's other map.
- **Import settings by use:**

  | Used as | sRGB | Compression |
  |---|---|---|
  | Colour, scattering | on | Default |
  | Normal | off | Normal map; green channel flipped for OpenGL-style maps (`_opengl`, `normalgl`, `_gl`) only |
  | Roughness, specular, metallic, opacity, AO, height | off | Grayscale |

- **One texture asset per file:** named `T_<Character>_<Slot>_<Role>`, or after the file when three or more slots share it. Different files with the same name, which is common across outfit pieces, each get their own asset.
- **Where the files are found:** the manifest's paths first; if a file isn't there, the export's `Textures` folder.

### The Materials page

The page shows that plan grouped by mesh. For each slot:

- the manifest type and a **Parent** choice. **Skip** leaves the slot alone. Changing the parent keeps the textures it shares with the new family;
- one row per texture parameter of the parent, with the chosen file. Hover it for the full path, the texture asset name, whether its green channel is flipped, and why it was chosen;
- **Browse...** picks a file, **Clear** leaves the parameter at the parent's default. A browsed file is imported with the settings for its row;
- a warning on slots whose manifest lists a texture that can't be found.

**Reset from Export** rebuilds every slot from the export. The import uses the plan exactly as shown. Scripted imports use the defaults.

## Material merge check

Daz characters often have several surfaces that DazToHue's Houdini Material node can merge into one material slot. Without texture baking, a slot can only show one texture set. So merging is only safe for surfaces that share their textures and UV layout; DazToHue's own guides give this rule. A merge that breaks it shows up in Unreal as the wrong texture on part of a mesh.

The check finds those slots before you see them in Unreal:

1. It reads Daz's own export (`houdini/daz-export/<Character>/<Character>.fbx`, found by walking up from the export folder; scripting can pass another path).
2. It traces each exported slot back to the Daz surfaces it was made from, by matching UVs. UVs pass through DazToHue unchanged; positions don't, because clothing is pushed out against poke-through.
3. It reports every slot whose surfaces use **different texture sets**, naming the surfaces, their textures and the fix: give each its own slot in the Houdini Material node, or bake the slot.

Slots whose textures aren't any of their surfaces' files were baked and are skipped. Meshes that can't be traced back (for example, re-unwrapped UVs) are listed, not guessed at. The check runs at the start of every import, from **Check Material Merges** on the Meshes & Rig page, and from scripting.

## Importing again

Importing into a folder that already holds the character is safe:

- **Meshes** are reused unless **Reimport existing meshes** is ticked.
- **Materials** are updated in place: parent, textures and switches.
- **Textures** that already exist are reused, with their import settings applied again.
- **The post-process blueprint copy** is reused. An add-on pose node already in it isn't added twice.
- **Pose assets** are rebuilt in place, so anything that references them keeps working.
- **The control rig** is updated.

## Scripting

Everything the wizard does is available to editor Python and Blueprints through `DTHImportScripting` (`unreal.DTHImportScripting` in Python). The calls that return lines mark warnings and errors with `WARNING:` and `ERROR:` prefixes.

| Call | What it does |
|---|---|
| `describe_export(export_folder)` | What the manifest says: character, DTH version, figure, target skeleton, skinning, meshes (with their slot counts), pose assets, curve files. |
| `check_material_merges(export_folder, daz_export_fbx="")` | The material merge check on its own. |
| `import_character(export_folder, destination_folder, include_unlisted=False, reimport_existing_meshes=False, skeleton_override="")` | A full import with the default material plan; returns the import's messages. |
| `describe_character(skeletal_mesh_path, pose_folder="")` | What an import set up on a mesh: post-process blueprint, deformer, curves, retargeting, pose assets. |
| `open_wizard(export_folder="", page="")` | Opens the wizard, loads an export and shows a page (`Settings`, `Meshes`, `Materials`, `Grooms`, `Import`). |

```python
import unreal

export = "C:/Exports/MyCharacter"
for line in unreal.DTHImportScripting.check_material_merges(export):
    print(line)
for line in unreal.DTHImportScripting.import_character(export, "/Game/Characters/MyCharacter"):
    print(line)
```

## Troubleshooting

- **"only Genesis 9 is supported so far":** the manifest's figure isn't Genesis 9.
- **"no UE5 Mannequin Skeleton is set":** the export targets the Mannequin; set the skeleton in the project settings.
- **"DTH post-process blueprint not found", "DQS deformer graph not found", "Control rig module not found":** the DazToHue Content Folder setting doesn't point at the DazToHue content pack, or the pack is missing those assets.
- **A mesh "ended up on skeleton ... instead of ...":** the FBX doesn't match the target skeleton. Check the Houdini Skeleton node's source and target skeleton settings and export again.
- **Wrong texture on part of a mesh:** run **Check Material Merges**. A merged slot with different texture sets is the usual cause.
- **"Recompute Tangents needs ... Support Compute Skin Cache":** turn it on in Project Settings (Rendering) for linear-skinned characters.
- **The editor stops responding during an import:** large exports take minutes to import (each pose-asset FBX can take a minute or more), and the editor waits until the import finishes.

## Limitations

- Genesis 9 only.
- Win64 editor only.
- Grooms are not imported yet.
- The material plan's edits aren't saved between sessions; reopening an export starts from the defaults.
- During an import, progress is shown per step, not per texture or file.

## Building from source

The plugin is a standard editor plugin (C++, module `DTHImportWizard`). Copy it into a project's `Plugins` folder and build the project's editor target, or package it with

```
RunUAT BuildPlugin -Plugin=<path>/DTHImportWizard.uplugin -Package=<output folder> -TargetPlatforms=Win64
```

Use a short output path: long paths can exceed Windows' 260-character limit during the build.
