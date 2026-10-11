# UMvC3 Random Select
Makes the Random and Random All tiles on the character select screen pick from every vanilla character plus every Clone Engine character, not just the 50 vanilla ones.

Drop `umvc3_randomselect.asi` and `umvc3_randomselect.ini` into your UMVC3 game directory. It reads Clone Engine's `Characters.ini` (path set in the ini).

Don't run it alongside CSSPush, that one already has this built in.

## Configuration
`umvc3_randomselect.ini`
```ini
[CloneEngine]
CharactersIni=Characters.ini

[Random]
IncludeVanilla=1
IncludeClones=1
```
Turn either pool off to narrow it down. Both off, or no `Characters.ini`, and you get vanilla random. The inis get re-read when you hit random, so no restart needed.

## What it picks from
- Vanilla 1 to 50 that pass the same unlock and owned checks vanilla uses
- Every Clone Engine character that isn't a child, as long as its base character passes those checks
- Minus anyone already on that player's team, so no dupes on your own team. Both players can still roll the same character, same as vanilla.

A child is anything another character lists as `ChildN`, or anything with no `SoundID`. With the current `Characters.ini` that's 85 clones you can roll and 21 children you can't. Clone IDs start at 60, `[Character1]` is 60.

Rolls use the game's own RNG (`MtXorShift`).

## Clones
Colors roll inside the clone's `NumColors`. Vanilla checks DLC costume unlock bits using offsets from the character ID, which lands on unrelated bits for a clone ID, so clones skip that. Vanilla characters still use the original color roll.

Clones always get English voices. Clone Engine characters only ship English voice files, so with JP voices on the game asks for stuff like `Eyu\ryu_vo_jp.srqr`, can't find it and dies. The voice lookup right after the pick gets hooked and returns English whenever a clone got picked. It goes off the pick itself, not the ID read back from the slot, since that can already be the base character's by then.

The announcer's name callout only covers vanilla IDs and is bounds checked, so a random clone just doesn't get one.

Actually spawning the clone is still on Clone Engine.

## How it works
Both tiles go through one picker, `FUN_140360da0`. Vanilla tracks who's on your team with a 64 bit mask over IDs 0 to 51, so clones can't fit in it. Random and Random All each have their own call to the picker and the color roll, so that's four call site hooks.

| Hook | Random | Random All |
|---|---|---|
| Picker | `0x140365358` | `0x140365F87` |
| Color | `0x140365386` | `0x140365FA9` |
| Voice | `0x140365405` | `0x14036603B` |

## Credits
Built on the same utils as FrameStepper. Thanks to EternalYoshi and Gneiss.
