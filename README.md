# OpenLF2 for Game Boy Advance

Little Fighter 2 for the Game Boy Advance, based on [OpenLF2](https://github.com/OpenLF2/OpenLF2).

## Get the ROM

Open the [web page](https://openlf2.github.io/OpenLF2-gba/) and press **Download GBA ROM**. It fetches the original
game and builds the ROM in your browser. Nothing is uploaded.

Do not share the ROM. It contains the original game's pictures, sounds and music, which are not free to
redistribute.

## Controls

| Button | Menus | Fight |
| --- | --- | --- |
| D-pad | move, change a value | walk (tap twice to run) |
| A | confirm | attack |
| B | back | jump |
| L / R | | defend (hold) |
| Start | start | pause |
| Select | | leave the fight |

Special moves are the original's: hold **defend**, press a direction, then **attack** or **jump**.

## Build from source

Needs `arm-none-eabi-gcc` 12 or newer, `make`, Python 3 with `numpy` and `Pillow`, and optionally `ffmpeg` for music.

```sh
make INSTALLER=/path/to/LF2_v2.0a.exe
```

The result is `build/openlf2.gba`.

## License

The code, scripts and documentation are MIT-licensed ([LICENSE](LICENSE)). This license does not cover the original
game, its installer or its assets.

## The original game

Little Fighter 2 was created by Marti Wong and Starsky Wong. Visit the
[original Little Fighter 2 website](https://www.lf2.net/en/intro.html) and support them with
[Little Fighter 2 Remastered](https://www.lf2.net/) or on its
[Steam page](https://store.steampowered.com/app/3249650/).

## Trademarks

Little Fighter 2, Nintendo and Game Boy Advance are names or marks of their respective rights holders. They are used
here only to identify the game or the platform. This is an independent project and is not affiliated with or
endorsed by Nintendo or the creators of Little Fighter 2.
