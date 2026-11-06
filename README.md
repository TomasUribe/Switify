<!-- Bloque compartido: es el mismo en cristowo/SwitchU y cristowo/Switify. -->
# SwitchU + Switify (fork de cristowo)

Dos forks que funcionan juntos en la Switch:

| Repositorio | Fork de | Rama | Qué es |
| --- | --- | --- | --- |
| [cristowo/SwitchU](https://github.com/cristowo/SwitchU) | [PoloNX/SwitchU](https://github.com/PoloNX/SwitchU) | `switify-integration` | Menú HOME estilo Wii U, con Spotify integrado |
| [cristowo/Switify](https://github.com/cristowo/Switify) | [DomazinUS/Switify](https://github.com/DomazinUS/Switify) | `switchu` | Servicio en segundo plano de Switify, ajustado para SwitchU |

## Capturas

| HOME | Spotify en los Ajustes rápidos |
| --- | --- |
| ![HOME con el tema Luciérnagas](https://raw.githubusercontent.com/cristowo/SwitchU/switify-integration/screenshots/fork/home.jpg) | ![Ajustes rápidos con Spotify](https://raw.githubusercontent.com/cristowo/SwitchU/switify-integration/screenshots/fork/quick-settings-spotify.jpg) |
| **Una playlist de Spotify** | **Pantalla de bloqueo con Spotify** |
| ![Explorando una playlist de Spotify](https://raw.githubusercontent.com/cristowo/SwitchU/switify-integration/screenshots/fork/spotify-playlist.jpg) | ![Pantalla de bloqueo con barras de espectro](https://raw.githubusercontent.com/cristowo/SwitchU/switify-integration/screenshots/fork/lock-screen-spotify.jpg) |

## Créditos

- **SwitchU**: [PoloNX](https://github.com/PoloNX) y sus colaboradores (GPL-3.0).
  [uLaunch](https://github.com/Xortroll/uLaunch), de [Xortroll](https://github.com/Xortroll),
  inspiró el proyecto.
- **HomeNX**: la pantalla de bloqueo y su contraseña, el zoom al salir de un juego,
  la subida de texturas sin bloqueo, los nombres de juego personalizados, el ajuste
  de efectos de cristal y la temperatura del chip vienen de
  [HomeNX](https://github.com/ad3333m/homenx), de ad3333m (GPL-3.0). Su pantalla de
  bloqueo es de PowerCmptr.
- **Switify**: HiroshiYamauchi ([DomazinUS/Switify](https://github.com/DomazinUS/Switify)).
  La app es GPL-3.0-or-later y el overlay GPL-2.0-only. Usa
  [cspot](https://github.com/feelfreelinux/cspot) y bell, de feelfreelinux y
  colaboradores; el resto de dependencias está en `THIRD_PARTY_NOTICES.md` de Switify.
- **Cambios de estos forks**: cristowo, desarrollados con Claude Code (Anthropic).

Todo el trabajo original pertenece a sus autores. Estos forks mantienen sus licencias.

## Qué cambia respecto a los originales

### SwitchU

- **Música unificada**: selector Canciones / Spotify en los Ajustes rápidos (L).
  Spotify se controla y se explora (cola, playlist, biblioteca, inicio, búsqueda)
  a través del servicio de Switify. Suena una sola fuente a la vez.
- **Widgets de música**: "Reproduciendo" en 1x1 (la portada ocupa todo el
  recuadro, con las barras encima), 2x1 y 2x2 (portada, canción, barras y
  progreso). Además, "Playlists de Spotify": muestra una playlist de tu
  biblioteca; con Más pasas a la siguiente o la anterior, y con A suena ahí
  mismo, sin abrir los Ajustes rápidos.
- **Pantalla de bloqueo** al despertar, con contraseña opcional, controles de música
  y barras de espectro. Muestra "Reconectando..." mientras Spotify vuelve. Con
  "Carátula en la pantalla de bloqueo", la funda del álbum ocupa el centro con un
  disco que asoma y gira en los dos colores principales del álbum, la hora pasa
  arriba a la izquierda y las barras van debajo. La música, la carátula y la
  música con la pantalla apagada se eligen por separado para las canciones del
  menú y para Spotify.
- **Al encender la consola** suena la música del tema desde su canción de HOME
  ("Wii U Home Menu"), aunque lo último haya sido Spotify.
- **Sin suspensión automática con música** (Ajustes, Suspensión; activada por
  defecto): mientras suena música en HOME o en la pantalla de bloqueo, la consola
  no se suspende sola.
- **Cerrar juego**: pide confirmación y cierra al instante, como el HOME original.
  Antes esperaba hasta 15 s a un juego suspendido.
- **Fiabilidad**:
  - el menú elige su memoria según lo que queda libre;
  - se reconecta a la SD si el sistema le corta la sesión;
  - manda su log en vivo al daemon durante el arranque, así un fallo al iniciar queda registrado;
  - los logs viejos del daemon se borran por fecha.

### Switify (solo el servicio en segundo plano)

El detalle está en `SWITCHU-CHANGES.md` de Switify. En resumen:

- **`GetAudioLevels` (comando 38)**: 32 bandas del audio que suena, para las barras
  de SwitchU. Solo se calculan si alguien las pide.
- **`ReloadCurrent` (comando 39)**: tras suspender la consola, espera a que vuelva la
  sesión de Spotify y retoma la misma canción donde iba, en vez de saltarla.
- **Lo cargado sigue sonando al reconectar**: siempre hay unos 5 s de canción
  descargados por adelantado (10 s en calidad normal). Al despertar suena eso y,
  cuando vuelve la conexión, el mismo stream sigue desde el byte donde quedó: sin
  saltos y, si vuelve a tiempo, sin cortes. Si no puede, la canción se recarga en
  la posición exacta del decodificador. El estado lleva el flag
  `SwitifyStatusFlag_Reloading` (bit 8) mientras tanto.
- **Conexión al CDN más robusta**: una respuesta que no sea el rango pedido cierra
  la conexión y abre otra, en vez de arruinar las siguientes y cambiar de canción.
- **Cambiar rápido de canción ya no lo cierra**: una respuesta de Spotify para una
  canción ya saltada se descarta, en vez de escribir en memoria liberada.
- **Memoria a demanda**: el heap ya no es un bloque fijo de 4 MiB. Se toma en pasos
  de 2 MiB según se necesita, hasta el mismo máximo. El servicio cuenta contra la
  memoria del sistema, y con 4 MiB fijos abrir HOME sobre un juego podía agotarla:
  el menú se quedaba en negro.
- **Cuenta como applet** (`application_type: 2` en `switify-sysmodule.json`): su
  memoria sale del espacio de los applets, junto al menú y el daemon de SwitchU,
  que tiene decenas de MiB libres, y no de la memoria del sistema.
- **Errores del CDN**: un hueco en el audio ya no cambia de canción, y una página de
  error del CDN ya no se decodifica como audio.

## Cómo dependen uno del otro

- **SwitchU funciona sin Switify**, pero solo con su música propia. Para Spotify hace
  falta el servicio de Switify en
  `sdmc:/atmosphere/contents/4200000000535754/exefs.nsp`. También hay que haber
  iniciado sesión una vez con la app de Switify (NRO), porque el servicio reutiliza
  esas credenciales en `sdmc:/config/Switify/`.
- **Con el servicio oficial de Switify** Spotify también funciona, pero:
  - no hay barras de espectro;
  - tras suspender, la canción puede saltar o quedarse en silencio;
  - el heap fijo de 4 MiB puede dejar el sistema sin memoria al abrir HOME sobre un juego.
- **Este fork de Switify solo reemplaza el servicio en segundo plano**. El overlay
  y la app NRO oficiales siguen sirviendo tal cual: el servicio les responde igual
  y ellos ignoran los comandos y el flag nuevos.
- **El protocolo IPC está definido en dos sitios que deben coincidir**:
  `phase2/common/include/switify/ipc_protocol.h` en Switify y
  `projects/menu/src/core/SwitifyProtocol.hpp` en SwitchU. Un comando nuevo se añade
  en ambos.

### Compilar e instalar

- **SwitchU**: con xmake y devkitPro (ver "How to build" más abajo). `xmake install -o sd_out`
  deja en `sd_out/` la estructura de la SD.
- **Servicio de Switify**: con devkitA64 r30 (GCC 16), como el oficial. Desde la raíz de Switify:

  ```
  cmake -S phase2/sysmodule -B phase2/build_engine -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release
  ninja -C phase2/build_engine switify-background-package
  ```

  Copia `phase2/build_engine/switify-background.nsp` como
  `sdmc:/atmosphere/contents/4200000000535754/exefs.nsp`.
- **Reinicia la consola** después de cambiar el daemon de SwitchU o el servicio de Switify.

---

<!-- Fin del bloque compartido. Abajo sigue el README original del proyecto. -->

<p align="center">
  <img src="images/Switify_banner.png" alt="Switify" width="760">
</p>

<h1 align="center">Switify</h1>

<p align="center">
  A native, controller-first Spotify client and background playback overlay for Nintendo Switch homebrew.
</p>

<p align="center">
  <a href="https://github.com/DomazinUS/Switify/releases/latest"><img src="https://img.shields.io/github/v/release/DomazinUS/Switify?label=release" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/app%20license-GPL--3.0--or--later-1DB954" alt="App license: GPL-3.0-or-later"></a>
  <a href="LICENSES/GPL-2.0.txt"><img src="https://img.shields.io/badge/overlay%20license-GPL--2.0--only-1DB954" alt="Overlay license: GPL-2.0-only"></a>
</p>

Created by **HiroshiYamauchi**.

Switify plays Spotify directly on the Switch. It provides two complementary,
independently installed experiences:

| Component | Current version | Purpose |
| --- | ---: | --- |
| **Switify NRO** | **0.7.0** | Full-screen Home, Search, Library, collection browsing, first-time login, lyrics, and native playback. |
| **Switify Overlay** | **0.5.0** | Lightweight Ultrahand player and browser backed by an on-demand Atmosphere background service that can keep playing over games. |

The two products do not share a process or live playback engine. They share
only persistent authentication, settings, session metadata, and selected cache
files under `sdmc:/config/Switify/`. They can run independently at the same
time, although doing so deliberately produces two overlapping audio streams.

## Features

### Full-screen NRO

- Spotify Home with **All**, **Music**, and **Podcasts** filters
- Search filters for songs, artists, albums, playlists, and podcasts
- Liked Songs, playlists, saved albums, shows, episodes, and collection pages
- Paginated large libraries and responsive playlist rendering
- Persistent mini-player plus full Now Playing and theater modes
- Full lyrics view with live Musixmatch line synchronization when available
- Optional line-synced full-player lyrics, enabled by default
- Black, animated RGB Gradient, and animated RGB Solid full-player backgrounds
- Native shuffle, Previous, Play/Pause, Next, Repeat All, and Repeat One
- Stateful liked-track heart, queue, seeking, and persistent volume
- Optional global `Y`, `L`, `R`, `ZL`, and `ZR` playback shortcuts
- Persistent Bass/Mid/Treble equalizer
- Normal 160 kbps and High 320 kbps Spotify Vorbis quality modes
- Persistent artwork, encoded-image, and texture caches
- Last session restored **paused**; opening Switify never resumes audio by itself

### Ultrahand overlay

- Streamfin-inspired Now Playing card with artwork and progress
- Shuffle, transport, Off/All/One repeat, queue, playlist, and seeking
- Visible, focusable volume slider with persistent percentage
- Lightweight Home, Search, Library, and paginated 25-track collection views
- Shared Normal/High streaming-quality preference
- Background audio that can continue while another title is running
- A dormant service that starts only when the overlay requests it; Switify does
  not reserve its background resources at console boot
- Followed podcasts in Your Library with playable paginated episodes
- Resilient service supervision, explicit/coalesced playback controls, and
  safe resource teardown when Ultrahand closes
- Last session restored paused and silent until an explicit Play command

## Requirements

- A Nintendo Switch capable of running Atmosphere homebrew
- Recent Atmosphere and Homebrew Menu installations
- A Spotify **Premium** account (required by native Spotify playback)
- Network access; a phone or other Spotify client on the same local network is
  needed for the first pairing
- Ultrahand Overlay for the optional overlay package

The standalone NRO should be launched through title override for full memory:
hold `R` while opening a game, continue holding it until Homebrew Menu appears,
then launch Switify.

## Downloads

The [latest release](https://github.com/DomazinUS/Switify/releases/latest)
contains:

- `Switify-0.7.0.zip` — standalone full-screen application
- `Switify-Overlay-0.5.0.zip` — Ultrahand overlay and dormant background service
- `Switify-source-0.7.0.zip` — corresponding buildable source snapshot
- `SHA256SUMS.txt` — SHA-256 checksums for all three archives

## Install the standalone app

1. Download `Switify-0.7.0.zip` and extract it to the root of the SD card.
2. Confirm that the resulting file is
   `sdmc:/switch/Switify/Switify.nro`.
3. Return the SD card to the console and launch Switify through title override.
4. Complete the first login below.

### First login

Switify does **not** require a Spotify developer Client ID or client secret.
The first login uses Spotify Connect discovery:

1. Connect the Switch and a phone to the same local network.
2. Leave Switify open on the Switch while it waits for authentication.
3. Open Spotify on the phone and open **Connect to a device**.
4. Select **Switify**.
5. Wait for the Switch to confirm the connection.

Switify stores the reusable session at
`sdmc:/config/Switify/credentials.json`. Later launches use it automatically,
although playback remains paused until you press Play. The credentials file is
private account data: never attach it to an issue or include it in an SD-card
backup intended for someone else.

If Switify does not appear in the device picker, verify that both devices are
on the same non-guest Wi-Fi network, grant the phone app local-network access,
and retry with Switify still open.

## Install the overlay

Authenticate successfully in the NRO **before** installing or opening the
overlay. The overlay intentionally has no separate login flow.

1. Close the standalone NRO.
2. Download `Switify-Overlay-0.5.0.zip` and extract it to the SD-card root.
3. Confirm these installed files:

       sdmc:/switch/.overlays/Switify.ovl
       sdmc:/atmosphere/contents/4200000000535754/exefs.nsp
       sdmc:/atmosphere/contents/4200000000535754/toolbox.json

4. Reboot once so Atmosphere discovers the replaced service content.
5. Open Ultrahand and select **Switify**. The overlay launches the dormant
   service automatically and then connects to it.

There is deliberately no `boot2.flag`; the service does not start with the
console. The standalone NRO and background service are independent playback
engines. They can coexist, but normally close one before using the other to
avoid two overlapping Spotify streams.

## Updating

Extract the newer package over the existing files. For an overlay/service
update, stop the old Switify service first and reboot once after replacing the
Atmosphere content. Authentication, volume, quality, equalizer, caches, and
session state live under `sdmc:/config/Switify/` and are not part of the
release archive.

## Known limitations

- Spotify Premium is required.
- The standalone NRO and background service are separate native Spotify/audio
  engines. Running both deliberately can produce two slightly offset audio
  streams.
- Switify streams audio and does not download Spotify music for offline use.
- Switify depends on undocumented Spotify protocols that Spotify may change.

## Diagnostics

Before reporting a problem, preserve the log generated by the affected build:

    sdmc:/config/Switify/app.log
    sdmc:/config/Switify/app.log.previous
    sdmc:/config/Switify/background.log
    sdmc:/config/Switify/background.log.previous
    sdmc:/atmosphere/crash_reports/

Review logs before posting them because account identifiers may be present.
Never publish `credentials.json`. Hardware crash reports can be symbolicated
with `symbolicate-crash.ps1` and the matching private build ELF.

## Build from source

See [BUILDING.md](BUILDING.md) for the devkitPro requirements, standalone and
overlay build commands, outputs, NetLoader loop, and release packaging.

## Repository layout

    app/                 Full-screen UI, catalog, state, and resources
    targets/switch/      Shared libnx audio, player, logging, and discovery
    vendor/cspot/        Modified cspot/bell Spotify and playback stack
    vendor/borealis/     Controller-first UI framework
    phase2/              Background service, IPC, tests, and Ultrahand overlay
    images/              Public repository artwork
    LICENSES/            Consolidated primary license texts and index

## Project history

Switify began as a hardware feasibility proof and grew into the standalone
native application plus an independent background product. The complete public
development history is in [CHANGELOG.md](CHANGELOG.md). Release-specific notes
are in [RELEASE_NOTES.md](RELEASE_NOTES.md).

## Credits

- **HiroshiYamauchi** — creator and maintainer
- [cspot](https://github.com/feelfreelinux/cspot) — Spotify protocol and native playback foundation
- [Borealis](https://github.com/natinusala/borealis) — standalone controller-first UI framework
- [libultrahand](https://github.com/ppkantorski/libultrahand) / libtesla — overlay framework
- [Streamfin Switch](https://github.com/dammitjeff/streamfin-switch) — background-player and overlay UX reference
- [sys-tune](https://github.com/ppkantorski/sys-tune) and sys-clk — sysmodule/IPC architectural references
- [librespot](https://github.com/librespot-org/librespot), [psst](https://github.com/jpochyla/psst), [Spotix](https://github.com/wrexik/Spotix), and [Spotube](https://github.com/KRTirtho/spotube) — protocol, platform, and UX research references

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[phase2/THIRD_PARTY_NOTICES.md](phase2/THIRD_PARTY_NOTICES.md) for the exact
attribution boundary.

## License

- The standalone application and background engine are distributed under
  **GPL-3.0-or-later**. See [LICENSE](LICENSE).
- The separately built Ultrahand overlay is **GPL-2.0-only**. Its license is
  retained with the overlay source and consolidated under
  [LICENSES/GPL-2.0.txt](LICENSES/GPL-2.0.txt).
- Borealis is Apache-2.0. Every bundled dependency retains its license beside
  its source; [LICENSES/README.md](LICENSES/README.md) is the public license
  index.

Switify is independent, unofficial, and not affiliated with or endorsed by
Spotify, Nintendo, or their subsidiaries. Spotify and Nintendo names and marks
belong to their respective owners.
