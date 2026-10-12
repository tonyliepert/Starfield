# Starfield

A Windows screensaver: a starship flying through deep space at warp.

- Stars stream past with motion blur and Doppler colour, and the ship flies
  on through drifting nebulae. A chase camera follows an original
  saucer-and-twin-pod starship, registry FSS-908, its engine rings pulsing,
  its red beacon blinking, rows of red tail lights flashing outwards along
  its wings, and ion trails streaming from its exhausts and spine.
- Close passes bend the ship's course by gravity. A very close pass captures
  it: it drops out of warp at a red giant, yellow star, brown dwarf, neutron
  star, or a red giant with a neutron star pulling a stream of gas off it
  into a glowing disc. It then descends into a low orbit around one of ten
  kinds of procedural planet, with up to three moons, while the camera
  follows above and behind it. Half the planets are inhabited, with fields,
  roads and towns by day and city lights by night; half of those are wary
  of visitors, and send two spinning guide buoys to fly alongside the ship
  and escort it away.
  Otherwise, after two laps the ship jumps back to warp.
- Original music, synthesized at startup, crossfades between a cruise theme
  and a calmer orbit theme. If `geoffharvey-starfleet-command-150605.mp3` (or
  whatever `kTrackFile` in `src/music.h` names) is beside the program, it
  plays at warp instead.

## Building

Needs [MSYS2](https://www.msys2.org/) with the UCRT64 toolchain
(`pacman -S mingw-w64-ucrt-x86_64-gcc`), installed in `C:\msys64`. Then run:

```
build.bat
```

This builds `Starfield.scr` (the screensaver) and `Starfield.exe` (the same
program, which opens in a window when double-clicked; Esc closes it).

To install it as your screensaver, right-click `Starfield.scr` and choose
**Install**.

## Command line

- `/s` run full screen
- `/p <hwnd>` run in the Screen Saver Settings preview
- `/c` settings (there are none)
- `/w` run in a resizable window
- `/x <name> [seconds]` fly for that many seconds, then save three frames as
  `<name>_0.bmp` .. `<name>_2.bmp`

`shots.ps1` uses `/x` to render test frames as PNGs, and `tools/make_icon.py`
makes `app.ico` from one of them.
