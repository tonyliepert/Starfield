# Starfield

A Windows screensaver: a starship flying through deep space at warp.

- It opens with the ship docked at the hub of a rotating wheel of a space
  station, parked between an Earth-like world and its Moon, not far from
  the Moon. The camera starts far off to the side, with the Earth and the
  Moon both in view, and closes in over 30 seconds until the wheel fills
  the screen; then the ship backs slowly out of the hub, heads briskly
  away from the Moon and goes to warp.
- Stars stream past with motion blur and Doppler colour, and the ship flies
  on through drifting nebulae. A chase camera follows an original
  saucer-and-twin-pod starship, registry FSS-908, its engine rings pulsing,
  its red beacon blinking, rows of red tail lights flashing outwards along
  its wings, and ion trails streaming from its exhausts and spine.
- Close passes bend the ship's course by gravity. A very close pass captures
  it: it drops out of warp at a red giant, yellow star, brown dwarf, pair of
  brown dwarfs circling each other, neutron star (one visit in twenty), or
  red giant with a neutron star pulling a stream of gas off it into a
  glowing disc (one in forty). It then shuts down its main engines (until
  its next jump to warp) and descends into a low orbit around one of ten
  kinds of procedural planet, with up to three moons (molten ones round a
  lava world), while the camera follows above and behind it. Planets vary
  in size, and some are huge. After two laps of an inhabited world, or one of an
  empty one, the ship jumps back to warp.
- Brown dwarfs' planets are the most often inhabited, then yellow stars';
  red giants' and neutron stars' never are. Inhabited worlds have fields,
  roads and towns by day and city lights by night. Half of them have a
  space elevator: a tether rising from the surface, with a climber riding
  it, to a station with solar wings and a counterweight above. The ship
  slows to the station, its main engines going dark, and settles onto one
  of its two docking pads on the bottom of its saucer (another ship,
  FSS-117, is docked on the other), while the camera drifts round to look down the
  tether; then it lifts off and heads away. Of the rest, half are wary of
  visitors: once the ship has settled into orbit they send up two spinning
  guide buoys to fly alongside it and escort it away.
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
program, which opens in a window when double-clicked; Esc closes it, and S
saves a JPEG of the screen in your Downloads folder).

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
