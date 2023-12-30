

# PMD Sky Speedrun Mod

  **This mod aims to enhance the experience of speedrunning or racing PMD: Explorers of Sky in various ways. It can also be used for randomizer runs, etc. Since it's different from the vanilla game, separate leaderboards are hosted [here](https://www.speedrun.com/pmdskyext?h=Fast_any-ENG-no_WM-Cutsceneless).**

## Cutsceneless

Removes all mandatory cutscenes. Brings the run time to around 2 hours at a high level, while staying accurate to timesaves and losses from deaths, shopping, etc. For example, the skippable dinner cutscenes  in vanilla are not removed, you still need to manually skip them by pressing start!

**Credit:** SBDWolf
  
## RNG Sync

This feature lets you play through the game on a pre-determined RNG seed. It is primarily meant to be used in races, where the players pick a fresh previously unplayed seed. This way, every player will encounter the same floors, shops and basically anything that meaningfully affects a linear speedrun category, such as Any%. While there still is luck involved in decision making, this still drastically reduces the RNG factor in a race.  All the while, from the standpoint of an individual player, the randomness they experience in their playthrough is indistinguishable from that of a vanilla run.

To activate this feature, go to **Other** in the **Main Menu** and choose **RNG Seed**. Then, input any seed you wish. The field is already filled with a random 16-bit number, but you can enter anything that fits, including letters. After confirming the input, the RNG seed is set until you either restart the game or go back into the menu and change it again. Setting the seed to empty will cause the RNG to behave like vanilla again.

The RNG is influenced by your current story progress. This means that even if you visit e.g. Beach Cave multiple times during a run, it will be different each time. Even if the story doesn't progress, the same results won't appear twice. This means that if you spawn in a monster house and die, you can retry the dungeon without having that happen again, for example. After progressing to the next point in the story, however, the RNG becomes synced back up with everybody else even if they did not die.

## Speedrun HUD

This is a display for useful supplementary information during a speedrun. This display can be toggled by pressing **start+select**, rotating between three modes: **off**, **minimal** and **maximal**. The HUD is off by default. If you enable RNG Sync, the HUD will always be turned on in order to display the current seed.

The information displayed in each mode is described in the table below. The descriptions of the individual components can be found after.
| Mode | Timer | Seed display | FPS counter | Input display |
|--|--|--|--|--|
| Minimal | Top screen | Top screen | *Hidden* | *Hidden* |
| Maximal| Bottom screen | Top screen | Bottom screen | Bottom screen |

### Timer

The core functionality of this component is to display the current play time of the save file. The reason this is useful as opposed to a real timer is that the game does a surprisingly good job at removing load times and lag from the timer. This means that runners on different platforms like DS, Wii U, different emulators, different flashcarts, different versions of nds-bootstrap, etc. can play on an even playing field without losing extra time to loading or lag. While comparing the in-game time could already be done after races, it's more exciting for runners and viewers to be able to tell the standings during the races as well.

The timer also supports timing for custom intervals. This can be used to time runs of individual dungeons during practice, speedruns of special episodes or even get rough timings on things like move animations for routing purposes. To start a custom interval, simply press **start+L**. This will reset the timer to 0 on screen, but will continue counting with the real value in the background. To return to the file timer, input the combination again.

You can also temporarily stop the timer to show a split time. To do this, press **select+L**. This will pause the timer display for 10 seconds and show the split time in blue text. Again, the time is still being counted in the background. Additionally, the split time will automatically be displayed at the end of every dungeon.

### Seed display

This component displays the current RNG Sync Seed, if applicable. Cannot be hidden if a seed has been set.

### FPS counter

Displays the current frame rate. This is defined as the ratio between frames where the timer advances and frames where the RNG advances over a monitoring window of 0.25 seconds. As such, it's more of a general lag counter as the picture can sometimes be updated during loads, but for practical purposes it is the same thing.

The reason for its inclusion is mainly the fact that this game lags a lot while dashing in corridors, so this can be used to more easily measure that lag. Knowing this is interesting, as the dashing lag varies depending on platform, so this complements the in-game timer quite well.

### Input display

Displays the current inputs using in-game icons.

## Known issues

