# DTH Import Wizard

An Unreal Engine 5.8 editor plugin that imports a **DazToHue** character export (the files written by DazToHue's Houdini Export node) into a project in one pass. It sets up the character as DazToHue's own documentation and importer do, and adds the steps they leave to you: materials and textures, add-on pose assets, the modular control rig, a set of export checks, and a character blueprint with a wardrobe that further outfit and hair exports add to.

> DazToHue is a separate product by its own author. This plugin doesn't include or change any DazToHue content; it needs the DazToHue Unreal content pack in your project and uses it as is.

---

## Contents

- [Requirements](#requirements)
- [Installation](#installation)
- [Project settings](#project-settings)
- [Using the wizard](#using-the-wizard)
- [What an import does](#what-an-import-does)
- [Wardrobe and character blueprint](#wardrobe-and-character-blueprint)
- [Materials and textures](#materials-and-textures)
- [Material merge check](#material-merge-check)
- [Importing again](#importing-again)
- [Scripting](#scripting)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)
- [Building from source](#building-from-source)

---

## Requirements

- **Unreal Engine 5.8**, Windows (Win64). The wizard is editor only; the character blueprint's runtime module (`DTHWardrobe`) also works in Movie Render Queue and packaged projects.
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
   - **Wardrobe:** the outfit name this export's garments become, and **Add to an existing character** for an outfit or hair export of a character already imported into the destination (ticked automatically when the destination has a wardrobe), and **Delete the old wardrobe** (see [Importing again](#importing-again)). See [Wardrobe and character blueprint](#wardrobe-and-character-blueprint);
   - the meshes to import (manifest meshes are ticked; FBX files not listed in the manifest are shown unticked);
   - **Reimport existing meshes and grooms:** replace meshes and grooms that already exist in the destination instead of reusing them;
   - **Check Material Merges:** runs the [material merge check](#material-merge-check) on its own.
3. **Materials:** the material each slot will get, editable. See [Materials and textures](#materials-and-textures).
4. **Grooms:** every `ABC_<Groom Name>.abc` the DazToHue Groom Export node wrote to the root of the export folder, each with an import tick box, its binding's interpolation points and the body slot used as its Matching Section. See [Grooms](#grooms).
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
8. **Pose assets:** one per exported category at `PoseAssets/PA_<Character><Category>`, built from the exported animation and pose names, and made additive on its rest pose. **Add-on categories** (for example GEN and FBM) are evaluated only once their pose asset is in the character's post-process blueprint. The wizard adds a Pose Blend node for each one just before Output Pose and recompiles. If that fails, it's listed as a manual step. PHY is the exception: it stays out of that step, because the jiggle Pose Drivers use it.
   With **Jiggle physics (Genesis 9)** ticked, the wizard also adds Spring Controllers on the five physics bones and the exported Pose Drivers to the post-process blueprint, with stiffness and damping as blueprint variables for each region. It fills in the pose targets the export leaves empty and sets the driver radius to 2, so the character isn't deformed at rest. The option is available for Genesis 9 exports with a PHY pose asset and pose driver code, which need the physics bones ticked on DazToHueSkeleton.
9. **Control rig:** a modular control rig, `MCR_<Character>`, on the first imported mesh:
   - DazToHue's Body and Face modules, and its GP9 or DK9 module when the mesh has those bones;
   - connected to the mesh's sockets and bones;
   - DazToHue's control shape library, checked to reach the compiled rig, so its controls show their shapes in Sequencer;
   - its hierarchy checked against the mesh's rest pose;
   - set as the mesh's default animating rig.
10. **Grooms:** the DazToHue Groom guide's Unreal setup for each ticked groom. See [Grooms](#grooms).
11. **Wardrobe and character blueprint:** the wardrobe items, the export's outfit and `BP_<Character>`. See [Wardrobe and character blueprint](#wardrobe-and-character-blueprint).
12. **Save** of everything the import created or changed.

An **add-on import** (Add to an existing character) does steps 1 to 6, 10, 11 and 12 for the export's garments and grooms only. First it checks the export's body against the character's; the export's body is never imported, and retargeting, pose assets and the control rig are left as they are.

## Grooms

Export each groom from Houdini with the DazToHue Groom Export node, with its **Export Directory** set to the character's export folder (the one holding `DTH_<Character>.dth`). The wizard picks up every `ABC_<Groom Name>.abc` there and follows the Groom guide's "Unreal Engine - Basic setup":

- **Import** at default options into the character's folder, as `ABC_<Groom Name>`.
- **Binding** on the body mesh (target and source), named `ABC_<Groom Name>_<Body>_<Body>_Binding` as the editor's Create Binding names it. The Matching Section is the element number of the chosen body slot, by default the slot whose name ends in `Head`. Interpolation points default to 25 for grooms named like brows or lashes and 100 for everything else, as the guide suggests.
- **Material:** a copy of DazToHue's `MI_DTH_Hair` per groom, `Materials/MI_<Character><Groom Name>`, in the groom's material slot. It shows the strand colours set in Houdini; change its parameters to override them.

Each groom becomes a wardrobe item, so `BP_<Character>` shows it, bound to the body; there is no Groom component to add by hand. The binding is built once, at import, against the character's one body, and is never rebuilt when outfits or hair change. The plugin turns on the engine's **Groom** and **Alembic Groom Importer** plugins.

## Wardrobe and character blueprint

Every import fills the character's wardrobe in `<Character>/Wardrobe` and makes sure `BP_<Character>` exists next to it.

- **Items** (`DA_Item_<Character>_<Name>`): one per garment and per groom. Each has a **Slot** guessed from its name: Torso, Legs, Feet, Arms, Shoulders, Neck, Head, Underwear, Accessory for garments; Hair, Eyebrows, Eyelashes, Vellus, Beard for grooms. Edit the slot or the name the item is known by (**Item Name**) freely: a later import finds an item by its mesh or groom, not its name.
- **Base items:** brows, lashes and vellus are part of the face and are worn with every outfit.
- **Outfits** (`DA_Outfit_<Character>_<Name>`): the export's garments and head hair become the outfit named on the Meshes & Rig page. The name defaults to what the garment names share, e.g. `Witch` for `Witch_Dress`, `Witch_Hat`... The first import also makes **Nude** (head hair only).
- **The wardrobe** (`DA_Wardrobe_<Character>`): the body, the base items, the outfits, and **extra items**. An export with grooms but no garments (a new hairstyle) adds them as extra items instead of making an outfit.
- **`BP_<Character>`:** a **DTH Character** on the wardrobe, wearing the first import's outfit. Its Components panel has the **Body** (animate this component; its mesh is set) and, under it, a `Wardrobe_<Item>` component for every garment (following the body's pose and its joint correctives) and every groom (bound to the body). It opens in the full Blueprint editor, with the character in the viewport. Changing outfits only shows and hides these components. You can adjust them like any component, e.g. set a groom's Physics Asset; a later import keeps your changes and only adds components for new items. Items you add to the wardrobe by hand are created by the actor at construction instead.

### Wardrobe State

What the character wears is one text property, **Wardrobe State**, set in the Details panel or keyed in Sequencer (add a **Wardrobe State** track to the actor; each key is a state):

| State | Wears |
|---|---|
| `Witch` | the Witch outfit and the base items |
| `Witch -Hat -Heels` | without those items |
| `Witch -slot:Feet` | without every Feet item |
| `Witch +Ponytail` | with an item from anywhere in the wardrobe; it replaces the worn items of its slot (the outfit's hair, here) |
| `-Hat` | the current outfit, with new overrides |
| empty | the wardrobe's first outfit |

Names are case-insensitive and can be separated by spaces or commas. Unknown names are reported in the Output Log (category `LogDTHWardrobe`), which also logs each state set from Sequencer, scripting or Blueprints.

### From Blueprint

`BP_<Character>` is a child of the C++ **DTH Character** class, so its wardrobe support is inherited: right-click in its graph (or in a child Blueprint, or any Blueprint with a DTH Character reference) and look under **Wardrobe**. The inherited variables show in My Blueprint once **Show Inherited Variables** is on (the gear menu), and under **Wardrobe** in Class Defaults and the Details panel.

| Kind | Members |
|---|---|
| Variables | **Wardrobe**, **Wardrobe State**, **Current Outfit**, **Worn Items** |
| Inventory (pure) | **Get Outfit Names**, **Get Item Names** (Slot, None for all), **Get Items** (Slot), **Get Slots**, **Find Item**, **Is Wearing**, **Get Current Outfit Name**, **Get Worn Items**, **Get Item Component**; on an item or outfit, **Wardrobe Name** |
| Changes | **Set Outfit**, **Wear Item**, **Remove Item**, **Toggle Item**, **Remove Slot**, **Reset To Outfit**, **Set Wardrobe State** |
| Events | **On Wardrobe Changed** (Old State, New State, Worn Items), **On Item Worn** (Item), **On Item Removed** (Item) |

The changes edit Wardrobe State. A toggle is just another state, so Blueprint, Sequencer keys and the Details panel can't disagree, and every change fires the events. For example, an inventory UI can list **Get Item Names**, call **Toggle Item** on a click, and refresh from **On Wardrobe Changed**. The lists grow with every add-on import, with nothing to change in the Blueprint.

### More outfits and hairstyles

A character can have any number of outfits and hairstyles, each exported from its own Houdini file. Import each one into the character's folder with **Add to an existing character** ticked. Its garments and grooms go on the character's body and into its wardrobe, and the outfit is available on the same `BP_<Character>`.

The export's body has to be the character's body: same vertex count, rest-pose size and bones as the file the character's body was imported from. Otherwise the import stops before anything is imported and says what differs. So export add-ons from a Houdini file with the same character at the same subdivision level, and **without the Occlusion node** (it deletes body polygons under the clothing, which changes the body).

## Materials and textures

Each material slot gets a material instance named after the slot, in `<Character>/Materials`. It sits under a per-character copy of a DazToHue master material for its family. Eye, eye occlusion and tear slots use a per-character copy of DazToHue's eye materials directly. Textures are imported into `<Character>/Textures`.

### Defaults

The wizard builds a plan for every slot from the export:

- **Parent family:** the manifest's material type (Skin, Cloth, ClothMasked, Metal, Plastic, Glass, Eye, EyeOcclusion, Tear, Hair). DazToHue types most clothing as Skin, so a Skin slot that isn't named after a body part (head, body, arms, legs, nails, mouth, face, torso, genitalia...) is set to Cloth.
- **Cloth with an opacity map** (lace, sheer panels, cut-outs) is set to ClothMasked. DazToHue's Cloth material is opaque and has no opacity input, so the opacity map would otherwise be dropped. ClothMasked is a per-character copy of DazToHue's masked cloth material (`M_<Character>ClothMasked`). The opacity map goes on its `Mask Texture`, with `Enable Mask` and `Use Mask Texture` on. The copy dithers the mask: each pixel shows with a probability equal to its opacity, so sheer fabric that Iray renders partly see-through stays see-through under temporal anti-aliasing (TSR or TAA), while fully opaque and fully transparent areas are unchanged.
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
- **The wardrobe** is only added to: existing items (found by mesh or groom), outfits, slots, names and `BP_<Character>` are kept.

Before it makes anything, the import checks every path it writes: the meshes, the post-process blueprint, pose assets, the control rig, grooms and `BP_<Character>`. Unreal can't replace an asset with one of another kind, so if one of those paths holds something else, the import stops and names it. Move or rename it and import again.

The destination's `Wardrobe` folder belongs to the import's wardrobe. If it holds anything else, such as an older wardrobe made by the Python wizard, the import stops and lists those assets. Either move them out of the folder, or tick **Delete the old wardrobe** to have the import delete them. Anything that used them, such as an old character blueprint, loses those references; the import lists each one.

## Scripting

Everything the wizard does is available to editor Python and Blueprints through `DTHImportScripting` (`unreal.DTHImportScripting` in Python). The calls that return lines mark warnings and errors with `WARNING:` and `ERROR:` prefixes.

| Call | What it does |
|---|---|
| `describe_export(export_folder)` | What the manifest says: character, DTH version, figure, target skeleton, skinning, meshes (with their slot counts), pose assets, curve files. |
| `check_material_merges(export_folder, daz_export_fbx="")` | The material merge check on its own. |
| `import_character(export_folder, destination_folder, include_unlisted=False, reimport_existing_meshes=False, skeleton_override="", add_to_character=False, outfit_name="", delete_old_wardrobe=False, jiggle_physics=False)` | A full import with the default material plan, or with `add_to_character` an add-on import into the character in `destination_folder`; `delete_old_wardrobe` as the wizard's check box; `jiggle_physics` as its Jiggle physics box. Returns the import's messages. |
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
- **"This export's body is not ...'s body":** an add-on export was made from a different body. See [More outfits and hairstyles](#more-outfits-and-hairstyles).
- **The body disappears for a moment after changing Wardrobe State in the Details panel:** the editor rebuilds the actor's components and the body takes a few frames to show again. Keys in Sequencer don't rebuild anything.
- **"... holds an older wardrobe" or "... is a ...; the import puts a ... there":** something else is at a path the import writes. See [Importing again](#importing-again).
- **A character's controls show in the Control Rig editor but not in Sequencer:** its rig was made by version 0.3.1 or earlier, which didn't pass the shape library on to the compiled rig. Import the character again (nothing needs ticking); the rig is repaired in place.
- **The editor stops responding during an import:** large exports take minutes to import (each pose-asset FBX can take a minute or more), and the editor waits until the import finishes.

## Limitations

- Genesis 9 only.
- Win64 editor only.
- The groom occlusion texture the Groom Export node can bake is not used.
- The material plan's edits aren't saved between sessions; reopening an export starts from the defaults.
- During an import, progress is shown per step, not per texture or file.

## Building from source

The plugin is a standard C++ plugin with an editor module (`DTHImportWizard`) and a runtime module (`DTHWardrobe`). Copy it into a project's `Plugins` folder and build the project's editor target, or package it with

```
RunUAT BuildPlugin -Plugin=<path>/DTHImportWizard.uplugin -Package=<output folder> -TargetPlatforms=Win64
```

Use a short output path: long paths can exceed Windows' 260-character limit during the build.
