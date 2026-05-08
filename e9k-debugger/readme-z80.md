# Z80 debug sidecars

How to get source level z80 debugging (neo geo only).

The debugger looks for Z80 sidecar files next to the selected source/build dir. If `--source-dir` points at a project that has a `build/` directory, that `build/` directory wins. Otherwise the source dir itself is used. As a fallback it will look next to the selected ROM/ELF path.

Put one matching pair of files there:

```sh
whatever.noi
whatever.z80srcmap
```

The basename is auto-detected. The only rule is that there should be one obvious pair. If there are multiple pairs, the debugger will not guess.

Optional banked symbol files are also picked up:

```sh
whatever_bank1.noi
whatever_bank2.noi
...
whatever_bank15.noi
```

The `.noi` (NoICE) files provide symbols. The useful lines look like this:

```text
DEF snd_start_driver 0x120
DEF s_DATA 0xF800
```

## Creating .noi files

The debugger only cares about `DEF name address` lines. If your toolchain can already emit a NoICE/NOI symbol file, use that and put it next to the `.z80srcmap`.

For SDCC/ASxxxx-style Z80 builds, this usually means asking the linker for a NoICE file when linking the final Z80 image. The exact command line varies by wrapper, but it is typically a linker option on the `sdldz80`/`aslink` step, not on the assembler step:

```sh
sdldz80 ... -n whatever.noi ...
```
Keep the `.noi` basename the same as the source map basename:

```text
build/sound.noi
build/sound.z80srcmap
```

Banked symbol files should use the same stem too:

```text
build/sound_bank1.noi
```

If your toolchain cannot emit `.noi`, a small converter is enough. Generate one `DEF` line per exported symbol, with a 16-bit Z80 runtime address. The parser accepts `$1234`, `0x1234`, or plain hex.

The `.z80srcmap` file provides address to source-line lookup. It is tab-separated:

```text
# engine9000 z80 source map v1
0120	/path/to/source.s	146
```

## z80_srcmap

`z80_srcmap` builds the `.z80srcmap` sidecar from assembler listings plus source files.

Basic usage:

```sh
z80_srcmap \
  --build-dir build \
  --source-dir src \
  --out build/whatever.z80srcmap
```

You can pass `--source-dir` more than once. The tool scans those dirs recursively for `.s` and `.inc` files. It also scans the build dir for `.s` and `.inc`.

Listings come from:

```sh
--build-dir build
--source-dir src
--listing-dir some/extra/listings
```

The build dir is scanned recursively for `.lst` files, except listings under a `pack/` path are skipped. Source dirs are checked for immediate `.lst` files. Extra listing dirs are scanned recursively.

The mapper also reads every `.noi` file directly under `--build-dir`. Those symbols are used to translate listing-local addresses into runtime Z80 addresses, so make sure your Z80 build emits `.noi` and `.lst` files into the build area.

## Endian

Right now multi-byte values are treated consistently as big-endian. That is not really Z80-native. This is (probably) transitional: this should move behind an option so we can display Z80 words as little-endian.