- The text placement has been optimized so that it does not collide with other UI elements, but due to the janky window system, it is not always possible to do perfectly. There are some instances of text clipping out, such as the top screen text getting partly covered by the top screen map in dungeons. However, this top screen setting  is suboptimal for speedruns either way due to losing time to loads, so it is not a high priorty.
- The HUD cannot be seen during most fades. This is another limitation of the window system and cannot be fixed without low level tinkering.

## Credits

- [Base cutsceneless hack](https://github.com/SBDWolf/PMD-Sky-Cutsceneless-Hack)
- [SkyTemple](https://github.com/SkyTemple/skytemple)
- [c-of-time](https://github.com/SkyTemple/c-of-time)
- [pmdsky-debug](https://github.com/UsernameFodder/pmdsky-debug)

**Original readme of c-of-time below. Includes build instructions.**

To obtain the base rom, apply the base patches in [/base_patches](/base_patches) first to a clean EU rom. The base rom includes the cutsceneless patch and some string changes, but technically the c-of-time patches should work on any rom that does not utilize the privately allocated `Overlay36.bin` area.

# c-of-time

An environment for hooking and linking to Pokémon Mystery Dungeon: Explorers of Sky.

## Credits
This project is loosely based on [EternalCode's template](https://github.com/EternalCode/Empty-Template). The build configuration is based on scripts provided by [devkitPro](https://devkitpro.org). The patch format was inspired by [Starlight](https://github.com/shadowninja108/Starlight).

Special thanks to [UsernameFodder](https://github.com/UsernameFodder) for the [pmdsky-debug](https://github.com/UsernameFodder/pmdsky-debug) project, [End45](https://github.com/End45) for the *ExtraSpace* patch and irdkwia for their research on item, move and special process effects.

## Rust subsystem
**NOTE: The `main` branch does currently not contain the Rust subsystem anymore**, as it's support
for symbols for `pmdsky-debug` is outdated and we eventually want to split the Rust subsytem
off so we can keep `c-of-time` up-to-date with `pmdsky-debug` more easily. Use the `rust` branch
if you want to use the Rust subsystem.

c-of-time can also be used with Rust projects. If you want to use Rust (including mixed Rust + C projects),
continue reading the `README.md` in the `rust` directory.

If you want to build pure C projects, continue below.

## Project setup
1. Install [Python](https://www.python.org/downloads/).
2. Install GCC and Binutils for `arm-none-eabi`. See [install_gcc.md](install_gcc.md) for information on how to install it.
3. Clone this repository *recursively* with `git clone --recursive https://github.com/tech-ticks/c-of-time.git`. Make sure that you enter the correct directory before continuing (e.g. `cd c-of-time`).
  - If you don't have Git installed, click the green "Code" button on GitHub to download this repository, then do the same for [pmdsky-debug](https://github.com/UsernameFodder/pmdsky-debug). Finally, extract both .zip files and copy the `pmdsky-debug` folder into the `c-of-time` folder.
4. Install Python dependencies: `pip3 install pyyaml ndspy`
5. Patch a Pokémon Mystery Dungeon: Explorers of Sky ROM with the [`ExtraSpace` patch by End45](https://github.com/End45/EoS-asm-hacks/blob/main/src/ExtraSpace.asm). You can apply the patch with [SkyTemple](https://skytemple.org):
    1. Open the ROM in SkyTemple
    2. Click *ASM Patches* (*Patches > ASM* in SkyTemple 1.4+) and switch to the *Utility* tab
    3. Select the *ExtraSpace* patch and click *Apply*
6. Place the ROM in `[project root]/rom.nds`
    - **US ROM offsets are used by default.** If you're using a EU ROM, change the `REGION` variable in `Makefile` to `EU`.
7. Follow these steps depending on your operating system:
    - If you are using Linux, install [armips](https://github.com/Kingcom/armips) manually.
    - If you are encountering errors with armips on Windows, you might need to install the [Visual C++ Redistributable for Visual Studio 2015](https://www.microsoft.com/en-US/download/details.aspx?id=48145).
    - On macOS, you might need to do the following:
      - Navigate to the folder `bin/armips` in Finder
      - Right-click `armips-mac-x64`, click "Open" and confirm
8. (optional) Run `make header-comments` to generate documentation comments for IDEs.

## Building
To build the project, run `make patch`. This command will build your code, inject it into an overlay in the provided ROM and apply the patches in the `patches` directory. The output ROM will be saved as `out.nds` by default.

If you want to check the generated assembly, run `make asmdump`. A file `out.asm` will be generated, which contains an assembly listing annotated with the corresponding source code lines.

## Usage
Patches can be added to `.asm` files inside the `patches` directory. These patch files contain offsets into functions that should be patched and assembly instructions, which allow calling into custom code. See `src/main.c` and `patches/patches.asm` for examples.

### Logging and assertions
You can use the logging macros `COT_LOG`, `COT_WARN` and `COT_ERROR`. To view the logs, open the ROM in the SkyTemple debugger and check "Game Internal" in the log window. A macro for assertions `COT_ASSERT(expr)` is also available.

To disable assertions and logging globally and save some performance, change `RELEASE_CONFIG` in `Makefile`.

### Custom move/item effects and special processes
To create custom special processes, add them into the `switch` statement in `CustomScriptSpecialProcessCall`. This function is only called for special process ID 100 and greater for compatibility with existing patches.

You can add custom item or move effects in `CustomApplyItemEffect` and `CustomApplyMoveEffect`.

#### Compatiblity with existing patches
This project aims to keep compatibility with existing patches for move, item and special process effects to some degree. Special process effects using the `ExtractSpCode` patch can be reused without problems if they were imported with an ID lower than 100. Compatiblity with the `ExtractMoveCode` has not been thoroughly tested yet and might potentially cause issues with the *Metronome* move.

## Updating symbol definitions and headers
To update symbol data from `pmdsky-debug`, run `git submodule foreach git pull origin master`,
then clean the build with `make clean`.

## Adding custom symbols
If you've found symbols that are currently missing, consider contributing them to [pmdsky-debug](https://github.com/UsernameFodder/pmdsky-debug). You can find instructions in the repository's [contribution docs](https://github.com/UsernameFodder/pmdsky-debug/blob/master/docs/contributing.md).

For quick testing, you can also add symbols to `symbols/custom_[region].ld` (`symbols/generated_[region].ld` is auto-generated and should not be modified). You need to specify the file each symbol belongs to in comments:

```
/* !file arm9 */
MyCoolFunction = 0x200DABC;

/* !file overlay29 */
SomeDungeonFunction = 0x22DEABC;
SomeOtherDungeonFunction = 0x22DEEFF;
```

## Code size constraints

The built code gets injected into the custom overlay 36. The entire overlay is 228 KB big, most of which is reserved for common patches provided by SkyTemple. Your code will be placed in the last 32 KB, which are considered the "common area" . If the binary is larger than 32 KB, you will get the following linker error: 
```
error "section '.text' will not fit in region 'out'"
```

### Expanding the available space
To work around this issue, you can extend the space allocated in the overlay. **If you decide to extend the space, you do so at your own risk. Be careful since this space might be used by future patches!** Check the [list of assigned areas](https://docs.google.com/document/d/1Rs4icdYtiM6KYnWxMkdlw7jpWrH7qw5v6LOfDWIiYho) to find out if patches used in your ROM are affected.

The value of `ORIGIN` must a multiple of 16 (end with 0 in hexadecimal). Therefore, the amount of bytes added to `LENGTH` must also be a multiple of 16.

To extend the allocated space, open `linker.ld` and edit the following line:
```
out     : ORIGIN = 0x23D7FF0, LENGTH = 0x8010
```

Subtract the amount of additional bytes you want to allocate from `ORIGIN` and add them to `LENGTH`. Next, open `patches/patch.py` and set `START_ADDRESS` of the top of the file to the same value as `ORIGIN` in the linker script.

### Optimizing for size
You can also change the compiler flags to optimize for size instead of speed. To do so, set `OPT_LEVEL := Os` in `Makefile`. Effectiveness varies per project.

## Licensing
- Build scripts (everything under the `tools`) are licensed under GPLv3. Review the file `LICENSE_GPLv3` for more information.
- All other code is licensed under MIT. Review the file `LICENSE_MIT` for more information.
